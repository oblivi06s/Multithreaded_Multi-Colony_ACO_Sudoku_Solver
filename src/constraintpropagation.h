#pragma once

#include <atomic>
#include <mutex>

// Forward declaration
class Timer;

/*******************************************************************************
 * CONSTRAINT PROPAGATION - Sudoku Logic Rules
 * 
 * This module implements the constraint propagation algorithms used to reduce
 * the search space before applying ACO. Two classic Sudoku solving rules are
 * implemented:
 * 
 * - Rule 1 (Elimination): Removes values that are already fixed in the same
 *   row, column, or box. If only one value remains, the cell becomes fixed.
 * 
 * - Rule 2 (Hidden Single): Finds values that can only appear in one place
 *   within a unit (row, column, or box). If such a value exists, the cell
 *   is set to that value.
 * 
 * These rules are applied iteratively during puzzle initialization to maximize
 * the number of cells that can be logically deduced before ACO begins.
 * 
 * TIMING INSTRUMENTATION:
 * This module tracks time spent in constraint propagation for cost-benefit
 * analysis.
 ******************************************************************************/

#include "board.h"
#include "valueset.h"

// ============================================================================
// TIMING INSTRUMENTATION FOR COST-BENEFIT ANALYSIS
// ============================================================================

/*******************************************************************************
 * CP Timing Statistics
 * 
 * These functions allow tracking time spent in constraint propagation:
 * - InitialCPTime: Time spent during initial board construction
 * - AntCPTime: Time spent during ant construction (every ant step)
 ******************************************************************************/

// Reset all CP timing statistics
void ResetCPTiming();

// Get timing statistics
float GetInitialCPTime();
float GetAntCPTime();
int GetCPCallCount();

// Internal: Mark initial CP phase (called from Board constructor)
void BeginInitialCP();
void EndInitialCP();

// ============================================================================
// CP NAMESPACE - Per-thread CP time tracking for multi-threaded algorithms
// ============================================================================

namespace CP
{
	// Register a thread's CP time accumulator pointer (atomic for thread-safe accumulation)
	// For multi-threaded algorithms (2, 4), pass std::atomic<float>* 
	// For single-threaded algorithms (0, 3), this is not used (pass nullptr or don't call)
	void RegisterThreadCPTime(std::atomic<float>* cpTimePtr);
	
	// Unregister the current thread's CP time accumulator
	void UnregisterThreadCPTime();
	
	// Add elapsed time to the registered thread's CP time accumulator
	// If no thread is registered, this does nothing
	void AddTime(float elapsed);
	
	// Register a thread's main algorithm timer pointer (for pause/resume)
	// When CP work is being done, the main algorithm timer will be paused
	void RegisterMainAlgorithmTimer(Timer* timerPtr);
	
	// Unregister the current thread's main algorithm timer
	void UnregisterMainAlgorithmTimer();
	
	// Get the registered main algorithm timer (for manual pause/resume)
	Timer* GetMainAlgorithmTimer();
}

/*******************************************************************************
 * Rule1_Elimination
 * 
 * Applies the elimination rule to a single cell: removes all values that are
 * already fixed in the same row, column, or box.
 * 
 * If after elimination only one value remains, the cell is automatically set
 * to that value (which triggers further propagation).
 * 
 * Parameters:
 *   board      - The Sudoku board to operate on
 *   cellIndex  - The index of the cell to apply elimination to
 * 
 * Returns: true if the cell was fixed as a result of this rule
 ******************************************************************************/
bool Rule1_Elimination(Board& board, int cellIndex);

/*******************************************************************************
 * Rule2_HiddenSingle
 * 
 * Applies the hidden single rule to a single cell: checks if any of the cell's
 * possible values can only appear in this cell within its row, column, or box.
 * 
 * If such a value is found, the cell is set to that value (which triggers
 * further propagation).
 * 
 * Parameters:
 *   board      - The Sudoku board to operate on
 *   cellIndex  - The index of the cell to check for hidden singles
 * 
 * Returns: true if the cell was fixed as a result of this rule
 ******************************************************************************/
bool Rule2_HiddenSingle(Board& board, int cellIndex);

/*******************************************************************************
 * PropagateConstraints
 * 
 * Main entry point for constraint propagation on a single cell. This function
 * applies both Rule1 and Rule2 to the specified cell.
 * 
 * This function is called internally during SetCellAndPropagate to cascade
 * constraints when a cell is fixed.
 * 
 * Parameters:
 *   board      - The Sudoku board to operate on
 *   cellIndex  - The index of the cell to propagate constraints from
 ******************************************************************************/
void PropagateConstraints(Board& board, int cellIndex);

/*******************************************************************************
 * SetCellAndPropagate
 * 
 * Sets a cell to a specific value and propagates the constraints to all
 * affected cells (cells in the same row, column, and box).
 * 
 * This is the main function used during puzzle initialization to set cells
 * that are given in the input puzzle.
 * 
 * Parameters:
 *   board      - The Sudoku board to operate on
 *   cellIndex  - The index of the cell to set
 *   value      - The ValueSet containing the value to set (should be a single value)
 ******************************************************************************/
void SetCellAndPropagate(Board& board, int cellIndex, const ValueSet& value);
