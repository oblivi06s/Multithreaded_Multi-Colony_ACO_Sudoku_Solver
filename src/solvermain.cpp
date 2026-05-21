/*******************************************************************************
 * SUDOKU SOLVER - Main Entry Point
 * 
 * This is the main executable for the Sudoku solver. It provides a
 * command-line interface for running different solving algorithms with
 * comprehensive timing instrumentation.
 * 
 * Execution Flow:
 * 1. Parse command-line arguments
 * 2. Load puzzle from file or command line
 * 3. Initialize board (triggers initial constraint propagation)
 * 4. Select and configure algorithm
 * 5. Run solver with timing instrumentation
 * 6. Extract algorithm-specific statistics
 * 7. Validate solution
 * 8. Output results (compact, verbose, or JSON format)
 * 
 * Supported Algorithms:
 * - Algorithm 0: Single-threaded Ant Colony System (ACS)
 * - Algorithm 1: Backtracking search
 * - Algorithm 2: Parallel ACS with multiple sub-colonies (multithreaded)
 * - Algorithm 3: Multi-Colony Ant System (single-threaded)
 * - Algorithm 4: MultiThread Multi-Colony Ant System (parallel multi-colony)
 * 
 * Output Formats:
 * - Compact: For batch processing (success flag + time)
 * - Verbose: Detailed solution, timing breakdown, and statistics
 * - JSON: Machine-readable format for integration
 * 
 * Timing Components Tracked:
 * - Initial constraint propagation time
 * - Ant constraint propagation time (during solution construction)
 * - Ant decision-making time (greedy vs weighted selection)
 * - Cooperative game theory time (algorithms 3 & 4)
 * - Pheromone fusion time (algorithms 3 & 4)
 * - Public path recommendation time (algorithms 3 & 4)
 * - Thread communication time (algorithms 2 & 4)
 * 
 * @see TIMING_DOCUMENTATION.md for detailed timing explanations
 * @see RUN_GENERAL_GUIDE.md for batch execution guide
 ******************************************************************************/

#include "sudokuantsystem.h"
#include "parallelsudokuantsystem.h"
#include "multicolonyantsystem.h"
#include "multithreadmulticolonyantsystem.h"
#include "backtracksearch.h"
#include "sudokusolver.h"
#include "board.h"
#include "arguments.h"
#include "constraintpropagation.h"
#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include <sstream>
#include <memory>
using namespace std;

// ============================================================================
// CONSTANTS
// ============================================================================

namespace PuzzleDefaults
{
	constexpr int CELL_COUNT_9X9 = 81;
	constexpr int CELL_COUNT_16X16 = 256;
	constexpr int CELL_COUNT_25X25 = 625;
	
	constexpr int TIMEOUT_9X9 = 5;
	constexpr int TIMEOUT_16X16 = 20;
	constexpr int TIMEOUT_25X25 = 120;
	constexpr int TIMEOUT_DEFAULT = 120;
	
	constexpr int ORDER_9X9 = 3;
	constexpr int ORDER_16X16 = 4;
	constexpr int DECIMAL_THRESHOLD = 11;
}

// ============================================================================
// HELPER STRUCTURES
// ============================================================================

/**
 * Timing information extracted from the winning thread for parallel algorithms
 */
struct WinningThreadTimings
{
	int threadId = -1;
	float cpTime = 0.0f;
	float communicationTime = 0.0f;
	float cooperativeGameTime = 0.0f;
	float pheromoneFusionTime = 0.0f;
	float publicPathRecommendationTime = 0.0f;
	float totalSystemCommunicationTime = 0.0f;
	float totalSystemIdleTime = 0.0f;      // NEW: Sum of all threads
	int totalSystemBarrierEntries = 0;     // NEW: Sum of all threads
	float totalSystemIdleStdDev = 0.0f;        // NEW
};

/**
 * General solver statistics (iterations, communication status)
 */
struct SolverStatistics
{
	int iterations = 0;
	bool communicationOccurred = false;
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Escape special characters in a string for JSON output
 */
static string jsonEscape(const string &input)
{
	ostringstream out;
	for (char c : input)
	{
		switch (c)
		{
		case '\\': out << "\\\\"; break;
		case '"': out << "\\\""; break;
		case '\n': out << "\\n"; break;
		case '\r': out << "\\r"; break;
		case '\t': out << "\\t"; break;
		default: out << c; break;
		}
	}
	return out.str();
}

/**
 * Print a timing line with consistent formatting
 * @param label The label for the timing component
 * @param timeValue The time value in seconds
 * @param percentage The percentage of total time
 */
static void printTimingLine(const string& label, float timeValue, float percentage)
{
	cout << fixed << setprecision(6);
	cout << label << ": " << timeValue << " s (" 
	     << setprecision(2) << percentage << "%)" << endl;
}

/**
 * Extract timing information from the winning thread for Parallel ACS (Algorithm 2)
 * @param solver Pointer to the parallel solver
 * @return Timing information from the sub-colony with the best solution
 */
static WinningThreadTimings extractParallelACSTimings(ParallelSudokuAntSystem* solver)
{
	WinningThreadTimings timings;
	const auto& subColonies = solver->GetSubColonies();
	int bestScore = 0;
	
	for (size_t i = 0; i < subColonies.size(); i++)
	{
		int score = subColonies[i]->GetBestSolScore();
		if (score > bestScore)
		{
			bestScore = score;
			timings.threadId = static_cast<int>(i);
			timings.cpTime = subColonies[i]->GetCPTime();
			timings.communicationTime = subColonies[i]->GetCommunicationTime();
		}
	}
	return timings;
}

/**
 * Extract timing information from the winning thread for Multi-Thread Multi-Colony (Algorithm 4)
 * @param solver Pointer to the multi-thread multi-colony solver
 * @return Timing information from the thread with the best solution
 */
static WinningThreadTimings extractMultiThreadMultiColonyTimings(MultiThreadMultiColonyAntSystem* solver)
{
	WinningThreadTimings timings;
	const auto& threads = solver->GetThreads();
	int bestScore = 0;

	int combinedCount = 0;
	float combinedMean = 0.0f;
	float combinedM2 = 0.0f;
	
	for (size_t i = 0; i < threads.size(); i++)
	{
		// Accumulate the total for every thread
		timings.totalSystemIdleTime += threads[i]->GetIdleTime();
		timings.totalSystemBarrierEntries += threads[i]->GetBarrierEntries();
		timings.totalSystemCommunicationTime += threads[i]->GetCommunicationTime();

		int nA = combinedCount;
		int nB = threads[i]->GetBarrierEntries();
		if (nB > 0)
		{
			float delta = threads[i]->GetIdleMean() - combinedMean;
			combinedCount = nA + nB;
			combinedMean += delta * nB / combinedCount;
			combinedM2 += threads[i]->GetIdleM2() + delta * delta * nA * nB / combinedCount;
		}

		int score = threads[i]->GetBestSolScore();
		if (score > bestScore)
		{
			bestScore = score;
			timings.threadId = static_cast<int>(i);
			timings.cpTime = threads[i]->GetCPTime();
			timings.communicationTime = threads[i]->GetCommunicationTime();
			timings.cooperativeGameTime = threads[i]->GetCooperativeGameTime();
			timings.pheromoneFusionTime = threads[i]->GetPheromoneFusionTime();
			timings.publicPathRecommendationTime = threads[i]->GetPublicPathRecommendationTime();
		}
	}
	// Calculate the final Standard Deviation!
	if (combinedCount > 0) {
		timings.totalSystemIdleStdDev = std::sqrt(combinedM2 / combinedCount);
	}
	return timings;
}

/**
 * Extract solver statistics (iterations, communication status) based on algorithm
 * @param solver Pointer to the solver
 * @param algorithmId The algorithm identifier (0-4)
 * @return Statistics extracted from the solver
 */
static SolverStatistics extractSolverStatistics(SudokuSolver* solver, int algorithmId)
{
	SolverStatistics stats;
	
	switch (algorithmId)
	{
		case 0:
			if (auto* antSolver = dynamic_cast<SudokuAntSystem*>(solver))
				stats.iterations = antSolver->GetIterationsCompleted();
			break;
		
		case 2:
			if (auto* parallelSolver = dynamic_cast<ParallelSudokuAntSystem*>(solver))
			{
				stats.iterations = parallelSolver->GetIterationsCompleted();
				stats.communicationOccurred = parallelSolver->GetCommunicationOccurred();
			}
			break;
		
		case 3:
			if (auto* multiColonySolver = dynamic_cast<MultiColonyAntSystem*>(solver))
				stats.iterations = multiColonySolver->GetIterationCount();
			break;
		
		case 4:
			if (auto* multiThreadSolver = dynamic_cast<MultiThreadMultiColonyAntSystem*>(solver))
			{
				stats.iterations = multiThreadSolver->GetIterationsCompleted();
				stats.communicationOccurred = multiThreadSolver->GetCommunicationOccurred();
			}
			break;
	}
	
	return stats;
}

// ============================================================================
// SECTION 1: PUZZLE FILE READER
// ============================================================================

/*******************************************************************************
 * readPuzzleFile - Read a Sudoku puzzle from a text file
 * 
 * File format:
 *   Line 1: order (e.g., 3 for 9x9, 4 for 16x16, 5 for 25x25)
 *   Line 2: dummy value (ignored)
 *   Remaining: cell values (one per line)
 *     -1 represents empty cell (.)
 *     1-9 (for 9x9), 1-16 (for 16x16), etc. represent fixed values
 * 
 * Character encoding:
 *   9x9 (order=3): '1'-'9' for values 1-9
 *   16x16 (order=4): '0'-'9' for values 1-10, 'a'-'f' for values 11-16
 *   25x25+ (order>=5): 'a'-'y' for values 1-25
 * 
 * @param fileName Path to the puzzle file
 * @return Puzzle string representation (e.g., "4.5...2.1...") or empty string on error
 ******************************************************************************/
string readPuzzleFile(const string& fileName)
{
	ifstream inFile(fileName);
	if (!inFile.is_open())
	{
		cerr << "Error: Could not open file: " << fileName << endl;
		return string();
	}
	
	int order, dummyValue;
	inFile >> order;
	int numCells = order * order * order * order;
	inFile >> dummyValue;  // Read and ignore dummy value
	
	char* puzString = new char[numCells + 1];
	
	for (int i = 0; i < numCells; i++)
	{
		int val;
		inFile >> val;
		
		if (val == -1)
		{
			puzString[i] = '.';
		}
		else if (order == PuzzleDefaults::ORDER_9X9)
		{
			// 9x9: values 1-9 encoded as '1'-'9'
			puzString[i] = '1' + (val - 1);
		}
		else if (order == PuzzleDefaults::ORDER_16X16)
		{
			// 16x16: values 1-10 as '0'-'9', values 11-16 as 'a'-'f'
			if (val < PuzzleDefaults::DECIMAL_THRESHOLD)
				puzString[i] = '0' + val - 1;
			else
				puzString[i] = 'a' + val - 11;
		}
		else
		{
			// 25x25+: all values encoded as 'a'-'y'
			puzString[i] = 'a' + val - 1;
		}
	}
	
	puzString[numCells] = '\0';
	inFile.close();
	
	string retVal = string(puzString);
	delete[] puzString;
	return retVal;
}

// ============================================================================
// SECTION 2: MAIN FUNCTION
// ============================================================================

/*******************************************************************************
 * main - Entry point for the Sudoku solver
 * 
 * This function orchestrates the entire solving process:
 * 1. Parse command-line arguments
 * 2. Load puzzle
 * 3. Initialize board (applies constraint propagation)
 * 4. Configure and run selected algorithm
 * 5. Validate and output results
 ******************************************************************************/
int main(int argc, char* argv[])
{
	// ========================================================================
	// SECTION 2.1: COMMAND-LINE ARGUMENT PARSING
	// ========================================================================
	
	Arguments args(argc, argv);
	string puzzleString;
	
	// Option 1: Generate blank puzzle of specified order
	if (args.GetArg("blank", 0) && args.GetArg("order", 0))
	{
		int order = args.GetArg("order", 0);
		if (order != 0)
			puzzleString = string(order * order * order * order, '.');
	}
	// Option 2: Read puzzle from command line or file
	else 
	{
		// Try reading from command-line argument
		puzzleString = args.GetArg(string("puzzle"), string());
		if (puzzleString.length() == 0)
		{
			// Try reading from file
			string fileName = args.GetArg(string("file"), string());
			puzzleString = readPuzzleFile(fileName);
		}
		if (puzzleString.length() == 0)
		{
			cerr << "Error: No puzzle specified. Use --file or --puzzle argument." << endl;
			return 1;
		}
	}
	
	// Reset constraint propagation timing statistics before starting the solve.
	// This ensures we only measure CP time for this specific puzzle instance.
	ResetCPTiming();
	
	// Initialize board from puzzle string. This triggers initial constraint
	// propagation which reduces the search space before the solver runs.
	// Initial CP time is tracked separately from ant CP time.
	Board board(puzzleString);

	// ========================================================================
	// Parse algorithm configuration parameters
	// ========================================================================
	
	// Core algorithm selection and timeout
	const int algorithmId = args.GetArg("alg", 0);  // 0-4, see algorithm list in header
	int timeoutSeconds = args.GetArg("timeout", -1);  // -1 = auto-select based on puzzle size
	
	// Ant Colony Optimization parameters (algorithms 0, 2, 3, 4)
	const int antCount = args.GetArg("ants", 3);  // Number of ants per colony
	const float q0 = args.GetArg("q0", 0.9f);      // Exploitation vs exploration (0.0-1.0)
	const float rho = args.GetArg("rho", 0.9f);    // Pheromone persistence (0.0-1.0)
	const float evaporationRate = args.GetArg("evap", 0.005f);  // Best Value Pheromone evaporation rate
	const float xi = args.GetArg("xi", 0.1f); //Local Pheromone Update 
	
	// Parallel algorithm parameters (algorithms 2, 4)
	const int threadCount = args.GetArg("threads", 3);  // Number of parallel threads
	
	// Communication interval parameters (algorithms 2, 4)
	const int commEarlyInterval = args.GetArg("comm-early-interval", 100);  // Communication interval for early iterations
	const int commLateInterval = args.GetArg("comm-late-interval", 10);     // Communication interval for late iterations
	const int commThreshold = args.GetArg("comm-threshold", 200);           // Iteration threshold for switching intervals
	
	// Multi-colony algorithm parameters (algorithms 4)
	const int acsColonyCount = args.GetArg("numacs", 2);  // Number of ACS colonies
	const int totalColonyCount = args.GetArg("numcolonies", acsColonyCount + 1);  // Total colonies (ACS + MMAS)
	const float convergenceThreshold = args.GetArg("convthreshold", 0.8f);  // Threshold for public path recommendation
	const float entropyThreshold = args.GetArg("entropythreshold", 1.47f);  // Threshold for pheromone fusion
	// Algorithm 4: 0 = no inter-thread communication (ring/random off), 1 = both topologies when comm fires (default)
	const int threadsCommFlag = args.GetArg("threads-comm", 1);
	const bool threadCommunicationEnabled = (threadsCommFlag != 0);
	
	// Output control flags
	const bool blank = args.GetArg("blank", false);         // Generate blank puzzle
	const bool verbose = args.GetArg("verbose", 0);         // Detailed output
	const bool showInitial = args.GetArg("showinitial", 0); // Show board after initial CP
	const bool jsonOutput = args.GetArg("json", 0);         // JSON output format
	bool success;  // Will be set by solver

	// Auto-select timeout based on puzzle size if not specified
	// These defaults are based on empirical testing:
	// - 9x9 puzzles typically solve in < 5 seconds
	// - 16x16 puzzles typically solve in < 20 seconds
	// - 25x25+ puzzles may need 2+ minutes
	if (timeoutSeconds <= 0)
	{
		int cellCount = board.CellCount();
		if (cellCount == PuzzleDefaults::CELL_COUNT_9X9)
			timeoutSeconds = PuzzleDefaults::TIMEOUT_9X9;
		else if (cellCount == PuzzleDefaults::CELL_COUNT_16X16)
			timeoutSeconds = PuzzleDefaults::TIMEOUT_16X16;
		else if (cellCount == PuzzleDefaults::CELL_COUNT_25X25)
			timeoutSeconds = PuzzleDefaults::TIMEOUT_25X25;
		else
			timeoutSeconds = PuzzleDefaults::TIMEOUT_DEFAULT;
	}

	float solutionTime;
	Board solutionBoard;
	unique_ptr<SudokuSolver> solver;
	
	// ========================================================================
	// SECTION 2.2: ALGORITHM SELECTION & CONFIGURATION
	// ========================================================================
	
	// Calculate initial pheromone value (tau0) based on board size
	const float initialPheromone = 1.0f / board.CellCount();
	
	// Create solver instance based on selected algorithm
	if (algorithmId == 0)
	{
		// Algorithm 0: Single-threaded Ant Colony System
		solver = make_unique<SudokuAntSystem>(antCount, q0, rho, initialPheromone, evaporationRate);
	}
	else if (algorithmId == 1)
	{
		// Algorithm 1: Backtracking search
		solver = make_unique<BacktrackSearch>();
	}
	else if (algorithmId == 2)
	{
		// Algorithm 2: Parallel ACS with multiple sub-colonies
		solver = make_unique<ParallelSudokuAntSystem>(threadCount, antCount, q0, rho, initialPheromone, evaporationRate,
		                                               commEarlyInterval, commLateInterval, commThreshold);
	}
	else if (algorithmId == 3)
	{
		// Algorithm 3: Multi-Colony Ant System (single-threaded)
		solver = make_unique<MultiColonyAntSystem>(antCount, q0, rho, initialPheromone, evaporationRate, 
		                                           totalColonyCount, acsColonyCount, convergenceThreshold, entropyThreshold, xi);
	}
	else if (algorithmId == 4)
	{
		// Algorithm 4: Multi-Thread Multi-Colony Ant System
		solver = make_unique<MultiThreadMultiColonyAntSystem>(threadCount, antCount, q0, rho, initialPheromone, evaporationRate,
		                                                       totalColonyCount, acsColonyCount, convergenceThreshold, entropyThreshold, xi, 
		                                                       commEarlyInterval, commLateInterval, commThreshold,
		                                                       threadCommunicationEnabled);
	}
	else
	{
		cerr << "Error: Invalid algorithm: " << algorithmId 
		     << ". Use 0 (single-thread ACS), 1 (backtracking), 2 (parallel ACS), "
		     << "3 (multi-colony), or 4 (multi-thread multi-colony)." << endl;
		return 1;
	}

	// Optionally show the puzzle after initial constraint propagation
	if (showInitial)
	{
		cout << "Initial constrained grid:" << endl;
		cout << board.AsString(false, true) << endl;
	}
	
	// ========================================================================
	// SECTION 2.3: RUN SOLVER
	// ========================================================================
	
	success = solver->Solve(board, static_cast<float>(timeoutSeconds));
	solutionBoard = solver->GetSolution();
	solutionTime = solver->GetSolutionTime();
	

	// ========================================================================
	// SECTION 2.4: SOLUTION VALIDATION & OUTPUT
	// ========================================================================
	
	// Validate the solution
	string errorMessage;
	if (success && !board.CheckSolution(solutionBoard))
	{
		errorMessage = "Solution not valid";
		if (!jsonOutput)
		{
			cout << "Error: Solution not valid for file: " << args.GetArg("file", string()) 
			     << ", algorithm: " << algorithmId << endl;
			cout << "Number of fixed cells: " << solutionBoard.FixedCellCount() << endl;
			cout << solutionBoard.AsString(true) << endl;
		}
		success = false;
	}
	
	// ========================================================================
	// Extract Constraint Propagation Timing Statistics
	// ========================================================================
	// Initial CP time: time spent in constraint propagation during board initialization
	float initialCPTime = GetInitialCPTime();
	solutionTime += initialCPTime;  // Include initial CP in total time
	
	float antCPTime = GetAntCPTime();
	int cpCallCount = GetCPCallCount();
	
	// ========================================================================
	// Extract Timing from Winning Thread (for multi-threaded algorithms)
	// ========================================================================
	// For parallel algorithms (2 and 4), each thread maintains its own timing
	// statistics. We extract timing from the "winning thread" (the thread that
	// found the best solution) to get representative timing values.
	// For single-threaded algorithms, we use the global timing values.
	// ========================================================================
	
	WinningThreadTimings timings;
	timings.cpTime = antCPTime;  // Default for single-threaded
	
	switch (algorithmId)
	{
		case 0:  // Single-threaded ACS
			// No additional timings needed (will calculate ACS time by subtraction)
			break;
		
		case 2:  // Parallel ACS
			if (auto* parallelSolver = dynamic_cast<ParallelSudokuAntSystem*>(solver.get()))
				timings = extractParallelACSTimings(parallelSolver);
			break;
		
		case 3:  // Multi-Colony (single-threaded)
			if (auto* multiColonySolver = dynamic_cast<MultiColonyAntSystem*>(solver.get()))
			{
				timings.cooperativeGameTime = multiColonySolver->GetCooperativeGameTime();
				timings.pheromoneFusionTime = multiColonySolver->GetPheromoneFusionTime();
				timings.publicPathRecommendationTime = multiColonySolver->GetPublicPathRecommendationTime();
			}
			break;
		
		case 4:  // Multi-Thread Multi-Colony
			if (auto* multiThreadSolver = dynamic_cast<MultiThreadMultiColonyAntSystem*>(solver.get()))
				timings = extractMultiThreadMultiColonyTimings(multiThreadSolver);
			break;
	}
	
	float totalCPTime = initialCPTime + timings.cpTime;
	
	// ========================================================================
	// Extract Solver Statistics (iterations, communication status)
	// ========================================================================
	SolverStatistics stats = extractSolverStatistics(solver.get(), algorithmId);
	
	// ========================================================================
	// JSON Output Format
	// ========================================================================
	// Machine-readable format for integration with other tools and scripts.
	// Includes all core metrics: success, time, iterations, CP statistics.
	// Note: JSON output excludes some verbose timing details for simplicity.
	if (jsonOutput)
	{
		cout << fixed << setprecision(6);
		cout << "{";
		cout << "\"success\":" << (success ? "true" : "false") << ",";
		cout << "\"algorithm\":" << algorithmId << ",";
		cout << "\"time\":" << solutionTime << ",";
		cout << "\"iterations\":" << stats.iterations << ",";
		cout << "\"communication\":" << (stats.communicationOccurred ? "true" : "false") << ",";
		cout << "\"solution\":\"" << jsonEscape(solutionBoard.AsString(true)) << "\",";
		cout << "\"error\":\"" << jsonEscape(errorMessage) << "\",";
		cout << "\"cp_initial\":" << initialCPTime << ",";
		cout << "\"cp_ant\":" << timings.cpTime << ",";
		cout << "\"cooperative_game\":" << timings.cooperativeGameTime << ",";
		cout << "\"pheromone_fusion\":" << timings.pheromoneFusionTime << ",";
		cout << "\"public_path\":" << timings.publicPathRecommendationTime << ",";
		cout << "\"communication\":" << timings.communicationTime << ",";
		cout << "\"idle_time_total\":" << timings.totalSystemIdleTime << ",";       // NEW
		cout << "\"barrier_entries_total\":" << timings.totalSystemBarrierEntries << ","; // NEW
		cout << "\"cp_calls\":" << cpCallCount << ",";
		cout << "\"cp_total\":" << totalCPTime;
		cout << "}" << endl;
		return 0;
	}

	// ========================================================================
	// Compact Output Format (for batch processing)
	// ========================================================================
	if (!verbose)
	{
		cout << !success << endl << solutionTime << endl;
	}
	
	// ========================================================================
	// Output CP timing for script parsing
	// ========================================================================
	// These lines are always output (both verbose and non-verbose modes) to
	// allow the run_general.py script to parse timing statistics.
	// Format: "cp_initial: <value>" for easy regex matching.
	cout << "cp_initial: " << initialCPTime << endl;
	cout << "cp_ant: " << timings.cpTime << endl;
	cout << "cp_calls: " << cpCallCount << endl;
	
	// ========================================================================
	// Verbose Output Format (for interactive use)
	// ========================================================================
	if (verbose)
	{
		if (!success)
		{
			cout << "Failed to solve in " << solutionTime << " seconds" << endl;
		}
		else
		{
			cout << "Solution:" << endl;
			cout << solutionBoard.AsString(true) << endl;
			cout << "Solved in " << solutionTime << " seconds" << endl;
		}
		
		// Show iterations
		cout << "Iterations: " << stats.iterations << endl;
		
		// Show communication status and winning thread for multi-threaded algorithms
		if (algorithmId == 2 || algorithmId == 4)
		{
			cout << "Communication: " << (stats.communicationOccurred ? "yes" : "no") << endl;
			if (timings.threadId >= 0)
				cout << "Winning Thread: " << (timings.threadId + 1) << endl;

			// NEW: Print the system-wide normalized idle time
			if (timings.totalSystemBarrierEntries > 0)
			{
				float avgIdleMs = (timings.totalSystemIdleTime / timings.totalSystemBarrierEntries) * 1000.0f;
				float stdDevMs = timings.totalSystemIdleStdDev * 1000.0f; // NEW

				cout << "\n\n- Total System Idle Time: " << fixed << setprecision(6) 
				     << timings.totalSystemIdleTime << " s (" << setprecision(2) 
				     << avgIdleMs << " ms per barrier entry)" << endl;
				
				cout << "Standard Dev (ms per barrier entry): "<< stdDevMs <<endl;
			}

			
		}
		
		// ====================================================================
		// TIMING BREAKDOWN SECTION
		// ====================================================================
		// Display detailed timing breakdown showing how total solve time is
		// distributed across different components. Each component shows both
		// absolute time (seconds) and percentage of total solve time.
		// 
		// All algorithms show:
		//   - Initial CP Time
		//   - Ant CP Time
		//   - Main Algorithm Time (ACS or DCM-ACO) - calculated by subtraction
		//   - Communication Time (algorithms 2 & 4 only)
		// 
		// For DCM-ACO (algorithms 3 & 4), sub-components are shown for info:
		//   - Cooperative Game Theory (part of DCM-ACO time)
		//   - Pheromone Fusion (part of DCM-ACO time)
		//   - Public Path Recommendation (part of DCM-ACO time)
		// 
		// Main algorithm time = Total - Initial CP - Ant CP - Communication
		// This ensures exactly 100% coverage.
		// ====================================================================
		cout << "\n==Time Breakdown==" << endl;
		
		// Calculate percentages (avoid division by zero)
		float totalTime = (solutionTime > 0.0f) ? solutionTime : 1.0f;
		float initialCPPercent = (initialCPTime / totalTime) * 100.0f;
		float antCPPercent = (timings.cpTime / totalTime) * 100.0f;
		float coopGamePercent = (timings.cooperativeGameTime / totalTime) * 100.0f;
		float fusionPercent = (timings.pheromoneFusionTime / totalTime) * 100.0f;
		float publicPathPercent = (timings.publicPathRecommendationTime / totalTime) * 100.0f;
		float commPercent = (timings.communicationTime / totalTime) * 100.0f;
		
		// Calculate main algorithm time by subtraction
		// For algorithms 0 & 2: ACS Time = Total - Initial CP - Ant CP [- Communication]
		// For algorithms 3 & 4: DCM-ACO Time = Total - Initial CP - Ant CP [- Communication]
		// Note: Coop Game, Fusion, Public Path are INSIDE DCM-ACO time, not subtracted
		float trackedTime = initialCPTime + timings.cpTime;
		if (algorithmId == 2 || algorithmId == 4)
		{
			trackedTime += timings.communicationTime;
		}
		float mainAlgorithmTime = solutionTime - trackedTime;
		float mainAlgorithmPercent = (mainAlgorithmTime / totalTime) * 100.0f;
		
		// Display timing components
		printTimingLine("Initial CP Time", initialCPTime, initialCPPercent);
		printTimingLine("Ant CP Time", timings.cpTime, antCPPercent);
		
		// Display main algorithm time (ACS or DCM-ACO)
		if (algorithmId == 0 || algorithmId == 2)
		{
			printTimingLine("ACS Time", mainAlgorithmTime, mainAlgorithmPercent);
		}
		else if (algorithmId == 3 || algorithmId == 4)
		{
			printTimingLine("DCM-ACO Time", mainAlgorithmTime, mainAlgorithmPercent);
			// Show sub-components with simple indentation
			cout << "  - ";
			printTimingLine("Cooperative Game Time", timings.cooperativeGameTime, coopGamePercent);
			cout << "  - ";
			printTimingLine("Pheromone Fusion Time", timings.pheromoneFusionTime, fusionPercent);
			cout << "  - ";
			printTimingLine("Public Path Recommendation Time", timings.publicPathRecommendationTime, publicPathPercent);
		}
		
		// Show communication time for multi-threaded algorithms
		if (algorithmId == 2 || algorithmId == 4)
		{
			printTimingLine("Communication Time", timings.communicationTime, commPercent);
		}
		
		cout << fixed << setprecision(6);
		cout << "Total Solve Time: " << solutionTime << " s" << endl;
	}
	
	return 0;
}
