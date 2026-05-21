#pragma once

// Interface for ant colony systems
// This allows SudokuAnt to work with both SudokuAntSystem and SubColony
class IAntColony
{
public:
	virtual ~IAntColony() {}
	virtual float Getq0() = 0;
	virtual float random() = 0;
	virtual float Pher(int i, int j) = 0;
	virtual void LocalPheromoneUpdate(int iCell, int iChoice) = 0;
};


