#pragma once
#include <vector>
#include <random>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "board.h"
#include "timer.h"
#include "sudokusolver.h"

// Forward declaration - must be before multicolonyantsystem.h to allow friend declaration
class MultiColonyAntSystem;

#include "multicolonyantsystem.h"
#include "colonyant.h"

// Forward declaration
class MultiThreadMultiColonyAntSystem;

// Multi-colony thread class representing one thread running a multi-colony ant system
// Each thread contains multiple colonies (ACS and MMAS types)
class MultiColonyThread
{
private:
	int threadId;
	MultiColonyAntSystem* multiColonySystem;
	
	Board iterationBest;      // Best solution in current iteration
	Board bestSol;            // Best solution found so far
	Board receivedIterationBest;  // Received iteration-best from ring topology
	Board receivedBestSol;        // Received best-so-far from random topology
	
	int iterationBestScore;   // Number of cells filled in iteration best
	int bestSolScore;         // Number of cells filled in best so far
	int receivedIterationBestScore;  // Score of received iteration-best
	int receivedBestSolScore;        // Score of received best-so-far
	
	int currentIteration;
	int numCells;  // Number of cells in the puzzle
	int numUnits;  // Number of units (values per cell)
	Timer dcmAcoTimer;  // Main DCM-ACO algorithm timer (paused during CP and tracked components)
	float dcmAcoTime;  // Time spent in main DCM-ACO algorithm work (seconds)
	std::atomic<float> cpTime;  // Total CP time for this thread (atomic for thread-safe accumulation)
	float cooperativeGameTime;      // Total Cooperative Game Theory time (seconds)
	float pheromoneFusionTime;      // Total Pheromone Fusion time (seconds)
	float publicPathRecommendationTime;  // Total Public Path Recommendation time (seconds)
	float communicationTime;        // Total Communication Between Threads time (seconds)
	float idleTime;
	int barrierEntries;
	float idleMean;                 
	float idleM2;                   
	
	// Helper method for pheromone calculation
	float PherAdd(int cellsFilled);
	
public:
	MultiColonyThread(int id, int antsPerColony, float q0, float rho, float pher0, float bestEvap,
	               int numColonies, int numACS, float convThreshold, float entropyThreshold, float xi);
	~MultiColonyThread();
	
	// Initialize for a new puzzle
	void Initialize(const Board& puzzle);
	
	// Run one iteration of the multi-colony system
	void RunIteration(const Board& puzzle);
	
	// Update pheromone with communication (only for MMAS colonies)
	void UpdatePheromoneWithCommunication();
	
	// Update MMAS pheromone locally when there is no communication (single MMAS colony)
	void UpdateMmasPheromoneLocal();
	
	// Get results
	const Board& GetIterationBest() const { return iterationBest; }
	const Board& GetBestSol() const { return bestSol; }
	int GetIterationBestScore() const { return iterationBestScore; }
	int GetBestSolScore() const { return bestSolScore; }
	int GetCurrentIteration() const { return currentIteration; }
	void SetCurrentIteration(int iter) { currentIteration = iter; }
	float GetDCMAcoTime() const { return dcmAcoTime; }
	float GetCPTime() const { return cpTime.load(); }
	void ResetCPTime() { cpTime.store(0.0f); }
	std::atomic<float>* GetCPTimePtr() { return &cpTime; }
	Timer* GetDCMAcoTimerPtr() { return &dcmAcoTimer; }  // For pause/resume access
	void SetDCMAcoTime(float time) { dcmAcoTime = time; }  // For setting the final time
	
	float GetCooperativeGameTime() const { return cooperativeGameTime; }
	float GetPheromoneFusionTime() const { return pheromoneFusionTime; }
	float GetPublicPathRecommendationTime() const { return publicPathRecommendationTime; }
	float GetCommunicationTime() const { return communicationTime; }
	void AddCommunicationTime(float elapsed) { communicationTime += elapsed; }
	float GetIdleTime() const { return idleTime; }
	float GetIdleMean() const { return idleMean; }
	float GetIdleM2() const { return idleM2; }
	
	void AddIdleTime(float elapsed) { 
		idleTime += elapsed; 
		
		// WELFORD's ONLINE ALGORITHM
		// (barrierEntries is already incremented before this is called)
		float delta = elapsed - idleMean;
		idleMean += delta / barrierEntries; 
		float delta2 = elapsed - idleMean;
		idleM2 += delta * delta2;
	}
	//void AddIdleTime(float elapsed) { idleTime += elapsed; }
	int GetBarrierEntries() const { return barrierEntries; }
	void IncrementBarrierEntries() { barrierEntries++; }

	void ResetAllTimers() 
	{ 
		dcmAcoTime = 0.0f;
		cpTime.store(0.0f);
		cooperativeGameTime = 0.0f;
		pheromoneFusionTime = 0.0f;
		publicPathRecommendationTime = 0.0f;
		communicationTime = 0.0f;
		idleTime = 0.0f;         
		barrierEntries = 0;     
		idleMean = 0.0f;         // NEW
		idleM2 = 0.0f;           // NEW 
	}
	
	// Set solutions (for communication)
	void ReceiveIterationBest(const Board& solution);
	void ReceiveBestSol(const Board& solution);
	
	// Get access to underlying multi-colony system for advanced operations
	MultiColonyAntSystem* GetMultiColonySystem() { return multiColonySystem; }
};

// Multi-Thread Multi-Colony Ant System
// Each thread runs an independent MultiColonyAntSystem with multiple colonies inside
class MultiThreadMultiColonyAntSystem : public SudokuSolver
{
private:
	int numThreads;
	float maxTime;  // Maximum time in seconds
	std::vector<MultiColonyThread*> threads;
	
	Board globalBest;
	int globalBestScore;
	int iterationsCompleted;
	bool communicationOccurred;
	float solTime;
	Timer solutionTimer;
	
	std::mt19937 masterRandGen;
	
	// Synchronization
	std::mutex commMutex;
	std::condition_variable commCV;
	std::atomic<int> barrier;
	std::atomic<bool> stopFlag;
	
	// Communication interval parameters
	int commEarlyInterval;  // Communication interval for iterations < commThreshold
	int commLateInterval;   // Communication interval for iterations >= commThreshold
	int commThreshold;      // Iteration number when switching from early to late interval
	// When false, threads never barrier-synchronize or exchange (ring/random); local MMAS update only
	bool threadCommunicationEnabled;
	
	// Communication helpers
	int CalculateInterval(int iteration);
	std::vector<int> GenerateMatchArray();
	void CommunicateRingTopology();
	void CommunicateRandomTopology(const std::vector<int>& matchArray);
	
	// Thread worker and helper methods
	void ThreadWorker(int threadId, const Board& puzzle);
	
	// Modular helper methods for ThreadWorker
	bool CheckTimeout();
	void ReportProgress(int threadId, int iteration, MultiColonyThread* thread, const Board& puzzle);
	bool CheckSolutionFound(MultiColonyThread* thread);
	void PerformBarrierSynchronization(MultiColonyThread* thread, const Board& puzzle);
	void ExecuteMasterThreadTasks(const Board& puzzle);
	void ExecuteWorkerThreadWait(std::unique_lock<std::mutex>& lock);
	
public:
	MultiThreadMultiColonyAntSystem(int numThreads, int antsPerColony, float q0, float rho,
	                             float pher0, float bestEvap, int numColonies, int numACS,
	                             float convThreshold, float entropyThreshold, float xi,
	                             int commEarlyInterval = 100, int commLateInterval = 10, int commThreshold = 200,
	                             bool threadCommunicationEnabled = true);
	~MultiThreadMultiColonyAntSystem();
	
	virtual bool Solve(const Board& puzzle, float maxTime);
	virtual float GetSolutionTime() { return solTime; }
	virtual const Board& GetSolution() { return globalBest; }
	int GetIterationsCompleted() { return iterationsCompleted; }
	bool GetCommunicationOccurred() { return communicationOccurred; }
	void PrintThreadDetails();
	
	// Access to threads for per-thread statistics
	const std::vector<MultiColonyThread*>& GetThreads() const { return threads; }
	MultiColonyThread* GetThread(int index) const { return (index >= 0 && index < (int)threads.size()) ? threads[index] : nullptr; }
};
