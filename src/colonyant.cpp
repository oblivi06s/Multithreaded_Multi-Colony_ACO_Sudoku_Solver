#include "colonyant.h"
#include "multicolonyantsystem.h"
#include "constraintpropagation.h"

void ColonyAnt::InitSolution(const Board &puzzle, int startCell)
{
    sol.Copy(puzzle);
    iCell = startCell;
    failCells = 0;
    if (roulette != nullptr)
    {
        delete[] roulette;
        delete[] rouletteVals;
    }
    roulette = new float[puzzle.GetNumUnits()];
    rouletteVals = new ValueSet[puzzle.GetNumUnits()];
}

void ColonyAnt::StepSolution()
{
    if (sol.GetCell(iCell).Empty())
    {
        failCells++;
    }
    else if (!sol.GetCell(iCell).Fixed())
    {
        // make a choice from the options
        ValueSet choice = ValueSet(sol.GetNumUnits(), 1);
        if (parent->random() < parent->Getq0(colonyIndex))
        {
            // greedy selection
            ValueSet best;
            float maxPher = -1.0f;

            for (int i = 0; i < sol.GetNumUnits(); i++)
            {
                if (sol.GetCell(iCell).Contains(choice))
                {
                    float ph = parent->Pher(colonyIndex, iCell, i);
                    if (ph > maxPher)
                    {
                        maxPher = ph;
                        best = choice;
                    }
                }
                choice <<= 1;
            }
            
            SetCellAndPropagate(sol, iCell, best);
            
            // local pheromone update
            parent->LocalPheromoneUpdate(colonyIndex, iCell, best.Index());
        }
        else
        {
            // weighted selection
            float totPher = 0.0f;
            int numChoices = 0;
            for (int i = 0; i < sol.GetNumUnits(); i++)
            {
                if (sol.GetCell(iCell).Contains(choice))
                {
                    float ph = parent->Pher(colonyIndex, iCell, i);
                    roulette[numChoices] = totPher + ph;
                    totPher = roulette[numChoices];
                    rouletteVals[numChoices] = choice;
                    ++numChoices;
                }
                choice <<= 1;
            }
            float rouletteVal = totPher * parent->random();

            for (int i = 0; i < numChoices; i++)
            {
                if (roulette[i] > rouletteVal)
                {
                    SetCellAndPropagate(sol, iCell, rouletteVals[i]);
                    
                    // local pheromone update
                    parent->LocalPheromoneUpdate(colonyIndex, iCell, rouletteVals[i].Index());
                    break;
                }
            }
        }
    }
    ++iCell;
    if (iCell == sol.CellCount()) // wrap around
        iCell = 0;
}