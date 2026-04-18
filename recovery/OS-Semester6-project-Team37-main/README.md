# SimOS — OS Scheduler Simulation

A simulation of three CPU scheduling algorithms (Round Robin, HRRN, MLFQ) with memory management, disk swapping, and mutex synchronization. Runs in standalone terminal mode or with a GUI via a built-in HTTP server.

---

## Prerequisites

- **GCC** (any recent version)
- **Make**
- macOS or Linux (Windows users: use WSL)

---

## Quick Start

```bash
# 1. Clone / download the project
# 2. Open a terminal in the project folder

# Build
make all

# Run (Round Robin, terminal mode)
printf '1\n5\ntest.txt\nHelloWorld\ntest.txt\n' | ./bin/scheduler --standalone 2
```

That's it — you'll see the full simulation with queues, memory dumps, and final stats.

---

## Build

```bash
make all       # compiles to bin/scheduler
make clean     # removes bin/ directory
```

No external dependencies — just standard C and POSIX sockets.

---

## How to Run

### Option 1: Terminal Mode (Standalone)

```bash
./bin/scheduler --standalone <algorithm>
```

| Algorithm | Flag | Description |
|-----------|------|-------------|
| HRRN | `1` | Highest Response Ratio Next (non-preemptive) |
| RR | `2` | Round Robin (quantum = 2) |
| MLFQ | `3` | Multi-Level Feedback Queue (4 levels: q=1,2,4,8) |

**Examples:**

```bash
# Round Robin
./bin/scheduler --standalone 2

# HRRN
./bin/scheduler --standalone 1

# MLFQ
./bin/scheduler --standalone 3
```

The simulation will prompt you 5 times for input. Enter these values in order:

```
1           ← P1's x (start of range)
5           ← P1's y (end of range → prints 1 2 3 4 5)
test.txt    ← P2's a (filename to write to)
HelloWorld  ← P2's b (data to write)
test.txt    ← P3's a (filename to read back)
```

**Or pipe them all at once (no typing needed):**

```bash
printf '1\n5\ntest.txt\nHelloWorld\ntest.txt\n' | ./bin/scheduler --standalone 2
```

#### What you'll see

Each clock cycle prints:
- Which process is running and what instruction it's executing
- The ready/blocked queue state
- The full 40-word memory layout
- Any swap in/out events with disk state

At the end, a stats table shows burst time, waiting time, and turnaround time for each process.

---

### Option 2: GUI Mode (Server + Frontend)

#### Step 1 — Start the backend

```bash
# Default: Round Robin
./bin/scheduler

# Or pick an algorithm:
./bin/scheduler 1    # HRRN
./bin/scheduler 2    # RR
./bin/scheduler 3    # MLFQ
```

Server starts on **http://localhost:8080**.

#### Step 2 — Open the frontend

```bash
open frontend/index.html       # macOS
xdg-open frontend/index.html   # Linux
```

Or just double-click `frontend/index.html` in your file manager.

#### Using the GUI

- **STEP** — advance one clock cycle
- **RUN** — auto-step continuously
- **RESET** — restart the simulation
- Change algorithm and quantum from the top controls
- When a process needs input, a dialog pops up — type a value and hit SUBMIT

---

## Project Structure

```
Scheduler/
├── Makefile
├── README.md
├── programs/
│   ├── program1.txt    # Reads 2 numbers, prints range between them
│   ├── program2.txt    # Reads filename + data, writes to file
│   └── program3.txt    # Reads filename, reads file, prints contents
├── src/
│   ├── SchedulerOS.c   # Main OS loop, scheduling algorithms, HTTP server
│   ├── memory.c/.h     # Memory allocation, swapping, PCB sync
│   ├── interpreter.c/.h # Instruction execution engine
│   ├── mutex.c/.h      # semWait / semSignal with wait queues
│   └── syscalls.c/.h   # I/O: print, input, readFile, writeFile
└── frontend/
    ├── index.html
    ├── css/
    └── js/
```

---

## The Three Programs

| Program | What it does | Mutexes used |
|---------|-------------|--------------|
| **P1** (arrives tick 0) | Reads x and y, prints all numbers from x to y | userInput, userOutput |
| **P2** (arrives tick 1) | Reads a filename and data, writes data to file | userInput, file |
| **P3** (arrives tick 4) | Reads a filename, reads file contents, prints them | userInput, file, userOutput |

---

## API Endpoints (for GUI / testing)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/api/state` | Returns full simulation state as JSON |
| `POST` | `/api/step` | Advances one tick. Body: `{"input":"value"}` (optional) |
| `POST` | `/api/reset` | Resets simulation. Body: `{"algo":1\|2\|3, "quantum":N}` (optional) |
