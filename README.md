SudoSoLVRR: A Web-based Sudoku Solver using Multithreaded Multi-Colony Ant Optimization with Ring and Random Communication Topologies

This framework combines constraint propagation and multi-colony ant optimization with a dynamic collaborative mechanism (DCM-ACO) in a multithreaded environment. Heterogeneous ACS and MMAS colonies are used to balance exploitation and exploration. This multi-colony ACO is executed inside each thread, where threads exchange their iteration-best solutions and best-so-far solutions using ring and random communication topologies, respectively. 

PREREQUISITES

To compile the C++ engine and run the web visualizer, your system must have the following installed: 

1. Microsoft C++ Build Tools 
If you do not already have Visual Studio 2022 installed, you need to download the lightweight C++ compiler:
Download the Visual Studio 2022 Build Tools directly from Microsoft.
Run the installer.
When the installer opens, check the box for "Desktop development with C++".
Click Install. 
2. Python 3.8+
Download and install Python.
During installation, ensure you check the box that says "Add Python to PATH" at the bottom of the window before clicking "Install Now."

BUILD THE SOLVER

The core solver is a high-performance C++ application that must be compiled into an executable (.exe) file before the web app can use it. An automated script (Solver_Build.bat) is provided to handle the configuration and compilation automatically through the Windows console. 
Open your standard Windows Command Prompt (Press the Win key, type cmd, and press Enter). 

Navigate to the root folder of this project using the cd command. 
For example: 
cd C:\path\to\your\project

Run the automated build script by typing its name into the console and pressing Enter: 
Type this:
	Solver_Build.bat

The script will open the Visual Studio environment and start compiling the source code. Wait for the process to finish. 

Once compilation finishes, look at the bottom of the console window. If the build is successful, you will see a message saying SUCCESS: The C++ engine was compiled successfully!. You can press any key to return to your normal command prompt. 

TEST THE SOLVER

Before launching the web app, it is highly recommended to test the compiled C++ executable directly in the terminal to ensure it works on your system.
 
Keep your Command Prompt open in the project's root folder. 
Run the executable by pasting the following command. Make sure to provide a valid puzzle instance file: 
For example: 

.\vs2017\x64\Release\sudoku_ants.exe --file instances\general\inst25x25_45_97.txt --alg 4 --ants 3 --q0 0.9 --xi 0.1 --rho 0.99 --evap 0.0125 --numacs 6 --convthreshold 0.4 --entropythreshold 1.39 --threads 5 --comm-threshold 200 --comm-early-interval 120 --comm-late-interval 5  --verbose

	Parameters
--alg (Default: 0)
Core algorithm selection (Options: 0, 3, 4).
--timeout (Default: -1)
Execution timeout in seconds (-1 enables auto-select based on puzzle size).
--verbose (Default: 0)
Set to 1 to enable detailed console output during execution.
--showinitial (Default: 0)
Set to 1 to display the board state immediately after the initial Constraint Propagation.
Ant Colony Optimization (ACO) Parameters (Applicable to algorithms 0, 3, and 4)
--ants (Default: 3)
Number of ants deployed per colony.
--q0 (Default: 0.9)
Balances exploitation vs. exploration (Range: 0.0 - 1.0).
--rho (Default: 0.9)
Pheromone persistence rate (Range: 0.0 - 1.0).
--evap (Default: 0.005)
Evaporation rate for Best Value Pheromone.
--xi (Default: 0.1)
Local Pheromone Update rate.

Parallel & Inter-Thread Communication (Applicable to algorithm 4)
--threads (Default: 3)
Number of parallel threads to execute.
--threads-comm (Default: 1)
1 = Enable inter-thread communication topologies; 0 = Disable.
--comm-threshold (Default: 200)
The specific iteration count that triggers the switch from the early to late communication interval.
--comm-early-interval (Default: 100)
How often threads synchronize and communicate during early iterations.
--comm-late-interval (Default: 10)
How often threads synchronize and communicate during late-stage convergence.
Multi-Colony (MMAS + ACS) Parameters (Applicable to algorithms 3 and 4)
--numacs (Default: 2)
The number of independent Ant Colony System (ACS) colonies.
--numcolonies (Default: 3)
Total colonies per thread (Calculated as ACS colonies + 1 MMAS colony).
--convthreshold (Default: 0.8)
Threshold trigger for the public path recommendation mechanic.
--entropythreshold (Default: 1.47)
Threshold trigger for initiating pheromone fusion.

Note on Puzzle Instance Files (--file) To run a specific Sudoku puzzle rather than a default test, you must use the --file argument followed by the path to the text file containing your puzzle grid.
Directory Structure: Ensure your puzzle text files are stored in the correct directory relative to where you execute the command. For the example command above to work, you must have an instances\general\ folder located in your project root containing the inst25x25_45_97.txt file.

You should immediately see the engine's initialization text, followed by the algorithm printing its "global-global best" solutions to the console as it runs. 

SET UP THE WEB APPLICATION

In your Command Prompt, navigate into the web application directory by running: 

	cd webapp

Install all required Python dependencies automatically by running: 

pip install -r requirements.txt

Start the Flask server by running: 

python app.py --public

Open your preferred web browser (Chrome, Edge, Firefox). 

In the address bar, navigate to: 

http://localhost:5000


