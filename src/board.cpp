/*******************************************************************************
 * BOARD - Sudoku Board Data Structure
 * 
 * This file implements the Board class, which represents a Sudoku puzzle
 * as a data structure. The Board stores:
 * - Cell values and their possibilities (using ValueSet)
 * - Puzzle dimensions (order, numUnits, numCells)
 * - Status counters (numFixedCells, numInfeasible)
 * 
 * Constraint propagation logic has been extracted to constraintpropagation.cpp
 * for better code organization.
 ******************************************************************************/

#include "board.h"
#include "constraintpropagation.h"
#include <iostream>
#include <iomanip>
#include <inttypes.h>
#include <string>
#include <sstream>

// ============================================================================
// SECTION 1: PUZZLE READING & INITIALIZATION
// ============================================================================

/*******************************************************************************
 * Board Constructor - Parse puzzle string and initialize board
 * 
 * Reads a puzzle string where:
 * - '.' represents an empty cell
 * - Digits/letters represent fixed cells
 * 
 * Puzzle sizes supported: 9x9 (81 cells), 16x16 (256 cells), 25x25 (625 cells),
 * 36x36 (1296 cells), etc.
 * 
 * After parsing, constraint propagation is applied to deduce additional cells.
 ******************************************************************************/
Board::Board(const string &puzzleString)
{
	// Determine puzzle order based on string length
	switch (puzzleString.length())
	{
	case 81:
		order = 3;
		break;
	case 256:
		order = 4;
		break;
	case 625:
		order = 5;
		break;
	case 1296:
		order = 6;
		break;
	case 2401:
		order = 7;
		break;
	case 4096:
		order = 8;
		break;
	default:
		std::cerr << "wrong number of cells for a sudoku board!" << std::endl;
		order = 0;
		break;
	}

	numUnits = order * order;
	numCells = numUnits * numUnits;
	
	cells = new ValueSet[numCells];

	int maxVal = numUnits;

	// Initialize all cells with all possible values
	for (int i = 0; i < numCells; i++)
		cells[i].Init(maxVal);

	for ( int i = 0; i < numCells; i++ )
	{
		cells[i] = ~cells[i];
	}
	
	// Set the known cells one by one using constraint propagation
	numInfeasible = 0;
	numFixedCells = 0;
	
	// Mark that we're in initial CP phase (for timing)
	BeginInitialCP();
	
	for (int i = 0; i < numCells; i++)
	{
		if (puzzleString[i] != '.')
		{
			int value;
			switch (order)
			{
			case 3:
				value = (int)(puzzleString[i] - '0');
				break;
			case 4:
				if (puzzleString[i] >= '0' && puzzleString[i] <= '9')
					value = 1+(int)(puzzleString[i] - '0');
				else
					value = 11 + (int)(puzzleString[i] - 'a');
				break;
			case 5:
			default:
				value = 1+(int)(puzzleString[i] - 'a');
			}
			SetCellAndPropagate(*this, i, ValueSet(maxVal, (int64_t)1 << (value-1) ));
		}
	}
	
	// End initial CP phase
	EndInitialCP();
}

/*******************************************************************************
 * Copy Constructor - Create a new board as a copy of another
 ******************************************************************************/
Board::Board(const Board &other)
{
	Copy(other);
}

/*******************************************************************************
 * Copy - Deep copy another board's state
 ******************************************************************************/
void Board::Copy(const Board& other)
{
	order = other.order;
	numUnits = order * order;
	numCells = numUnits * numUnits;

	if (cells == nullptr)
		cells = new ValueSet[numCells];

	int maxVal = numUnits;

	for (int i = 0; i < numCells; i++)
		cells[i] = other.GetCell(i);

	numFixedCells = other.FixedCellCount();
	numInfeasible = other.InfeasibleCellCount();
}

/*******************************************************************************
 * Destructor - Clean up allocated memory
 ******************************************************************************/
Board::~Board()
{
	if ( cells != nullptr ) delete [] cells;
}

// ============================================================================
// SECTION 2: GEOMETRIC HELPER FUNCTIONS
// These functions convert between different cell indexing schemes
// ============================================================================

/*******************************************************************************
 * RowCell - Get the index of the iCell'th cell in the iRow'th row
 ******************************************************************************/
int Board::RowCell(int iRow, int iCell) const
{
	return iRow * numUnits + iCell;
}

/*******************************************************************************
 * ColCell - Get the index of the iCell'th cell in the iCol'th column
 ******************************************************************************/
int Board::ColCell(int iCol, int iCell) const
{
	return iCell * numUnits + iCol;
}

/*******************************************************************************
 * BoxCell - Get the index of the iCell'th cell in the iBox'th box
 ******************************************************************************/
int Board::BoxCell(int iBox, int iCell) const
{
	int boxCol = iBox%order;
	int boxRow = iBox / order;
	int topCorner =  (boxCol*order) + boxRow*order*order*order;
	return topCorner + (iCell%order) + (iCell / order)*order*order;
}

/*******************************************************************************
 * RowForCell - Get the row index containing the given cell
 ******************************************************************************/
int Board::RowForCell(int iCell) const
{
	return iCell / numUnits;
}

/*******************************************************************************
 * ColForCell - Get the column index containing the given cell
 ******************************************************************************/
int Board::ColForCell(int iCell) const
{
	return iCell % numUnits;
}

/*******************************************************************************
 * BoxForCell - Get the box index containing the given cell
 ******************************************************************************/
int Board::BoxForCell(int iCell) const
{
	return order*(iCell / (order*order*order)) + ((iCell%(order*order))/order);
}

// ============================================================================
// SECTION 3: BOARD QUERY/STATUS FUNCTIONS
// ============================================================================

/*******************************************************************************
 * GetCell - Access a cell's ValueSet
 ******************************************************************************/
const ValueSet& Board::GetCell(int i) const
{
	return cells[i];
}

/*******************************************************************************
 * FixedCellCount - Number of cells with uniquely determined values
 ******************************************************************************/
int Board::FixedCellCount(void) const
{
	return numFixedCells;
}

/*******************************************************************************
 * InfeasibleCellCount - Number of cells with no possible values (error state)
 ******************************************************************************/
int Board::InfeasibleCellCount(void) const
{
	return numInfeasible;
}

/*******************************************************************************
 * CellCount - Total number of cells in the board
 ******************************************************************************/
int Board::CellCount(void) const
{
	return numCells;
}

/*******************************************************************************
 * GetNumUnits - Number of units (rows, columns, or boxes)
 ******************************************************************************/
int Board::GetNumUnits(void) const
{
	return numUnits;
}

// ============================================================================
// SECTION 4: OUTPUT & VISUALIZATION
// ============================================================================

/*******************************************************************************
 * AsString - Convert board to human-readable string representation
 * 
 * Parameters:
 *   useNumbers    - Use numeric representation (1..numUnits) vs single chars
 *   showUnfixed   - Show possibilities for unfixed cells (vs '.')
 ******************************************************************************/
string Board::AsString(bool useNumbers, bool showUnfixed )
{
	if ( showUnfixed )
		useNumbers = false;

	stringstream puzString;
	string alphabet;

	if ( !useNumbers )
	{
		if ( order == 3 )
			alphabet = string("123456789");
		else if (order == 4 )
			alphabet = string("0123456789abcdef");
		else
			alphabet = string("abcdefghijklmnopqrstuvwxy");
	}

	vector<string> cellStrings;
	size_t maxLen = 0;
	for (int i = 0; i < numCells; i++)
	{
		string cellContents;
		if ( !useNumbers )
		{
			if ( !showUnfixed && !cells[i].Fixed())
				cellContents = string(".");
			else
				cellContents = cells[i].toString(alphabet);
		}
		else 
			cellContents = to_string(cells[i].Index() + 1);
		if (cellContents.size() > maxLen )
			maxLen = cellContents.size();
		cellStrings.push_back(cellContents);
	}
	int pitch = static_cast<int>(maxLen + 1);
	for ( int i = 0; i < numCells; i++ )
	{
		puzString << setw(pitch) << cellStrings[i] << " ";
		if ( i%numUnits == numUnits -1 )
		{
			if ( i != numCells-1 )
				puzString << endl;
		}
		else if (i%order == order-1 )
			puzString << string("|");
		if ( i%(numUnits*order) == numUnits*order-1 && i != numCells-1)
		{
			for ( int j = 0; j < order; j++ )
			{
				for ( int k = 0; k < order*(pitch+1); k++ )
					puzString << '-';
				if ( j != order-1 )
					puzString << '+';
			}
			puzString << endl;
		}
	}
	return puzString.str();
}

/*******************************************************************************
 * CheckSolution - Verify that another board is a valid solution to this puzzle
 * 
 * Checks:
 * 1. All cells are filled
 * 2. All rows, columns, and boxes contain each number exactly once
 * 3. Fixed cells in this puzzle match the solution
 ******************************************************************************/
bool Board::CheckSolution(const Board& other) const
{
	if (other.CellCount() != CellCount())
		return false;
	bool isSolution = true;
	bool isConsistent = true;

	// Check it's a complete solution - all cells must be filled
	for (int i = 0; i < other.CellCount(); i++)
	{
		if (!other.GetCell(i).Fixed())
			isSolution = false;
	}
	
	// Check all rows, columns, boxes have one of each number
	for (int i = 0; i < numUnits; i++)
	{
		ValueSet row, col, box;
		row.Init(numUnits);
		col.Init(numUnits);
		box.Init(numUnits);
		for (int j = 0; j < numUnits; j++)
		{
			row += other.GetCell(RowCell(i, j));
			col += other.GetCell(ColCell(i, j));
			box += other.GetCell(BoxCell(i, j));
		}
		if (row.Count() != numUnits || col.Count() != numUnits || box.Count() != numUnits )
			isSolution = false;
	}

	// Check consistency with this board's fixed cells
	for (int i = 0; i < CellCount(); i++)
	{
		if (GetCell(i).Fixed())
		{
			if (GetCell(i).Index() != other.GetCell(i).Index() )
				isConsistent = false;
		}
	}

	return isSolution && isConsistent;
}

// ============================================================================
// SECTION 5: INTERNAL HELPER METHODS FOR CONSTRAINT PROPAGATION
// These methods are used by constraintpropagation.cpp
// ============================================================================

/*******************************************************************************
 * SetCellDirect - Set a cell value without propagation
 * Used internally by constraint propagation module
 ******************************************************************************/
void Board::SetCellDirect(int i, const ValueSet &c)
{
	cells[i] = c;
}

/*******************************************************************************
 * IncrementFixedCells - Update the fixed cell counter
 * Used internally by constraint propagation module
 ******************************************************************************/
void Board::IncrementFixedCells()
{
	++numFixedCells;
}

/*******************************************************************************
 * IncrementInfeasible - Update the infeasible cell counter
 * Used internally by constraint propagation module
 ******************************************************************************/
void Board::IncrementInfeasible()
{
	++numInfeasible;
}
