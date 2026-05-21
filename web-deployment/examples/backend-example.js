// Example Backend API Server
// This is a minimal working example for quick testing

const express = require('express');
const { spawn } = require('child_process');
const path = require('path');
const cors = require('cors');

const app = express();
app.use(cors());
app.use(express.json());

// Simple solver wrapper
function solveSudoku(puzzle, options = {}) {
    return new Promise((resolve, reject) => {
        // Path to your compiled solver executable
        // Adjust this path to match your setup
        const solverPath = path.join(__dirname, '../../vs2017/x64/Release/sudoku_ants.exe');
        
        const args = [
            '--puzzle', puzzle,
            '--alg', (options.algorithm || 2).toString(),
            '--timeout', (options.timeout || 120).toString(),
            '--subcolonies', (options.subcolonies || 4).toString(),
            '--ants', (options.ants || 30).toString(),
            '--verbose'
        ];

        const solver = spawn(solverPath, args);
        
        let stdout = '';
        let stderr = '';

        solver.stdout.on('data', (data) => {
            stdout += data.toString();
        });

        solver.stderr.on('data', (data) => {
            stderr += data.toString();
        });

        solver.on('close', (code) => {
            // Simple parsing - extract solution from output
            const lines = stdout.split('\n');
            let solution = '';
            let time = 0;
            let iterations = 0;
            let inSolution = false;

            for (const line of lines) {
                if (line.trim().startsWith('Solution:')) {
                    inSolution = true;
                    continue;
                }
                if (inSolution && line.trim().length > 0) {
                    if (line.includes('solved in')) {
                        const match = line.match(/solved in ([\d.]+)/);
                        if (match) time = parseFloat(match[1]);
                        inSolution = false;
                        continue;
                    }
                    solution += line + '\n';
                }
                if (line.includes('iterations:')) {
                    const match = line.match(/iterations: (\d+)/);
                    if (match) iterations = parseInt(match[1]);
                }
            }

            resolve({
                success: code === 0,
                solution: solution.trim(),
                time: time,
                iterations: iterations,
                rawOutput: stdout
            });
        });

        solver.on('error', (error) => {
            reject(new Error(`Solver error: ${error.message}`));
        });
    });
}

// API endpoint
app.post('/api/solve', async (req, res) => {
    try {
        const { puzzle, algorithm, timeout, params } = req.body;

        if (!puzzle) {
            return res.status(400).json({ error: 'Puzzle string required' });
        }

        const result = await solveSudoku(puzzle, {
            algorithm: algorithm || 2,
            timeout: timeout || 120,
            subcolonies: params?.subcolonies || 4,
            ants: params?.ants || 30
        });

        res.json(result);
    } catch (error) {
        res.status(500).json({ error: error.message });
    }
});

app.get('/api/health', (req, res) => {
    res.json({ status: 'ok', message: 'SudoPHASE API is running' });
});

const PORT = process.env.PORT || 3001;
app.listen(PORT, () => {
    console.log(`SudoPHASE API server running on http://localhost:${PORT}`);
    console.log(`Health check: http://localhost:${PORT}/api/health`);
});


















