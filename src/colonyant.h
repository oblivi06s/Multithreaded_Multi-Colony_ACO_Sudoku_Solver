#pragma once
#include "board.h"

class MultiColonyAntSystem;

class ColonyAnt
{
    Board sol;            // current working solution
    int iCell;            // current cell
    MultiColonyAntSystem *parent; // parent multi-colony system
    int failCells;        // number of unsettable cells this attempt
    float *roulette;      // working array for roulette wheel selection
    ValueSet *rouletteVals; // working array for roulette wheel selection
    int colonyIndex;      // which colony this ant belongs to

public:
    ColonyAnt(MultiColonyAntSystem *parent, int colonyIndex)
        : iCell(0), parent(parent), failCells(0), roulette(nullptr), rouletteVals(nullptr), colonyIndex(colonyIndex) {}
    void InitSolution(const Board &puzzle, int startCell);
    void StepSolution();
    const Board& GetSolution() { return sol; }
    int NumCellsFilled() { return sol.CellCount() - failCells; }
};