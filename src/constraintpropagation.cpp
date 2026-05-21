/*******************************************************************************
 * CONSTRAINT PROPAGATION - Implementation
 * 
 * This module implements classic Sudoku constraint propagation rules that
 * reduce the search space before ACO begins.
 ******************************************************************************/

#include "constraintpropagation.h"
#include "board.h"
#include "timer.h"
#include <chrono>
#include <atomic>
#include <thread>

// ============================================================================
// HELPER: Thread-safe float increment
// ============================================================================

static void AtomicAddFloat(std::atomic<float>& target, float value)
{
	float current = target.load();
	while (!target.compare_exchange_weak(current, current + value));
}

// Global timing statistics (thread-safe for parallel ACO)
static std::atomic<float> g_initialCPTime{0.0f};
static std::atomic<float> g_antCPTime{0.0f};
static std::atomic<int> g_cpCallCount{0};
static bool g_inInitialCP = false;

// Thread-local storage for per-thread CP time accumulator (atomic for thread-safety)
thread_local std::atomic<float>* g_threadCPTime = nullptr;
// Thread-local storage for per-thread main algorithm timer (for pause/resume)
thread_local Timer* g_mainAlgorithmTimer = nullptr;

void ResetCPTiming()
{
	g_initialCPTime.store(0.0f);
	g_antCPTime.store(0.0f);
	g_cpCallCount.store(0);
	g_inInitialCP = false;
}

float GetInitialCPTime()
{
	return g_initialCPTime.load();
}

float GetAntCPTime()
{
	return g_antCPTime.load();
}

int GetCPCallCount()
{
	return g_cpCallCount.load();
}

// Mark that we're in initial CP phase (called from Board constructor)
void BeginInitialCP()
{
	g_inInitialCP = true;
}

void EndInitialCP()
{
	g_inInitialCP = false;
}

// ============================================================================
// CP NAMESPACE - Per-thread CP time tracking implementation
// ============================================================================

void CP::RegisterThreadCPTime(std::atomic<float>* cpTimePtr)
{
	g_threadCPTime = cpTimePtr;
}

void CP::UnregisterThreadCPTime()
{
	g_threadCPTime = nullptr;
}

void CP::AddTime(float elapsed)
{
	if (g_threadCPTime != nullptr)
	{
		// Thread-safe atomic accumulation (for algorithms 2, 4)
		float expected = g_threadCPTime->load();
		float desired;
		do {
			desired = expected + elapsed;
		} while (!g_threadCPTime->compare_exchange_weak(expected, desired));
	}
}

void CP::RegisterMainAlgorithmTimer(Timer* timerPtr)
{
	g_mainAlgorithmTimer = timerPtr;
}

void CP::UnregisterMainAlgorithmTimer()
{
	g_mainAlgorithmTimer = nullptr;
}

Timer* CP::GetMainAlgorithmTimer()
{
	return g_mainAlgorithmTimer;
}

/*******************************************************************************
 * Rule1_Elimination
 * 
 * Implementation of the elimination rule:
 * 1. Collect all fixed values in the cell's row, column, and box
 * 2. Remove these values from the cell's possibilities
 * 3. If only one value remains, fix the cell to that value
 ******************************************************************************/
bool Rule1_Elimination(Board& board, int cellIndex)
{
	// Start timing this CP rule
	auto startTime = std::chrono::high_resolution_clock::now();
	
	const ValueSet& cell = board.GetCell(cellIndex);
	
	// Skip if cell is already fixed or empty
	if (cell.Empty() || cell.Fixed())
	{
		// Still count the time for this check
		auto endTime = std::chrono::high_resolution_clock::now();
		float elapsed = std::chrono::duration<float>(endTime - startTime).count();
		if (g_inInitialCP)
			AtomicAddFloat(g_initialCPTime, elapsed);
		else if (g_threadCPTime != nullptr)
		{
			// Thread-safe atomic accumulation for parallel algorithms (alg 2, 4)
			float expected = g_threadCPTime->load();
			float desired;
			do {
				desired = expected + elapsed;
			} while (!g_threadCPTime->compare_exchange_weak(expected, desired));
		}
		else
			AtomicAddFloat(g_antCPTime, elapsed);  // Global accumulation for single-threaded (alg 0, 3)
		return false;
	}
	
	int numUnits = board.GetNumUnits();
	int iBox = board.BoxForCell(cellIndex);
	int iCol = board.ColForCell(cellIndex);
	int iRow = board.RowForCell(cellIndex);
	
	// Collect all fixed values in the row, column, and box
	ValueSet colFixed(numUnits), rowFixed(numUnits), boxFixed(numUnits);
	
	for (int j = 0; j < numUnits; j++)
	{
		int k;
		
		// Box cells
		k = board.BoxCell(iBox, j);
		if (k != cellIndex && board.GetCell(k).Fixed())
			boxFixed += board.GetCell(k);
		
		// Column cells
		k = board.ColCell(iCol, j);
		if (k != cellIndex && board.GetCell(k).Fixed())
			colFixed += board.GetCell(k);
		
		// Row cells
		k = board.RowCell(iRow, j);
		if (k != cellIndex && board.GetCell(k).Fixed())
			rowFixed += board.GetCell(k);
	}
	
	// Calculate which values are still possible (complement of fixed values)
	ValueSet fixedCellsConstraint = ~(rowFixed + colFixed + boxFixed);
	
	// End timing for this rule (before recursive call)
	auto endTime = std::chrono::high_resolution_clock::now();
	float elapsed = std::chrono::duration<float>(endTime - startTime).count();
	if (g_inInitialCP)
		AtomicAddFloat(g_initialCPTime, elapsed);
	else if (g_threadCPTime != nullptr)
	{
		// Thread-safe atomic accumulation for parallel algorithms (alg 2, 4)
		float current = g_threadCPTime->load();
		while (!g_threadCPTime->compare_exchange_weak(current, current + elapsed));
	}
	else
		AtomicAddFloat(g_antCPTime, elapsed);  // Global accumulation for single-threaded (alg 0, 3)
	
	// If after elimination only one value remains, fix the cell
	if (fixedCellsConstraint.Fixed())
	{
		SetCellAndPropagate(board, cellIndex, fixedCellsConstraint);
		return true;
	}
	else
	{
		// Apply the elimination (remove impossible values from this cell)
		board.SetCellDirect(cellIndex, board.GetCell(cellIndex) ^ fixedCellsConstraint);
		return false;
	}
	
}

/*******************************************************************************
 * Rule2_HiddenSingle
 * 
 * Implementation of the hidden single rule:
 * 1. For each possible value in this cell, check if it can appear elsewhere
 *    in the same row, column, or box
 * 2. If a value can only appear in this cell within a unit, fix the cell
 *    to that value (it's a "hidden single")
 ******************************************************************************/
bool Rule2_HiddenSingle(Board& board, int cellIndex)
{
	// Start timing this CP rule
	auto startTime = std::chrono::high_resolution_clock::now();
	
	const ValueSet& cell = board.GetCell(cellIndex);
	
	// Skip if cell is already fixed or empty
	if (cell.Empty() || cell.Fixed())
	{
		// Still count the time for this check
		auto endTime = std::chrono::high_resolution_clock::now();
		float elapsed = std::chrono::duration<float>(endTime - startTime).count();
		if (g_inInitialCP)
			AtomicAddFloat(g_initialCPTime, elapsed);
		else if (g_threadCPTime != nullptr)
		{
			// Thread-safe atomic accumulation for parallel algorithms (alg 2, 4)
			float expected = g_threadCPTime->load();
			float desired;
			do {
				desired = expected + elapsed;
			} while (!g_threadCPTime->compare_exchange_weak(expected, desired));
		}
		else
			AtomicAddFloat(g_antCPTime, elapsed);  // Global accumulation for single-threaded (alg 0, 3)
		return false;
	}
	
	int numUnits = board.GetNumUnits();
	int iBox = board.BoxForCell(cellIndex);
	int iCol = board.ColForCell(cellIndex);
	int iRow = board.RowForCell(cellIndex);
	
	// Collect all possible values in the row, column, and box (excluding this cell)
	ValueSet colAll(numUnits), rowAll(numUnits), boxAll(numUnits);
	
	for (int j = 0; j < numUnits; j++)
	{
		int k;
		
		// Box cells
		k = board.BoxCell(iBox, j);
		if (k != cellIndex)
			boxAll += board.GetCell(k);
		
		// Column cells
		k = board.ColCell(iCol, j);
		if (k != cellIndex)
			colAll += board.GetCell(k);
		
		// Row cells
		k = board.RowCell(iRow, j);
		if (k != cellIndex)
			rowAll += board.GetCell(k);
	}
	
	// End timing for this rule (before recursive call)
	auto endTime = std::chrono::high_resolution_clock::now();
	float elapsed = std::chrono::duration<float>(endTime - startTime).count();
	if (g_inInitialCP)
		AtomicAddFloat(g_initialCPTime, elapsed);
	else if (g_threadCPTime != nullptr)
	{
		// Thread-safe atomic accumulation for parallel algorithms (alg 2, 4)
		float current = g_threadCPTime->load();
		while (!g_threadCPTime->compare_exchange_weak(current, current + elapsed));
	}
	else
		AtomicAddFloat(g_antCPTime, elapsed);  // Global accumulation for single-threaded (alg 0, 3)
	
	// Check if any value in this cell is unique to this cell within its row
	if ((cell - rowAll).Fixed())
	{
		SetCellAndPropagate(board, cellIndex, cell - rowAll);
		return true;
	}
	// Check if any value in this cell is unique to this cell within its column
	else if ((cell - colAll).Fixed())
	{
		SetCellAndPropagate(board, cellIndex, cell - colAll);
		return true;
	}
	// Check if any value in this cell is unique to this cell within its box
	else if ((cell - boxAll).Fixed())
	{
		SetCellAndPropagate(board, cellIndex, cell - boxAll);
		return true;
	}
	
	return false;
}

/*******************************************************************************
 * PropagateConstraints
 * 
 * Main entry point for constraint propagation. Applies both Rule1 (elimination)
 * and Rule2 (hidden single) to the specified cell.
 * 
 * This function is called after a cell is set to propagate the constraints
 * to neighboring cells.
 ******************************************************************************/
void PropagateConstraints(Board& board, int cellIndex)
{
	const ValueSet& cell = board.GetCell(cellIndex);
	
	// Skip if cell is already fixed or empty
	if (cell.Empty() || cell.Fixed())
		return;
	
	// Apply Rule 1: Elimination
	if (Rule1_Elimination(board, cellIndex))
		return; // Cell was fixed by Rule1, done
	
	// Apply Rule 2: Hidden Single
	Rule2_HiddenSingle(board, cellIndex);
	
	// Check if cell became infeasible (no possible values)
	if (board.GetCell(cellIndex).Empty())
		board.IncrementInfeasible();
}

/*******************************************************************************
 * SetCellAndPropagate
 * 
 * Sets a cell to a specific value and propagates the constraints to all
 * cells in the same row, column, and box.
 * 
 * This is the main function used during puzzle initialization.
 * 
 * NOTE: Timing is done in Rule1/Rule2 functions to avoid double-counting
 * due to recursive calls.
 ******************************************************************************/
void SetCellAndPropagate(Board& board, int cellIndex, const ValueSet& value)
{
	// Skip if cell is already fixed
	if (board.GetCell(cellIndex).Fixed())
		return;
	
	// Set the cell directly
	board.SetCellDirect(cellIndex, value);
	board.IncrementFixedCells();
	
	// Count this as a CP call (but timing is done in Rule functions)
	if (!g_inInitialCP)
		g_cpCallCount.fetch_add(1);
	
	// Propagate constraints to all cells in the same row, column, and box
	int numUnits = board.GetNumUnits();
	int iBox = board.BoxForCell(cellIndex);
	int iCol = board.ColForCell(cellIndex);
	int iRow = board.RowForCell(cellIndex);
	
	for (int j = 0; j < numUnits; j++)
	{
		int k;
		
		// Propagate to box cells
		k = board.BoxCell(iBox, j);
		if (k != cellIndex)
			PropagateConstraints(board, k);
		
		// Propagate to column cells
		k = board.ColCell(iCol, j);
		if (k != cellIndex)
			PropagateConstraints(board, k);
		
		// Propagate to row cells
		k = board.RowCell(iRow, j);
		if (k != cellIndex)
			PropagateConstraints(board, k);
	}
}
