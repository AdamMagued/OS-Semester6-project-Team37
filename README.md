# SimOS — OS Scheduler Simulation

## Build

```bash
make all
```

---

## Option 1: Standalone Mode (Terminal Only — No Frontend)

Runs the full simulation in the terminal, printing queues, memory, instructions, and a final stats table. User input is read from stdin.

```bash
# Round Robin (default quantum = 2)
./bin/scheduler --standalone 2

# HRRN (non-preemptive)
./bin/scheduler --standalone 1

# MLFQ (4-level feedback queue)
./bin/scheduler --standalone 3
```

The simulation will prompt you to enter values when a process executes `assign x input`. Type a value and press Enter.

**Example inputs for a full run:**
```
1           ← P1 variable x
5           ← P1 variable y (P1 prints numbers 1 to 5)
test.txt    ← P2 variable a (filename)
HelloWorld  ← P2 variable b (data written to file)
test.txt    ← P3 variable a (filename to read back)
```

---

## Option 2: Server + Frontend (GUI)

### Step 1 — Start the backend server

```bash
# Default algorithm: RR
./bin/scheduler

# Or specify an algorithm:
./bin/scheduler 1    # HRRN
./bin/scheduler 2    # RR
./bin/scheduler 3    # MLFQ
```

The server starts on **http://localhost:8080**.

### Step 2 — Open the frontend

Open `frontend/index.html` in any browser (just double-click the file, or):

```bash
open frontend/index.html
```

The GUI connects to the backend automatically. Use **STEP** to advance one clock cycle, **RUN** for continuous execution, and **RESET** to restart. The algorithm and quantum can be changed from the controls bar.

When a process needs user input, a modal dialog will appear — type a value and click SUBMIT.

---

## Clean

```bash
make clean
```
