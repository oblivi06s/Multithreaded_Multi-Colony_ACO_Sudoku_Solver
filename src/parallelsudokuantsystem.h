#pragma once
#include <vector>
#include <random>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "antcolonyinterface.h"
#include "sudokuant.h"
#include "board.h"
#include "timer.h"
#include "sudokusolver.h"

// Forward declaration
class ParallelSudokuAntSystem;

// Sub-colony class representing one thread's ant colony
class SubColony : public IAntColony
{
private:
	int numAnts;
	float q0;
	float rho;        // ACS evaporation parameter (used for both standard and communication updates)
	float pher0;
	
	Board iterationBest;      // Best solution in current iteration (ΔT_ij^1 - local)
	Board bestSol;            // Best solution found so far (used in standard Algorithm 0 update)
	Board receivedIterationBest;  // Received iteration-best from ring topology (ΔT_ij^2)
	Board receivedBestSol;        // Received best-so-far from random topology (ΔT_ij^3)
	
	int iterationBestScore;   // Number of cells filled in iteration best
	int bestSolScore;         // Number of cells filled in best so far
	int receivedIterationBestScore;  // Score of received iteration-best
	int receivedBestSolScore;        // Score of received best-so-far
	
	std::vector<SudokuAnt*> antList;
	std::mt19937 randGen;
	std::uniform_real_distribution<float> randomDist;
	std::uniform_int_distribution<int> startPosDist;  // For ant starting positions (reused)
	
	float **pher; // pheromone matrix
	int numCells;
	int numUnits;
	
	// Temporary arrays for pheromone updates (allocated once, reused every iteration)
	float* contributions;
	bool* hasContribution;
	
	// Timing for this thread
	Timer acsTimer;  // Main ACS algorithm timer (paused during CP and communication)
	float acsTime;  // Time spent in main ACS algorithm work
	std::atomic<float> cpTime;  // Atomic for thread-safe accumulation from CP rules
	float communicationTime;  // Communication time for this thread
	
	void InitPheromone(int numCells, int valuesPerCell);
	void ClearPheromone();
	float PherAdd(int numCellsFixed);
	
public:
	int currentIteration;     // Current iteration number (public for access by worker)
	float bestPher;           // Best pheromone value (for Algorithm 0 standard update)
	float bestEvap;           // Best pheromone evaporation parameter
	
	SubColony(int id, int numAnts, float q0, float rho, float pher0, float bestEvap);
	~SubColony();
	
	// Run one iteration of the ant colony
	void RunIteration(const Board& puzzle);
	
	// Standard Algorithm 0 global pheromone update - called every iteration
	void UpdatePheromone();
	
	// Communication-based three-source pheromone update - called after exchanges
	void UpdatePheromoneWithCommunication();
	
	// Get results
	const Board& GetIterationBest() const { return iterationBest; }
	const Board& GetBestSol() const { return bestSol; }
	int GetIterationBestScore() const { return iterationBestScore; }
	int GetBestSolScore() const { return bestSolScore; }
	int GetCurrentIteration() const { return currentIteration; }
	
	// Timing getters and setters
	float GetACSTime() const { return acsTime; }
	float GetCPTime() const { return cpTime.load(); }
	float GetCommunicationTime() const { return communicationTime; }
	std::atomic<float>* GetCPTimePtr() { return &cpTime; }
	Timer* GetACSTimerPtr() { return &acsTimer; }  // For pause/resume access
	void SetACSTime(float time) { acsTime = time; }  // For setting the final time
	void AddCommunicationTime(float elapsed) { communicationTime += elapsed; }
	void ResetCPTime() { cpTime.store(0.0f); }
	void ResetCommunicationTime() { communicationTime = 0.0f; }
	
	// Set solutions (for communication)
	void ReceiveIterationBest(const Board& solution);
	void ReceiveBestSol(const Board& solution);
	
	// Reset for new puzzle
	void Initialize(const Board& puzzle);
	
	// Helpers for ants
	inline float Getq0() { return q0; }
	inline float random() { return randomDist(randGen); }
	inline float Pher(int i, int j) { return pher[i][j]; }
	void LocalPheromoneUpdate(int iCell, int iChoice);
};

// Parallel Ant Colony System with multiple sub-colonies
class ParallelSudokuAntSystem : public SudokuSolver
{
private:
	int numSubColonies;
	float maxTime;  // Maximum time in seconds
	std::vector<SubColony*> subColonies;
	
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
	
	// Communication helpers
	std::vector<int> GenerateMatchArray();
	void CommunicateRingTopology();
	void CommunicateRandomTopology(const std::vector<int>& matchArray);
	
	// Thread worker and helper methods
	void SubColonyWorker(int colonyId, const Board& puzzle);
	
	// Modular helper methods for SubColonyWorker
	bool CheckTimeout();
	void ReportProgress(int colonyId, int iteration, SubColony* colony, const Board& puzzle);
	bool CheckSolutionFound(SubColony* colony);
	void PerformBarrierSynchronization(const Board& puzzle);
	void ExecuteMasterThreadTasks(const Board& puzzle);
	void ExecuteWorkerThreadWait(std::unique_lock<std::mutex>& lock);
	
public:
	ParallelSudokuAntSystem(int numSubColonies, int numAntsPerColony, 
	                        float q0, float rho, float pher0, float bestEvap,
	                        int commEarlyInterval = 100, int commLateInterval = 10, int commThreshold = 200);
	~ParallelSudokuAntSystem();
	
	virtual bool Solve(const Board& puzzle, float maxTime);
	virtual float GetSolutionTime() { return solTime; }
	virtual const Board& GetSolution() { return globalBest; }
	int GetIterationsCompleted() { return iterationsCompleted; }
	bool GetCommunicationOccurred() { return communicationOccurred; }
	
	// Access to sub-colonies for per-thread statistics
	const std::vector<SubColony*>& GetSubColonies() const { return subColonies; }
	SubColony* GetSubColony(int index) const { return (index >= 0 && index < (int)subColonies.size()) ? subColonies[index] : nullptr; }
};

