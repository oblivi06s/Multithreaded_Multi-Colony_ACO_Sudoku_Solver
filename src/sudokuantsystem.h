#pragma once
#include <vector>
#include <random>
#include "antcolonyinterface.h"
#include "sudokuant.h"
#include "board.h"
#include "timer.h"
#include "sudokusolver.h"

class SudokuAntSystem : public SudokuSolver, public IAntColony
{
	int numAnts;
	float q0;
	float rho;
	float pher0;
	float bestEvap;
	Board bestSol;
	float bestPher;
	int iterationsCompleted;
	Timer solutionTimer;
	Timer acsTimer;  // Main ACS algorithm timer (paused during CP)
	float solTime;
	float acsTime;  // Time spent in main ACS algorithm work

	std::vector<SudokuAnt*> antList;
	std::mt19937 randGen; 
	std::uniform_real_distribution<float> randomDist;

	float **pher; // pheromone matrix
	int numCells;
	void InitPheromone(int numCells, int valuesPerCell);
	void ClearPheromone();
	void UpdatePheromone();
	float PherAdd(int numCellsFixed);

public:
	SudokuAntSystem(int numAnts, float q0, float rho, float pher0, float bestEvap) : 
		numAnts(numAnts), q0(q0), rho(rho), pher0(pher0), bestEvap(bestEvap), iterationsCompleted(0)
	{
		for ( int i = 0; i < numAnts; i++ )
			antList.push_back(new SudokuAnt(this));
		randomDist = std::uniform_real_distribution<float>(0.0f, 1.0f);
		std::random_device rd;
		randGen = std::mt19937(rd());
	}
	~SudokuAntSystem()
	{
		for (auto a : antList)
			delete a;
	}
	virtual bool Solve(const Board& puzzle, float maxTime );
	virtual float GetSolutionTime() { return solTime; }
	virtual const Board& GetSolution() { return bestSol; }
	int GetIterationsCompleted() { return iterationsCompleted; }
	float GetACSTime() const { return acsTime; }
	// helpers for ants
	inline float Getq0() { return q0; }
	inline float random() { return randomDist(randGen); }
	inline float Pher(int i, int j) { return pher[i][j]; }
	void LocalPheromoneUpdate(int iCell, int iChoice);
};
