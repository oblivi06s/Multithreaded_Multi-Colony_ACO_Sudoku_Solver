/*******************************************************************************
 * MULTI-THREAD MULTI-COLONY ANT COLONY SYSTEM FOR SUDOKU
 * 
 * This implementation uses multiple threads, each running a MultiColonyAntSystem.
 * Each thread contains multiple colonies (ACS and MMAS types).
 * 
 * Key Features:
 * - Multi-threaded execution (one thread per MultiColonyAntSystem)
 * - Each thread contains multiple colonies (ACS and MMAS)
 * - Two communication topologies:
 *   1. Ring topology: iteration-best solutions
 *   2. Random topology: best-so-far solutions
 * - Communication at iteration intervals (similar to MultiThreadSudokuAntSystem)
 * - Timeout-based termination (default 120 seconds)
 * - Immediate stop upon finding complete solution
 ******************************************************************************/

#include "multithreadmulticolonyantsystem.h"
#include "constraintpropagation.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <climits>
#include <limits>
#include <cmath>
#include <iomanip>

// ============================================================================
// MULTICOLONYTHREAD CLASS - Represents one thread running a multi-colonyavdI system
// ============================================================================

// ----------------------------------------------------------------------------
// Constructor: Initialize a multi-colony thread with its own parameters
// ----------------------------------------------------------------------------
MultiColonyThread::MultiColonyThread(int id, int antsPerColony, float q0, float rho, float pher0, float bestEvap,
                                     int numColonies, int numACS, float convThreshold, float entropyThreshold, float xi)
	: threadId(id), currentIteration(0), iterationBestScore(0), bestSolScore(0),
	  receivedIterationBestScore(0), receivedBestSolScore(0), numCells(0), numUnits(0), 
	  dcmAcoTime(0.0f), cpTime(0.0f), cooperativeGameTime(0.0f), pheromoneFusionTime(0.0f), 
	  publicPathRecommendationTime(0.0f), communicationTime(0.0f),
	  idleTime(0.0f), barrierEntries(0), idleMean(0.0f), idleM2(0.0f)
{
	// Create the multi-colony system
	multiColonySystem = new MultiColonyAntSystem(antsPerColony, q0, rho, pher0, bestEvap,
	                                             numColonies, numACS, convThreshold, entropyThreshold, xi);
}

MultiColonyThread::~MultiColonyThread()
{
	if (multiColonySystem != nullptr)
		delete multiColonySystem;
}

// ============================================================================
// MULTITHREAD MULTI-COLONY ANT COLONY SYSTEM - Master Coordinator
// ============================================================================

// ----------------------------------------------------------------------------
// Constructor: Create the multithread system with N threads
// ----------------------------------------------------------------------------
MultiThreadMultiColonyAntSystem::MultiThreadMultiColonyAntSystem(int nThreads, int antsPerColony,
	float q0, float rho, float pher0, float bestEvap, int numColonies, int numACS,
	float convThreshold, float entropyThreshold, float xi,
	int commEarlyInterval, int commLateInterval, int commThreshold, bool threadCommunicationEnabled)
	: numThreads(nThreads), maxTime(120.0f),
	  globalBestScore(0), iterationsCompleted(0), communicationOccurred(false), 
	  solTime(0.0f), barrier(0), stopFlag(false),
	  commEarlyInterval(commEarlyInterval), commLateInterval(commLateInterval), commThreshold(commThreshold),
	  threadCommunicationEnabled(threadCommunicationEnabled)
{
	// Allow threads=1 (behaves like Algorithm 3)
	// No minimum thread requirement
	
	// Create N independent threads (each with its own MultiColonyAntSystem)
	for (int i = 0; i < numThreads; i++)
	{
		threads.push_back(new MultiColonyThread(i, antsPerColony, q0, rho, pher0, bestEvap,
		                                       numColonies, numACS, convThreshold, entropyThreshold, xi));
	}
	
	// Initialize master random generator (for random topology matching)
	std::random_device rd;
	masterRandGen = std::mt19937(rd());
}

MultiThreadMultiColonyAntSystem::~MultiThreadMultiColonyAntSystem()
{
	for (auto thread : threads)
		delete thread;
}

// ----------------------------------------------------------------------------
// Initialize: Prepare thread for a new puzzle
// Called once before starting the parallel algorithm
// ----------------------------------------------------------------------------
void MultiColonyThread::Initialize(const Board& puzzle)
{
	// Initialize the multi-colony system's state
	MultiColonyAntSystem* mcas = multiColonySystem;
	const int nACS = (std::min)(mcas->numACS, mcas->numColonies);
	
	// Store puzzle dimensions
	numCells = puzzle.CellCount();
	numUnits = puzzle.GetNumUnits();
	
	// Initialize colonies (similar to MultiColonyAntSystem::Solve initialization)
	mcas->colonyQ0.resize(mcas->numColonies);
	mcas->colonyRho.resize(mcas->numColonies);
	
	for (int c = 0; c < mcas->numColonies; ++c)
	{
		// Initialize pheromone
		int numCells = puzzle.CellCount();
		int valuesPerCell = puzzle.GetNumUnits();
		
		// Clear old pheromone if exists
		if (mcas->colonies[c].pher != nullptr)
		{
			for (int i = 0; i < mcas->colonies[c].numCells; i++)
				delete[] mcas->colonies[c].pher[i];
			delete[] mcas->colonies[c].pher;
		}
		
		mcas->colonies[c].numCells = numCells;
		mcas->colonies[c].valuesPerCell = valuesPerCell;
		mcas->colonies[c].pher = new float*[numCells];
		for (int i = 0; i < numCells; i++)
		{
			mcas->colonies[c].pher[i] = new float[valuesPerCell];
			for (int j = 0; j < valuesPerCell; j++)
				mcas->colonies[c].pher[i][j] = mcas->pher0;
		}
		
		mcas->colonies[c].bestPher = 0.0f;
		mcas->colonies[c].bestVal = 0;
		mcas->colonies[c].tau0 = mcas->pher0;
		
		// Assign colony type
		mcas->colonies[c].type = (c < nACS ? 0 : 1);
		
		// Parameters per colony type
		if (mcas->colonies[c].type == 0)
		{
			mcas->colonyQ0[c] = mcas->q0;
			mcas->colonyRho[c] = mcas->rho;
		}
		else
		{
			mcas->colonyQ0[c] = 0.0f;
			mcas->colonyRho[c] = 0.1f;
		}
		
		// Initial Max-Min bounds
		if (mcas->colonies[c].type == 1) // MMAS
		{
			float rho_mmas = mcas->colonyRho[c];
			float best_pheromone_toAdd = mcas->pher0;
			float n = (float)numCells;
			mcas->colonies[c].tauMax = best_pheromone_toAdd / rho_mmas;
			mcas->colonies[c].tauMin = mcas->colonies[c].tauMax / (2.0f * n);
		}
		
		mcas->colonies[c].lastImproveIter = 0;
		
		// Clear old ants
		for (auto* a : mcas->colonies[c].ants)
			delete a;
		mcas->colonies[c].ants.clear();
		
		// Create new ants
		for (int i = 0; i < mcas->antsPerColony; i++)
			mcas->colonies[c].ants.push_back(new ColonyAnt(mcas, c));
	}
	
	mcas->globalBestPher = 0.0f;
	mcas->globalBestVal = 0;
	
	// Initialize solution tracking
	iterationBest.Copy(puzzle);
	bestSol.Copy(puzzle);
	receivedIterationBest.Copy(puzzle);
	receivedBestSol.Copy(puzzle);
	
	iterationBestScore = puzzle.FixedCellCount();
	bestSolScore = puzzle.FixedCellCount();
	receivedIterationBestScore = 0;
	receivedBestSolScore = 0;
	currentIteration = 0;
	ResetAllTimers();  // Reset all timing variables for new puzzle
}


// ============================================================================
// MAIN SOLVE METHOD - Entry point for multithread algorithm
// ============================================================================
bool MultiThreadMultiColonyAntSystem::Solve(const Board& puzzle, float timeLimit)
{
	// Initialization
	maxTime = (timeLimit > 0) ? timeLimit : 120.0f;
	
	solutionTimer.Reset();
	stopFlag.store(false);
	barrier.store(0);
	
	globalBest.Copy(puzzle);
	globalBestScore = puzzle.FixedCellCount();
	
	// Thread creation
	std::vector<std::thread> threadObjects;
	for (int i = 0; i < numThreads; i++)
	{
		threadObjects.emplace_back(&MultiThreadMultiColonyAntSystem::ThreadWorker, this, i, std::ref(puzzle));
	}
	
	// Parallel execution happens in ThreadWorker
	
	// Thread synchronization
	for (auto& threadObj : threadObjects)
	{
		threadObj.join();
	}
	
	// Result collection
	iterationsCompleted = 0;
	for (int i = 0; i < numThreads; i++)
	{
		if (threads[i]->GetBestSolScore() > globalBestScore)
		{
			globalBest.Copy(threads[i]->GetBestSol());
			globalBestScore = threads[i]->GetBestSolScore();
		}
		
		if (threads[i]->GetCurrentIteration() > iterationsCompleted)
		{
			iterationsCompleted = threads[i]->GetCurrentIteration();
		}
	}
	
	solTime = solutionTimer.Elapsed();
	
	bool solved = (globalBestScore == puzzle.CellCount());
	return solved;
}



// ============================================================================
// THREAD WORKER - Main parallel execution loop
// ============================================================================
void MultiThreadMultiColonyAntSystem::ThreadWorker(int threadId, const Board& puzzle)
{
	MultiColonyThread* thread = threads[threadId];
	thread->Initialize(puzzle);  // Initialize already calls ResetAllTimers()
	
	// Reset the DCM-ACO timer
	thread->GetDCMAcoTimerPtr()->Reset();
	
	// Register this thread's CP time pointer for per-thread tracking
	CP::RegisterThreadCPTime(thread->GetCPTimePtr());
	
	int iter = 0;
	
	// === OPTIMIZATION: Use local bool for single thread to avoid atomic overhead ===
	bool shouldStop = false;
	bool useLocalStop = (numThreads == 1);
	
	// Main iteration loop
	while (useLocalStop ? !shouldStop : !stopFlag.load())
	{
		// Check termination conditions
		if (CheckTimeout())
		{
			if (useLocalStop)
				shouldStop = true;
			break;
		}
		
		iter++;
		// Update currentIteration before running iteration so it's synchronized
		thread->SetCurrentIteration(iter);
		
		// Run thread iteration (independent parallel work)
		// This runs one iteration of the MultiColonyAntSystem (with all its colonies)
		thread->RunIteration(puzzle);
		
		// Communication phase (periodic barrier synchronization)
		// Skip communication if only 1 thread (behaves like Algorithm 3), or if disabled via CLI (--threads-comm 0)
		bool shouldCommunicate = false;
		if (threadCommunicationEnabled && numThreads > 1)
		{
			int interval = CalculateInterval(iter);
			shouldCommunicate = (iter % interval == 0);
		}
		// If numThreads == 1 or threadCommunicationEnabled is false, shouldCommunicate stays false
		
		if (shouldCommunicate)
		{
			// Time Communication Between Threads
			auto startTime = std::chrono::steady_clock::now();
			PerformBarrierSynchronization(thread, puzzle);
			auto endTime = std::chrono::steady_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
			thread->AddCommunicationTime((float)duration.count());
			
			// After communication, update pheromone with received solutions
			// Only MMAS colonies receive this update (ACS colonies are not updated)
			thread->UpdatePheromoneWithCommunication();
			
			if (useLocalStop ? shouldStop : stopFlag.load())
				break;
		}
		else
		{
			// No communication this iteration -> apply normal MMAS pheromone update
			// (ACS updates are already handled inside RunIteration)
			thread->UpdateMmasPheromoneLocal();
		}
		
		// Report progress
		ReportProgress(threadId, iter, thread, puzzle);
		
		// Check if solution found
		if (CheckSolutionFound(thread))
		{
			if (useLocalStop)
				shouldStop = true;
			break;
		}
	}
	
	// Get the DCM-ACO time (will be calculated by subtraction in solvermain.cpp)
	thread->SetDCMAcoTime(thread->GetDCMAcoTimerPtr()->Elapsed());
	CP::UnregisterThreadCPTime();
}



// ----------------------------------------------------------------------------
// RunIteration: Execute one complete iteration of the multi-colony system
// This replicates the logic from MultiColonyAntSystem::Solve() but for one iteration
// ----------------------------------------------------------------------------
void MultiColonyThread::RunIteration(const Board& puzzle)
{
	MultiColonyAntSystem* mcas = multiColonySystem;
	
	std::uniform_int_distribution<int> startDist(0, puzzle.CellCount() - 1);
	
	// Init ant solutions with different starts
	for (int c = 0; c < mcas->numColonies; ++c)
	{
		for (auto* a : mcas->colonies[c].ants)
			a->InitSolution(puzzle, startDist(mcas->randGen));
	}
	
	// Construct solutions cell by cell
	for (int i = 0; i < puzzle.CellCount(); i++)
	{
		for (int c = 0; c < mcas->numColonies; ++c)
		{
			for (auto* a : mcas->colonies[c].ants)
				a->StepSolution();
		}
	}
	
	// Find best iteration solution across all colonies
	int bestIterVal = 0;
	Board bestIterSol;
	bestIterSol.Copy(puzzle);
	
	// Per-colony: evaluate bests and track global best
	for (int c = 0; c < mcas->numColonies; ++c)
	{
		int iBest = 0;
		int bestVal = 0;
		auto& ants = mcas->colonies[c].ants;
		for (unsigned int i = 0; i < ants.size(); i++)
		{
			if (ants[i]->NumCellsFilled() > bestVal)
			{
				bestVal = ants[i]->NumCellsFilled();
				iBest = (int)i;
			}
		}
		
		// Track iteration best (best across all colonies this iteration)
		if (bestVal > bestIterVal)
		{
			bestIterVal = bestVal;
			bestIterSol.Copy(ants[iBest]->GetSolution());
		}
		
		float pherToAdd = mcas->PherAdd(mcas->colonies[c].numCells, bestVal);
		if (pherToAdd > mcas->colonies[c].bestPher)
		{
			mcas->colonies[c].bestSol.Copy(ants[iBest]->GetSolution());
			mcas->colonies[c].bestPher = pherToAdd;
			mcas->colonies[c].bestVal = bestVal;
			mcas->colonies[c].lastImproveIter = currentIteration;
			
			// Update Max-Min bounds with improvement (for MMAS colonies)
			if (mcas->colonies[c].type == 1)
			{
				float rho_mmas = mcas->GetRho(c);
				float best_pheromone_toAdd = mcas->colonies[c].bestPher;
				float n = (float)mcas->colonies[c].valuesPerCell;
				
				mcas->colonies[c].tauMax = best_pheromone_toAdd / rho_mmas;
				mcas->colonies[c].tauMin = mcas->colonies[c].tauMax / (2.0f * n);
			}
		}
		
		// Update global best (within this sub-colony)
		if (mcas->colonies[c].bestPher > mcas->globalBestPher)
		{
			mcas->globalBestPher = mcas->colonies[c].bestPher;
			mcas->globalBestSol.Copy(mcas->colonies[c].bestSol);
			mcas->globalBestVal = mcas->colonies[c].bestVal;
		}
	}
	
	// Update iteration best
	iterationBest.Copy(bestIterSol);
	iterationBestScore = bestIterVal;
	
	// Update best-so-far (best across all colonies in this sub-colony)
	if (mcas->globalBestVal > bestSolScore)
	{
		bestSol.Copy(mcas->globalBestSol);
		bestSolScore = mcas->globalBestVal;
	}
	
	// Partition indices by type
	std::vector<int> acsIdx;
	acsIdx.reserve(mcas->numColonies);
	std::vector<int> mmasIdx;
	mmasIdx.reserve(mcas->numColonies);
	for (int c = 0; c < mcas->numColonies; ++c)
		(mcas->colonies[c].type == 0 ? acsIdx : mmasIdx).push_back(c);
	
	// Check ACS entropy: split by threshold and apply appropriate mechanism per colony
	// Low entropy (< threshold) -> pheromone fusion
	// High entropy (>= threshold) -> cooperative game allocation
	std::vector<float> acsAllocated;
	acsAllocated.resize(mcas->numColonies, 0.0f);
	std::vector<int> acsLowEntropy;   // Below threshold -> pheromone fusion
	std::vector<int> acsHighEntropy; // Above threshold -> cooperative game
	
	if (!acsIdx.empty())
	{
		// Split ACS colonies by entropy threshold
		for (int cidx : acsIdx)
		{
			if (mcas->ComputeEntropy(mcas->colonies[cidx]) < mcas->entropyThreshold)
				acsLowEntropy.push_back(cidx);
			else
				acsHighEntropy.push_back(cidx);
		}
		
		// Apply pheromone fusion to low entropy colonies
		if (!acsLowEntropy.empty() && !mmasIdx.empty())
		{
			// Time Pheromone Fusion
			auto startTime = std::chrono::steady_clock::now();
			mcas->ApplyPheromoneFusion(acsLowEntropy, mmasIdx);
			auto endTime = std::chrono::steady_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
			pheromoneFusionTime += (float)duration.count();
		}
		
		// Apply cooperative game allocation to high entropy colonies
		if (!acsHighEntropy.empty())
		{
			// Time Cooperative Game Theory
			auto startTime = std::chrono::steady_clock::now();
			mcas->ACSCooperativeGameAllocate(acsHighEntropy, acsAllocated);
			auto endTime = std::chrono::steady_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
			cooperativeGameTime += (float)duration.count();
			
			// Update pheromone for high entropy colonies (using cooperative game allocation)
			for (int c : acsHighEntropy)
			{
				float add = acsAllocated[c];  // Use allocated pheromone from cooperative game
				mcas->UpdatePheromone(c, mcas->colonies[c], mcas->colonies[c].bestSol, add);
				mcas->colonies[c].bestPher *= (1.0f - mcas->bestEvap);
			}
		}
	}
	
	// Check MMAS convergence speed: apply appropriate mechanism
	// Low convergence speed (< threshold) -> public path recommendation
	// High convergence speed (>= threshold) -> update pheromone (handled in ThreadWorker if no communication)
	if (!mmasIdx.empty())
	{
		int mmasCidx = mmasIdx[0];  // Single MMAS colony
		// convergence rate con_t = iter_opt / iter_t
		float con_t = (currentIteration > 0 ? ((float)mcas->colonies[mmasCidx].lastImproveIter / (float)currentIteration) : 1.0f);
		
		if (con_t < mcas->convThreshold)
		{
			// Low convergence speed -> public path recommendation
			if (!acsIdx.empty())
			{
				// Time Public Path Recommendation
				auto startTime = std::chrono::steady_clock::now();
				mcas->ApplyPublicPathRecommendation(currentIteration, acsIdx, std::vector<int>{mmasCidx});
				auto endTime = std::chrono::steady_clock::now();
				auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
				publicPathRecommendationTime += (float)duration.count();
			}
		}
		// Note: High convergence speed -> normal UpdatePheromone is handled in ThreadWorker
		// (either via UpdatePheromoneWithCommunication or UpdateMmasPheromoneLocal)
	}
	
	// Note: currentIteration is now set by ThreadWorker before calling RunIteration
	// so we don't increment it here to avoid double-counting
}

void MultiThreadMultiColonyAntSystem::PerformBarrierSynchronization(MultiColonyThread* thread, const Board& puzzle)
{
	if (stopFlag.load())
		return;

	thread->IncrementBarrierEntries(); // Count this synchronization event
	auto start_idle = std::chrono::steady_clock::now(); // Start the idle timer
	
	std::unique_lock<std::mutex> lock(commMutex);
	
	if (stopFlag.load())
	{
		barrier.store(0);
		commCV.notify_all();
		return;
	}
	
	int arrived = barrier.fetch_add(1) + 1;
	
	if (arrived == numThreads)
	{
		
		auto end_idle = std::chrono::steady_clock::now();
		thread->AddIdleTime((float)std::chrono::duration_cast<std::chrono::duration<double>>(end_idle - start_idle).count());
		ExecuteMasterThreadTasks(puzzle);
	}
	else
	{
		ExecuteWorkerThreadWait(lock);

		// When it wakes up, stop the idle timer
		auto end_idle = std::chrono::steady_clock::now();
		thread->AddIdleTime((float)std::chrono::duration_cast<std::chrono::duration<double>>(end_idle - start_idle).count());
	}
}

// ----------------------------------------------------------------------------
// ExecuteMasterThreadTasks: Tasks performed by the last thread to arrive
// This thread becomes the "master" and performs communication
// ----------------------------------------------------------------------------
void MultiThreadMultiColonyAntSystem::ExecuteMasterThreadTasks(const Board& puzzle)
{
	// Mark that communication occurred
	communicationOccurred = true;
	
	// Generate random matching for topology 2
	std::vector<int> matchArray = GenerateMatchArray();
	
	// --- COMMUNICATION TOPOLOGY 1: Ring (iteration-best) ---
	CommunicateRingTopology();
	
	// --- COMMUNICATION TOPOLOGY 2: Random (best-so-far) ---
	CommunicateRandomTopology(matchArray);
	
	// Check if any solution is complete
	for (int i = 0; i < numThreads; i++)
	{
		if (threads[i]->GetBestSolScore() == threads[i]->GetBestSol().CellCount())
		{
			stopFlag.store(true);
			break;
		}
	}
	
	// Reset barrier for next communication cycle
	barrier.store(0);
	
	// Release all waiting worker threads
	commCV.notify_all();
}


// ============================================================================
// COMMUNICATION TOPOLOGY 1: Ring Network
// ============================================================================
void MultiThreadMultiColonyAntSystem::CommunicateRingTopology()
{
	std::vector<Board> iterationBests(numThreads);
	
	// Collect all iteration-best solutions from each thread
	for (int i = 0; i < numThreads; i++)
	{
		iterationBests[i].Copy(threads[i]->GetIterationBest());
	}
	
	// Send to next thread in ring
	for (int i = 0; i < numThreads; i++)
	{
		int nextId = (i + 1) % numThreads;
		threads[nextId]->ReceiveIterationBest(iterationBests[i]);
	}
}

// ============================================================================
// COMMUNICATION TOPOLOGY 2: Random Network
// ============================================================================
void MultiThreadMultiColonyAntSystem::CommunicateRandomTopology(const std::vector<int>& matchArray)
{
	std::vector<Board> bestSols(numThreads);
	
	// Collect all best-so-far solutions from each thread
	for (int i = 0; i < numThreads; i++)
	{
		bestSols[i].Copy(threads[i]->GetBestSol());
	}
	
	// Send according to random matching
	for (int i = 0; i < numThreads; i++)
	{
		int threadId = matchArray[i];
		int fromPos = (i + numThreads - 1) % numThreads;
		int fromThreadId = matchArray[fromPos];
		
		threads[threadId]->ReceiveBestSol(bestSols[fromThreadId]);
	}
}



void MultiColonyThread::ReceiveIterationBest(const Board& solution)
{
	receivedIterationBest.Copy(solution);
	receivedIterationBestScore = solution.FixedCellCount();
}

void MultiColonyThread::ReceiveBestSol(const Board& solution)
{
	receivedBestSol.Copy(solution);
	receivedBestSolScore = solution.FixedCellCount();
}


float MultiColonyThread::PherAdd(int cellsFilled)
{
	return numCells / (float)(numCells - cellsFilled);
}

// ============================================================================
// UPDATE PHEROMONE WITH COMMUNICATION
// ============================================================================
// This method updates pheromone matrices using three sources:
// 1. Local iteration-best solution
// 2. Received iteration-best from ring topology
// 3. Received best-so-far from random topology
//
// IMPORTANT: Only MMAS colonies (type == 1) receive this update.
// ACS colonies (type == 0) are NOT updated with communication pheromone.
// ============================================================================

void MultiColonyThread::UpdatePheromoneWithCommunication()
{
	MultiColonyAntSystem* mcas = multiColonySystem;
	
	// Calculate pheromone deposit amounts for each source
	float pherValue1 = (iterationBestScore > 0) ? PherAdd(iterationBestScore) : 0.0f;
	float pherValue2 = (receivedIterationBestScore > 0) ? PherAdd(receivedIterationBestScore) : 0.0f;
	float pherValue3 = (receivedBestSolScore > 0) ? PherAdd(receivedBestSolScore) : 0.0f;
	
	// Find MMAS colonies (type == 1)
	std::vector<int> mmasIdx;
	for (int c = 0; c < mcas->numColonies; ++c)
	{
		if (mcas->colonies[c].type == 1)  // MMAS colony
			mmasIdx.push_back(c);
	}
	
	// Only update MMAS colonies
	for (int cidx : mmasIdx)
	{
		auto& colony = mcas->colonies[cidx];
		
		// Temporary arrays for tracking contributions per cell
		std::vector<float> contributions(numUnits, 0.0f);
		std::vector<bool> hasContribution(numUnits, false);
		
		// Process each cell
		for (int i = 0; i < numCells; i++)
		{
			// Clear temporary arrays for this cell
			std::fill(contributions.begin(), contributions.end(), 0.0f);
			std::fill(hasContribution.begin(), hasContribution.end(), false);
			
			// Collect contributions from all three sources
			
			// Source 1: Local iteration-best solution
			if (iterationBestScore > 0 && iterationBest.GetCell(i).Fixed())
			{
				int digit = iterationBest.GetCell(i).Index();
				contributions[digit] += pherValue1;
				hasContribution[digit] = true;
			}
			
			// Source 2: Iteration-best from ring topology neighbor
			if (receivedIterationBestScore > 0 && receivedIterationBest.GetCell(i).Fixed())
			{
				int digit = receivedIterationBest.GetCell(i).Index();
				contributions[digit] += pherValue2;
				hasContribution[digit] = true;
			}
			
			// Source 3: Best-so-far from random topology partner
			if (receivedBestSolScore > 0 && receivedBestSol.GetCell(i).Fixed())
			{
				int digit = receivedBestSol.GetCell(i).Index();
				contributions[digit] += pherValue3;
				hasContribution[digit] = true;
			}
			
			// Apply evaporation + reinforcement
			// Only for [cell, digit] pairs that received contributions
			for (int j = 0; j < numUnits; j++)
			{
				if (hasContribution[j])
				{
					// Evaporate old pheromone, add new contribution
					// Using rho (evaporation rate) and multiply contributions by rho
					float rho = mcas->GetRho(cidx);
					colony.pher[i][j] = colony.pher[i][j] * (1.0f - rho) + rho * contributions[j];
				}
			}
		}
		
		// Clamp pheromone values for MMAS (respect Max-Min bounds)
		mcas->ClampPheromone(colony);
	}
}

// ----------------------------------------------------------------------------
// UpdateMmasPheromoneLocal: Standard MMAS pheromone update when no communication
// Note: PublicPathRecommendation is already handled in RunIteration for low convergence cases
// This method only handles normal UpdatePheromone for high convergence speed cases
// ----------------------------------------------------------------------------
void MultiColonyThread::UpdateMmasPheromoneLocal()
{
	MultiColonyAntSystem* mcas = multiColonySystem;

	// Find the (single) MMAS colony (type == 1)
	int mmasIdx = -1;
	for (int c = 0; c < mcas->numColonies; ++c)
	{
		if (mcas->colonies[c].type == 1)
		{
			mmasIdx = c;
			break;
		}
	}

	if (mmasIdx < 0)
		return;  // No MMAS colony found

	// Check convergence speed: only update if high convergence
	// (Low convergence already got PublicPathRecommendation in RunIteration)
	float con_t = (currentIteration > 0 ? ((float)mcas->colonies[mmasIdx].lastImproveIter / (float)currentIteration) : 1.0f);
	
	if (con_t >= mcas->convThreshold)
	{
		// High convergence speed -> normal pheromone update
		mcas->UpdatePheromone(mmasIdx,
		                      mcas->colonies[mmasIdx],
		                      mcas->colonies[mmasIdx].bestSol,
		                      mcas->colonies[mmasIdx].bestPher);
	}


	// Note: Low convergence speed already got PublicPathRecommendation in RunIteration

}


int MultiThreadMultiColonyAntSystem::CalculateInterval(int iteration)
{
	if (iteration < commThreshold)
		return commEarlyInterval;
	else
		return commLateInterval;
}

std::vector<int> MultiThreadMultiColonyAntSystem::GenerateMatchArray()
{
	std::vector<int> matchArray(numThreads);
	std::iota(matchArray.begin(), matchArray.end(), 0);
	std::shuffle(matchArray.begin(), matchArray.end(), masterRandGen);
	return matchArray;
}


// ============================================================================
// HELPER METHODS FOR MODULAR WORKER THREAD
// ============================================================================

bool MultiThreadMultiColonyAntSystem::CheckTimeout()
{
	if (solutionTimer.Elapsed() >= maxTime)
	{
		stopFlag.store(true);
		if (numThreads > 1)
			commCV.notify_all();  // Only needed for multiple threads
		return true;
	}
	return false;
}

void MultiThreadMultiColonyAntSystem::ReportProgress(int threadId, int iteration, MultiColonyThread* thread, const Board& puzzle)
{
	if (threadId == 0)
	{
		std::lock_guard<std::mutex> lock(commMutex);
		
		// Find global best across all threads
		int globalBest = 0;
		int bestIdx = 0;
		for (int i = 0; i < numThreads; i++)
		{
			int score = threads[i]->GetBestSolScore();
			if (score > globalBest)
			{
				globalBest = score;
				bestIdx = i;
			}
		}

		{
			const Board& _brd = threads[bestIdx]->GetBestSol();
			int _nu = puzzle.GetNumUnits();
			int _ord = (_nu <= 9) ? 3 : (_nu <= 16) ? 4 : 5;
			std::string _flat;
			_flat.reserve(puzzle.CellCount());
			for (int _i = 0; _i < _brd.CellCount(); _i++) {
				const ValueSet& _c = _brd.GetCell(_i);
				if (_c.Fixed()) {
					int _idx = _c.Index();
					if (_nu == 9) _flat += (char)('1' + _idx);
					else if (_nu == 16) _flat += (_idx < 10) ? (char)('0' + _idx) : (char)('a' + _idx - 10);
					else _flat += (char)('a' + _idx);
				} else {
					_flat += '.';
				}
			}
			std::cout << "BEST_GRID " << _ord << " " << _flat << std::endl;
		}

		std::cerr << "Progress: iteration " << iteration << " (Global best-so-far: " 
		          << globalBest << "/" << puzzle.CellCount() << ")" << std::endl;
	}
}

bool MultiThreadMultiColonyAntSystem::CheckSolutionFound(MultiColonyThread* thread)
{
	if (thread->GetBestSolScore() == thread->GetBestSol().CellCount())
	{
		stopFlag.store(true);
		if (numThreads > 1)
			commCV.notify_all();  // Only needed for multiple threads
		return true;
	}
	return false;
}



void MultiThreadMultiColonyAntSystem::ExecuteWorkerThreadWait(std::unique_lock<std::mutex>& lock)
{
	auto predicate = [this]() { return barrier.load() == 0 || stopFlag.load(); };
	
	while (!predicate())
	{
		commCV.wait_for(lock, std::chrono::milliseconds(100), predicate);
		
		if (solutionTimer.Elapsed() >= maxTime && !stopFlag.load())
		{
			stopFlag.store(true);
			barrier.store(0);
			commCV.notify_all();
		}
	}
}




void MultiThreadMultiColonyAntSystem::PrintThreadDetails()
{
	std::cout << "Thread Details:" << std::endl;
	for (int i = 0; i < numThreads; i++)
	{
		std::cout << "  Thread " << i << ": "
		          << "iter=" << threads[i]->GetCurrentIteration() << ", "
		          << "iter-best=" << threads[i]->GetIterationBestScore() << "/" << threads[i]->GetIterationBest().CellCount() << ", "
		          << "best-so-far=" << threads[i]->GetBestSolScore() << "/" << threads[i]->GetBestSol().CellCount() << ", "
		          << "CPTime=" << std::fixed << std::setprecision(2) << threads[i]->GetCPTime() << "s, "
		          << "CoopGame=" << threads[i]->GetCooperativeGameTime() << "s, "
		          << "Fusion=" << threads[i]->GetPheromoneFusionTime() << "s, "
		          << "PubPath=" << threads[i]->GetPublicPathRecommendationTime() << "s, "
		          << "Comm=" << threads[i]->GetCommunicationTime() << "s"
		          << std::endl;
	}
}
