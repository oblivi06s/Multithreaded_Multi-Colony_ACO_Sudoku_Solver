#include "multicolonyantsystem.h"
#include "constraintpropagation.h"
#include <iostream>
#include <algorithm>
#include <climits>
#include <limits>
#include <cmath>
#include <chrono>

void MultiColonyAntSystem::InitPheromone(Colony &c, int nNumCells, int valuesPerCell)
{
    c.numCells = nNumCells;
    c.valuesPerCell = valuesPerCell;
    c.pher = new float *[c.numCells];
    for (int i = 0; i < c.numCells; i++)
    {
        c.pher[i] = new float[valuesPerCell];
        for (int j = 0; j < valuesPerCell; j++)
            c.pher[i][j] = pher0;
    }
}

void MultiColonyAntSystem::ClearPheromone(Colony &c)
{
    for (int i = 0; i < c.numCells; i++)
        delete[] c.pher[i];
    delete[] c.pher;
    c.pher = nullptr;
}

float MultiColonyAntSystem::PherAdd(int numCells, int cellsFilled)
{
    return numCells / (float)(numCells - cellsFilled);
}

void MultiColonyAntSystem::UpdatePheromone(int colonyIdx, Colony &c, const Board &bestSol, float bestPher)
{
    for (int i = 0; i < c.numCells; i++)
    {
        if (bestSol.GetCell(i).Fixed())
        {
            int idx = bestSol.GetCell(i).Index();
            float r = GetRho(colonyIdx);
            c.pher[i][idx] = c.pher[i][idx] * (1.0f - r) + r * bestPher;
        }
    }
    ClampPheromone(c);
}

bool MultiColonyAntSystem::Solve(const Board &puzzle, float maxTime)
{
    solutionTimer.Reset();
    dcmAcoTimer.Reset();
    int iter = 0;
    bool solved = false;
    const int nACS = (std::min)(numACS, numColonies);
    
    // Reset timing counters
    dcmAcoTime = 0.0f;
    cooperativeGameTime = 0.0f;
    pheromoneFusionTime = 0.0f;
    publicPathRecommendationTime = 0.0f;

    // init colonies
    colonyQ0.resize(numColonies);
    colonyRho.resize(numColonies);
    for (int c = 0; c < numColonies; ++c)
    {
        InitPheromone(colonies[c], puzzle.CellCount(), puzzle.GetNumUnits());
        colonies[c].bestPher = 0.0f;
        colonies[c].bestVal = 0;
        colonies[c].tau0 = pher0;
        // assign colony type: 2 ACS, 1 MMAS (for DCM-ACO)
        colonies[c].type = (c < nACS ? 0 : 1);
        // Parameters per colony type:
        //  - ACS colonies (first 2): use ACS q0 and rho from Lloyd & Amos ACS style
        //  - MMAS colony (remaining 1): pure roulette (q0 = 0), rho fixed to 0.1 as per paper
        if (colonies[c].type == 0)
        {
            colonyQ0[c] = q0;
            colonyRho[c] = rho;
        }
        else
        {
            colonyQ0[c] = 0.0f;
            colonyRho[c] = 0.1f;
        }
        // initial Max-Min bounds (used by MMAS colonies)
        // Initialize with initial pheromone value
        if (colonies[c].type == 1) // MMAS
        {
            float rho_mmas = colonyRho[c]; // 0.1 for MMAS
            float best_pheromone_toAdd = pher0; // initial estimate
            float n = (float)puzzle.GetNumUnits(); // puzzle size (e.g., 9 for 9x9)
            colonies[c].tauMax = best_pheromone_toAdd / rho_mmas;
            colonies[c].tauMin = colonies[c].tauMax / (2.0f * n);
        }

        colonies[c].lastImproveIter = 0;
        // create ants
        for (int i = 0; i < antsPerColony; i++)
            colonies[c].ants.push_back(new ColonyAnt(this, c));
    }

    std::uniform_int_distribution<int> startDist(0, puzzle.CellCount() - 1);

    while (!solved)
    {
        // init ant solutions with different starts
        for (int c = 0; c < numColonies; ++c)
        {
            for (auto *a : colonies[c].ants)
                a->InitSolution(puzzle, startDist(randGen));
        }

        // construct solutions cell by cell
        for (int i = 0; i < puzzle.CellCount(); i++)
        {
            for (int c = 0; c < numColonies; ++c)
            {
                for (auto *a : colonies[c].ants)
                    a->StepSolution();
            }
        }

        // per-colony: evaluate bests and track global best
        for (int c = 0; c < numColonies; ++c)
        {
            int iBest = 0;
            int bestVal = 0;
            auto &ants = colonies[c].ants;
            for (unsigned int i = 0; i < ants.size(); i++)
            {
                if (ants[i]->NumCellsFilled() > bestVal)
                {
                    bestVal = ants[i]->NumCellsFilled();
                    iBest = (int)i;
                }
            }
            float pherToAdd = PherAdd(colonies[c].numCells, bestVal);
            if (pherToAdd > colonies[c].bestPher)
            {
                colonies[c].bestSol.Copy(ants[iBest]->GetSolution());
                colonies[c].bestPher = pherToAdd;
                colonies[c].bestVal = bestVal;
                colonies[c].lastImproveIter = iter;
                // Update Max-Min bounds with improvement (for MMAS colonies)
                if (colonies[c].type == 1)
                {
                    // τ_max = best_pheromone_toAdd / ρ
                    // τ_min = τ_max / (2n) where n is the puzzle size (e.g., 9 for 9x9)
                    float rho_mmas = GetRho(c); // Should be 0.1 for MMAS
                    float best_pheromone_toAdd = colonies[c].bestPher; // best pheromone value
                    float n = (float)colonies[c].valuesPerCell; // puzzle size (e.g., 9 for 9x9)
                    
                    colonies[c].tauMax = best_pheromone_toAdd / rho_mmas;
                    colonies[c].tauMin = colonies[c].tauMax / (2.0f * n);
                }
            }
            // update global best
            if (colonies[c].bestPher > globalBestPher)
            {
                globalBestPher = colonies[c].bestPher;
                globalBestSol.Copy(colonies[c].bestSol);
                globalBestVal = colonies[c].bestVal;

                {
                    int _nu = puzzle.GetNumUnits();
                    int _ord = (_nu <= 9) ? 3 : (_nu <= 16) ? 4 : 5;
                    std::string _flat;
                    _flat.reserve(puzzle.CellCount());
                    for (int _i = 0; _i < globalBestSol.CellCount(); _i++) {
                        const ValueSet& _c = globalBestSol.GetCell(_i);
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

                if (globalBestVal == puzzle.CellCount())
                {
                    solved = true;
                    solTime = solutionTimer.Elapsed();
                }
            }
        }

        // partition indices by type
        std::vector<int> acsIdx; acsIdx.reserve(numColonies);
        std::vector<int> mmasIdx; mmasIdx.reserve(numColonies);
        for (int c = 0; c < numColonies; ++c)
            (colonies[c].type == 0 ? acsIdx : mmasIdx).push_back(c);

        // Check ACS entropy: split by threshold and apply appropriate mechanism per colony
        // Low entropy (< threshold) -> pheromone fusion
        // High entropy (>= threshold) -> cooperative game allocation
        std::vector<float> acsAllocated; acsAllocated.resize(numColonies, 0.0f);
        std::vector<int> acsLowEntropy;   // Below threshold -> pheromone fusion
        std::vector<int> acsHighEntropy; // Above threshold -> cooperative game
        
        if (!acsIdx.empty())
        {
            // Split ACS colonies by entropy threshold
            for (int cidx : acsIdx)
            {
                if (ComputeEntropy(colonies[cidx]) < entropyThreshold)
                    acsLowEntropy.push_back(cidx);
                else
                    acsHighEntropy.push_back(cidx);
            }
            
            // Apply pheromone fusion to low entropy colonies
            if (!acsLowEntropy.empty() && !mmasIdx.empty())
            {
                // Time Pheromone Fusion
                auto startTime = std::chrono::steady_clock::now();
                ApplyPheromoneFusion(acsLowEntropy, mmasIdx);
                auto endTime = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
                pheromoneFusionTime += (float)duration.count();
            }
            
            // Apply cooperative game allocation to high entropy colonies
            if (!acsHighEntropy.empty())
            {
                // Time Cooperative Game Theory
                auto startTime = std::chrono::steady_clock::now();
                ACSCooperativeGameAllocate(acsHighEntropy, acsAllocated);
                auto endTime = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
                cooperativeGameTime += (float)duration.count();
                
                // Update pheromone for high entropy colonies (using cooperative game allocation)
                for (int c : acsHighEntropy)
                {
                    float add = acsAllocated[c];  // Use allocated pheromone from cooperative game
                    UpdatePheromone(c, colonies[c], colonies[c].bestSol, add);
                    colonies[c].bestPher *= (1.0f - bestEvap);
                }
            }
        }


        
        // Check MMAS convergence speed: apply appropriate mechanism
        // Low convergence speed (< threshold) -> public path recommendation
        // High convergence speed (>= threshold) -> update pheromone
        // Note: Only 1 MMAS colony, so no need to split into vectors
        if (!mmasIdx.empty())
        {
            int mmasCidx = mmasIdx[0];  // MMAS colony
            // convergence rate con_t = iter_opt / iter_t
            float con_t = (iter > 0 ? ((float)colonies[mmasCidx].lastImproveIter / (float)iter) : 1.0f);
            
            if (con_t < convThreshold)
            {
                // Low convergence speed -> public path recommendation
                if (!acsIdx.empty())
                {
                    // Time Public Path Recommendation
                    auto startTime = std::chrono::steady_clock::now();
                    ApplyPublicPathRecommendation(iter, acsIdx, std::vector<int>{mmasCidx});
                    auto endTime = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
                    publicPathRecommendationTime += (float)duration.count();
                }
            }
            else
            {
                // High convergence speed -> update pheromone
                UpdatePheromone(mmasCidx, colonies[mmasCidx], colonies[mmasCidx].bestSol, colonies[mmasCidx].bestPher);
                // colonies[mmasCidx].bestPher *= (1.0f - bestEvap); 
            }
        }

        ++iter;
        if ((iter % 100) == 0)
        {
            float elapsed = solutionTimer.Elapsed();
            if (elapsed > maxTime)
                break;
        }
    }

    for (int c = 0; c < numColonies; ++c)
        ClearPheromone(colonies[c]);
    
    iterationCount = iter;
    
    // Always capture solution time, regardless of success/failure
    solTime = solutionTimer.Elapsed();
    
    // Get the DCM-ACO time (will be calculated by subtraction in solvermain.cpp)
    dcmAcoTime = dcmAcoTimer.Elapsed();
    
    std::cout << "Number of cycles (multi): " << iter << "\n";
    return solved;
}

void MultiColonyAntSystem::ClampPheromone(Colony &c)
{
    if (c.type == 1) // MMAS only
    {
        for (int i = 0; i < c.numCells; ++i)
            for (int j = 0; j < c.valuesPerCell; ++j)
            {
                if (c.pher[i][j] < c.tauMin) c.pher[i][j] = c.tauMin;
                else if (c.pher[i][j] > c.tauMax) c.pher[i][j] = c.tauMax;
            }
    }
}

// Helper function to check if two boards have identical fixed cells
// Used for grouping identical solutions in entropy calculation
static bool BoardsEqual(const Board& a, const Board& b)
{
    if (a.CellCount() != b.CellCount())
        return false;
    
    for (int i = 0; i < a.CellCount(); ++i)
    {
        const ValueSet& cellA = a.GetCell(i);
        const ValueSet& cellB = b.GetCell(i);
        
        // Both must be fixed or both unfixed
        if (cellA.Fixed() != cellB.Fixed())
            return false;
        
        // If fixed, values must match
        if (cellA.Fixed() && cellA.Index() != cellB.Index())
            return false;
    }
    
    return true;
}

// Shannon entropy of solution distribution for a colony
// E(Pt) = -Σ P_i(t) log P_i(t)
// where P_i(t) = n_i / M (proportion of ants that chose solution i)
// M = total number of ants
// m = number of distinct solutions
// n_i = number of ants that selected solution i
float MultiColonyAntSystem::ComputeEntropy(const Colony &c) const
{
    if (c.ants.empty())
        return 0.0f;
    
    int M = (int)c.ants.size();  // Total number of ants
    
    // Group solutions: count how many ants produced each distinct solution
    std::vector<Board> distinctSolutions;
    std::vector<int> solutionCounts;
    
    for (auto* ant : c.ants)
    {
        const Board& solution = ant->GetSolution();
        
        // Check if this solution already exists
        bool found = false;
        for (size_t i = 0; i < distinctSolutions.size(); ++i)
        {
            if (BoardsEqual(solution, distinctSolutions[i]))
            {
                solutionCounts[i]++;
                found = true;
                break;
            }
        }
        
        // If new distinct solution, add it
        if (!found)
        {
            distinctSolutions.push_back(Board(solution));  // Make a copy
            solutionCounts.push_back(1);
        }
    }
    
    // Compute entropy: E = -Σ P_i log(P_i)
    // where P_i = n_i / M
    double H = 0.0;
    for (size_t i = 0; i < distinctSolutions.size(); ++i)
    {
        double p = (double)solutionCounts[i] / (double)M;
        if (p > 0.0)
        {
            H -= p * std::log2(p);
        }
    }
    
    return (float)H;
}

// Cooperative game allocation among ACS colonies
void MultiColonyAntSystem::ACSCooperativeGameAllocate(std::vector<int> &acsIdx,
                                                      std::vector<float> &allocatedBestPher)
{
    if (acsIdx.empty()) return;
    // total payoff b = sum of per-ACS pheromone revenues (use PherAdd from bestVal)
    double b = 0.0;
    int minLen = (std::numeric_limits<int>::max)();
    std::vector<int> lengths; lengths.reserve(acsIdx.size());
    std::vector<float> entropies; entropies.reserve(acsIdx.size());
    float emax = 0.0f;
    for (int idx : acsIdx)
    {
        int len = colonies[idx].numCells - colonies[idx].bestVal; // lower is better
        lengths.push_back(len);
        if (len < minLen) minLen = len;
        float add = PherAdd(colonies[idx].numCells, colonies[idx].bestVal);
        b += add;
        float e = ComputeEntropy(colonies[idx]);
        entropies.push_back(e);
        if (e > emax) emax = e;
    }
    // contributions
    double sumContr = 0.0;
    std::vector<double> contr(acsIdx.size(), 0.0);
    for (size_t k = 0; k < acsIdx.size(); ++k)
    {
        double soli = (lengths[k] > 0 ? ((double)minLen / (double)lengths[k]) : 1.0);
        double Hi = (emax > 0.0f ? (entropies[k] / emax) : 0.0);
        contr[k] = soli * Hi;
        sumContr += contr[k];
    }
    // allocate new pheromone revenue per ACS
    for (size_t k = 0; k < acsIdx.size(); ++k)
    {
        double ihat = (sumContr > 0.0 ? contr[k] / sumContr : (1.0 / (double)acsIdx.size()));
        allocatedBestPher[acsIdx[k]] = (float)(ihat * b);
    }
}

// Mix ACS pheromone with MMAS pheromone when ACS entropy below fixed threshold 
void MultiColonyAntSystem::ApplyPheromoneFusion(const std::vector<int> &acsIdx,
                                                const std::vector<int> &mmasIdx)
{
    if (acsIdx.empty() || mmasIdx.empty()) return;

    const Colony &mmasColony = colonies[mmasIdx[0]];
    float eMMAS = ComputeEntropy(mmasColony);
    
    int nc = mmasColony.numCells;
    int vp = mmasColony.valuesPerCell;
    
    float eThresh = entropyThreshold;

    for (int cidx : acsIdx)
    {
        float eACS = ComputeEntropy(colonies[cidx]);
        if (eACS < eThresh)
        {
           // Equation 17: Wi = E(ACS) / (E(ACS) + E(MMAS))
            float totalE = eACS + eMMAS;
            float mix = (totalE > 0.0f ? (eACS / totalE) : 0.0f);

            // Equation 16: ph_acs = (1 - mix) * ph_acs + mix * ph_mmas
            for (int i = 0; i < nc; ++i)
            {
                for (int j = 0; j < vp; ++j)
                {
                    colonies[cidx].pher[i][j] = (1.0f - mix) * colonies[cidx].pher[i][j] + 
                                                mix * mmasColony.pher[i][j];
                }
            }
        }
    }
}

// Recommend public paths from ACS to MMAS when MMAS convergence is slow
// Note: mmasIdx should already contain only low-convergence-speed colonies
void MultiColonyAntSystem::ApplyPublicPathRecommendation(int iter,
                                                         const std::vector<int> &acsIdx,
                                                         const std::vector<int> &mmasIdx)
{
    if (acsIdx.empty() || mmasIdx.empty()) return;
    // Build public assignments: intersection of ACS best solutions' fixed cells
    int nc = colonies[acsIdx[0]].numCells;
    int vp = colonies[acsIdx[0]].valuesPerCell;
    std::vector<int> publicIdx(nc, -1);

    for (int cell = 0; cell < nc; ++cell)
    {
        bool allAgree = true;
        int agreeIdx = -1;
        for (size_t k = 0; k < acsIdx.size(); ++k)
        {
            const Board &b = colonies[acsIdx[k]].bestSol;
            if (!b.GetCell(cell).Fixed()) { allAgree = false; break; }

            int idx = b.GetCell(cell).Index();
            if (k == 0) agreeIdx = idx;
            else if (idx != agreeIdx) { allAgree = false; break; }
        }
        if (allAgree) publicIdx[cell] = agreeIdx;
    }

    // reinforcement amount: 1 / (n * e^iter) (paper constant)
    float n = (float)nc;
    float tauPub = std::exp(-(float)iter) / n;

    // Apply public path recommendation to all MMAS colonies (already filtered by convergence speed)
    Colony &mmasColony = colonies[mmasIdx[0]];
    for (int cell = 0; cell < nc; ++cell)
    {
        int idx = publicIdx[cell];
        if (idx >= 0)
        {
            mmasColony.pher[cell][idx] += tauPub;
        }
    }
    ClampPheromone(mmasColony);
}