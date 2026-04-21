// SchedulerOS.c
// OS scheduler simulation with built-in HTTP/1.1 server.
//
// Usage:
//   ./bin/scheduler [1|2|3]              server mode on :8080
//   ./bin/scheduler --standalone [1|2|3] run full simulation, print to stdout

#include "memory.h"
#include "mutex.h"
#include "interpreter.h"
#include "syscalls.h"
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <signal.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <errno.h>

// constants
#define HTTP_PORT       8080
#define MAX_LOG         200
#define LOG_EVENT_LEN   256
#define LOG_TYPE_LEN    32
#define JSON_BUF_SIZE   (192 * 1024)
#define HTTP_RX_BUF     8192

// log entry
typedef struct {
    int clock;
    int pid;           // -1 for system-level events
    char event[LOG_EVENT_LEN];
    char type[LOG_TYPE_LEN]; // scheduling|blocking|memory-swap|execution|finish
} LogEntry;

// -------- global simulation state --------

static LogEntry logEntries[MAX_LOG];
static int logCount = 0;

static int tick = 0;
static int algo = 2;              // 1=HRRN 2=RR 3=MLFQ
static int quantum = 2;           // RR time quantum (runtime-configurable)
static int simFinished = 0;
static int runningPid = -1;       // PID shown in RUNNING card

static int n = 3;
static const char *programFiles[MAX_PROCESSES];
static int arrivalTimes[MAX_PROCESSES];

static Process processes[MAX_PROCESSES];
static SchedulerInfo processList[MAX_PROCESSES];

// RR / HRRN queues
static int readyQueue[MAX_PROCESSES];
static int readyQueueSize = 0;
static int blockedQueue[MAX_PROCESSES];
static int blockedQueueSize = 0;
static int currentRunning = -1;
static int numberOfInstructionsRan = 0; // for RR quantum tracking

// MLFQ queues
static int queue1[MAX_PROCESSES], queue2[MAX_PROCESSES];
static int queue3[MAX_PROCESSES], queue4[MAX_PROCESSES];
static int queue1Size = 0, queue2Size = 0, queue3Size = 0, queue4Size = 0;
static int processQueueLevel[MAX_PROCESSES]; // 0=Q1 1=Q2 2=Q3 3=Q4, indexed by pid-1
static int prevMLFQRunner = -1; // tracks who ran LAST tick (for waiting time)

// JSON serialisation buffer
static char jsonBuf[JSON_BUF_SIZE];

// -------- forward declarations --------

bool isFinished(Process procs[], int count);
int selectHRRN(int rq[], int rqsz, SchedulerInfo pl[], Process procs[]);
void removeFromQueue(int q[], int *sz, int pid);
void demoteProcess(int currentQueue[], int nextQueue[], int *currentQueueSize, int *nextQueueSize, int pql[]);
void rotateQueue4(int q4[], int *q4sz);
void printQueues(int rq[], int rqsz, int bq[], int bqsz, int cr);
void printMLFQQueues(int q1[], int q1sz, int q2[], int q2sz,
                     int q3[], int q3sz, int q4[], int q4sz,
                     int bq[], int bqsz, int cr);
int ensureInMemory(int pid, Process procs[], int count, int cr);

// -------- log helpers --------

static void addLog(int pid, const char *event, const char *type) {
    if (logCount >= MAX_LOG) {
        // ring buffer: drop oldest entry
        memmove(&logEntries[0], &logEntries[1], (MAX_LOG - 1) * sizeof(LogEntry));
        logCount = MAX_LOG - 1;
    }
    logEntries[logCount].clock = tick;
    logEntries[logCount].pid = pid;
    strncpy(logEntries[logCount].event, event, LOG_EVENT_LEN - 1);
    logEntries[logCount].event[LOG_EVENT_LEN - 1] = '\0';
    strncpy(logEntries[logCount].type, type, LOG_TYPE_LEN - 1);
    logEntries[logCount].type[LOG_TYPE_LEN - 1] = '\0';
    logCount++;
}

// -------- needs-input check --------
// Returns the PID of a process whose next instruction is "assign ... input"
// while the input queue is empty. Returns -1 if no input is needed.

static int checkNeedsInput(void) {
    if (!sys_inputQueueEmpty()) return -1;

    // determine which PID will execute next
    int nextPid = -1;
    if (algo == 3) {
        if      (queue1Size > 0) nextPid = queue1[0];
        else if (queue2Size > 0) nextPid = queue2[0];
        else if (queue3Size > 0) nextPid = queue3[0];
        else if (queue4Size > 0) nextPid = queue4[0];
    } else if (algo == 2) {
        if (readyQueueSize > 0) nextPid = readyQueue[0];
    } else if (algo == 1) {
        nextPid = currentRunning;
        // if no one is running, check all ready processes
        if (nextPid == -1) {
            for (int k = 0; k < readyQueueSize; k++) {
                int pid = readyQueue[k];
                Process *pp = &processes[pid - 1];
                if (!pp->isCreated || pp->isSwappedOut) continue;
                int ppc = pp->pcb.programCounter;
                if (ppc >= pp->codeLineCount) continue;
                const char *ins = pp->codeLines[ppc];
                if (strncmp(ins, "assign ", 7) == 0) {
                    const char *r = ins + 7;
                    while (*r && *r != ' ' && *r != '\t') r++;
                    while (*r == ' ' || *r == '\t') r++;
                    if (strcmp(r, "input") == 0) { nextPid = pid; break; }
                }
            }
            if (nextPid != -1) return nextPid;
        }
    }
    if (nextPid <= 0) return -1;

    Process *p = &processes[nextPid - 1];
    if (!p->isCreated) return -1;
    if (strcmp(p->pcb.state, "FINISHED") == 0) return -1;

    int pc = p->pcb.programCounter;
    if (pc >= p->codeLineCount) return -1;

    // check if the instruction is "assign <var> input"
    const char *instr = p->codeLines[pc];
    if (strncmp(instr, "assign ", 7) != 0) return -1;
    const char *q = instr + 7;
    while (*q && *q != ' ' && *q != '\t') q++;  // skip var name
    while (*q == ' ' || *q == '\t') q++;          // skip whitespace
    if (strcmp(q, "input") == 0) return nextPid;
    return -1;
}

// -------- JSON helpers --------

static void jsonEsc(const char *src, char *dst, int cap) {
    int j = 0;
    for (int i = 0; src[i] && j < cap - 3; i++) {
        unsigned char c = (unsigned char)src[i];
        if      (c == '"')  { dst[j++] = '\\'; dst[j++] = '"';  }
        else if (c == '\\') { dst[j++] = '\\'; dst[j++] = '\\'; }
        else if (c == '\n') { dst[j++] = '\\'; dst[j++] = 'n';  }
        else if (c == '\r') { dst[j++] = '\\'; dst[j++] = 'r';  }
        else if (c == '\t') { dst[j++] = '\\'; dst[j++] = 't';  }
        else if (c < 0x20)  { /* skip other control chars */ }
        else                { dst[j++] = (char)c; }
    }
    dst[j] = '\0';
}

static void strLower(const char *src, char *dst, int cap) {
    int i;
    for (i = 0; src[i] && i < cap - 1; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

// returns the PID that owns memory slot idx, or -1 if free
static int slotPid(int idx) {
    for (int i = 0; i < n; i++) {
        Process *p = &processes[i];
        if (!p->isCreated || p->isSwappedOut) continue;
        if (strcmp(p->pcb.state, "FINISHED") == 0) continue;
        if (idx >= p->pcb.lowerBound && idx <= p->pcb.upperBound)
            return p->id;
    }
    return -1;
}

// returns the mutex resource name that pid is blocked on, or "" if none
static const char *blockedOn(int pid) {
    for (int i = 0; i < NUM_MUTEXES; i++)
        for (int j = 0; j < mutexes[i].waitCount; j++)
            if (mutexes[i].waitQueue[j] == pid)
                return mutexes[i].name;
    return "";
}

// -------- serialize state to JSON --------

char *serializeState(void) {
    int pos = 0;
    char e1[512], e2[512], st[32];

#define AP(fmt, ...) do { \
    int _n = snprintf(jsonBuf + pos, JSON_BUF_SIZE - pos, fmt, ##__VA_ARGS__); \
    if (_n > 0 && _n < JSON_BUF_SIZE - pos) pos += _n; \
} while(0)

    const char *algoStr = (algo == 1) ? "HRRN" :
                          (algo == 2) ? "RR"   : "MLFQ";
    int tsSlice = quantum;

    AP("{");
    AP("\"clock\":%d,", tick);
    AP("\"algorithm\":\"%s\",", algoStr);
    AP("\"timeSlice\":%d,", tsSlice);

    if (runningPid != -1)
        AP("\"runningPid\":%d,", runningPid);
    else
        AP("\"runningPid\":null,");

    // needsInput — tells frontend to prompt user
    {
        int nip = checkNeedsInput();
        if (nip != -1)
            AP("\"needsInput\":true,\"needsInputPid\":%d,", nip);
        else
            AP("\"needsInput\":false,\"needsInputPid\":null,");
    }

    // readyQueue
    AP("\"readyQueue\":[");
    {
        int first = 1;
        if (algo == 3) {
            // MLFQ: merge all levels, Q1 (highest) first
            int *qs[4] = { queue1, queue2, queue3, queue4 };
            int  sz[4] = { queue1Size, queue2Size, queue3Size, queue4Size };
            for (int qi = 0; qi < 4; qi++)
                for (int k = 0; k < sz[qi]; k++) {
                    if (!first) AP(",");
                    AP("%d", qs[qi][k]);
                    first = 0;
                }
        } else {
            for (int k = 0; k < readyQueueSize; k++) {
                if (!first) AP(",");
                AP("%d", readyQueue[k]);
                first = 0;
            }
        }
    }
    AP("],");

    // blockedQueue
    AP("\"blockedQueue\":[");
    for (int k = 0; k < blockedQueueSize; k++) {
        if (k > 0) AP(",");
        int pid = blockedQueue[k];
        jsonEsc(blockedOn(pid), e1, sizeof(e1));
        AP("{\"pid\":%d,\"resource\":\"%s\"}", pid, e1);
    }
    AP("],");

    // processes
    AP("\"processes\":[");
    {
        int first = 1;
        for (int i = 0; i < n; i++) {
            Process *p = &processes[i];
            if (!p->isCreated) continue;
            if (!first) AP(",");
            first = 0;

            strLower(p->pcb.state, st, sizeof(st));
            int finished = (strcmp(p->pcb.state, "FINISHED") == 0);
            int memS = finished ? 0 : p->pcb.lowerBound;
            int memE = finished ? 0 : p->pcb.upperBound;

            AP("{");
            AP("\"pid\":%d,",         p->id);
            AP("\"state\":\"%s\",",   st);
            AP("\"pc\":%d,",          p->pcb.programCounter);
            AP("\"memStart\":%d,",    memS);
            AP("\"memEnd\":%d,",      memE);

            // queueLevel — MLFQ only
            if (algo == 3)
                AP("\"queueLevel\":%d,", processQueueLevel[i] + 1);
            else
                AP("\"queueLevel\":null,");

            AP("\"waitingTime\":%d,", processList[i].waitingTime);

            // responseRatio — HRRN only: (W + BT) / BT with BT = total burst
            if (algo == 1) {
                int wt = processList[i].waitingTime;
                int bt = p->codeLineCount;
                if (bt <= 0) bt = 1;
                float rr = (float)(wt + bt) / bt;
                AP("\"responseRatio\":%.2f,", rr);
            } else {
                AP("\"responseRatio\":null,");
            }

            // vars — read from RAM or disk
            AP("\"vars\":[");
            {
                int fv = 1;
                if (!p->isSwappedOut && !finished) {
                    int vs = p->pcb.lowerBound + p->codeLineCount;
                    for (int j = 0; j < MAX_VARS; j++) {
                        MemoryWord *w = &memory[vs + j];
                        if (w->name[0] == '\0') continue;
                        if (!fv) AP(",");
                        jsonEsc(w->name,  e1, sizeof(e1));
                        jsonEsc(w->value, e2, sizeof(e2));
                        AP("{\"name\":\"%s\",\"value\":\"%s\"}", e1, e2);
                        fv = 0;
                    }
                } else if (p->isSwappedOut) {
                    for (int di = 0; di < MAX_PROCESSES; di++) {
                        if (!disk[di].isOccupied || disk[di].processId != p->id) continue;
                        int vb = p->codeLineCount;
                        for (int j = 0; j < MAX_VARS && vb + j < disk[di].blockSize; j++) {
                            MemoryWord *w = &disk[di].block[vb + j];
                            if (w->name[0] == '\0') continue;
                            if (!fv) AP(",");
                            jsonEsc(w->name,  e1, sizeof(e1));
                            jsonEsc(w->value, e2, sizeof(e2));
                            AP("{\"name\":\"%s\",\"value\":\"%s\"}", e1, e2);
                            fv = 0;
                        }
                        break;
                    }
                }
            }
            AP("],");

            // instructions
            AP("\"instructions\":[");
            for (int j = 0; j < p->codeLineCount; j++) {
                if (j > 0) AP(",");
                jsonEsc(p->codeLines[j], e1, sizeof(e1));
                AP("\"%s\"", e1);
            }
            AP("],");

            // heldMutexes
            AP("\"heldMutexes\":[");
            {
                int fm = 1;
                for (int mi = 0; mi < NUM_MUTEXES; mi++) {
                    if (mutexes[mi].owner != p->id) continue;
                    if (!fm) AP(",");
                    AP("\"%s\"", mutexes[mi].name);
                    fm = 0;
                }
            }
            AP("]}");
        }
    }
    AP("],");

    // memory
    AP("\"memory\":[");
    for (int i = 0; i < MEMORY_SIZE; i++) {
        if (i > 0) AP(",");
        int pid = slotPid(i);
        jsonEsc(memory[i].name,  e1, sizeof(e1));
        jsonEsc(memory[i].value, e2, sizeof(e2));
        if (pid != -1)
            AP("{\"index\":%d,\"varName\":\"%s\",\"value\":\"%s\",\"pid\":%d}",
               i, e1, e2, pid);
        else
            AP("{\"index\":%d,\"varName\":\"%s\",\"value\":\"%s\",\"pid\":null}",
               i, e1, e2);
    }
    AP("],");

    // disk
    AP("\"disk\":[");
    {
        int first = 1;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (!disk[i].isOccupied) continue;
            if (!first) AP(",");
            first = 0;
            snprintf(e1, sizeof(e1), "swapped (%d words)", disk[i].blockSize);
            jsonEsc(e1, e2, sizeof(e2));
            AP("{\"pid\":%d,\"data\":\"%s\"}", disk[i].processId, e2);
        }
    }
    AP("],");

    // log
    AP("\"log\":[");
    for (int i = 0; i < logCount; i++) {
        if (i > 0) AP(",");
        jsonEsc(logEntries[i].event, e1, sizeof(e1));
        jsonEsc(logEntries[i].type,  e2, sizeof(e2));
        AP("{\"clock\":%d,\"pid\":%d,\"event\":\"%s\",\"type\":\"%s\"}",
           logEntries[i].clock, logEntries[i].pid, e1, e2);
    }
    AP("],");

    // mutexes
    AP("\"mutexes\":{");
    for (int i = 0; i < NUM_MUTEXES; i++) {
        if (i > 0) AP(",");
        Mutex *m = &mutexes[i];
        AP("\"%s\":{", m->name);
        AP("\"locked\":%s,", m->locked ? "true" : "false");
        if (m->owner != -1)
            AP("\"heldBy\":%d,", m->owner);
        else
            AP("\"heldBy\":null,");
        AP("\"waitQueue\":[");
        for (int j = 0; j < m->waitCount; j++) {
            if (j > 0) AP(",");
            AP("%d", m->waitQueue[j]);
        }
        AP("]}");
    }
    AP("}");

    AP("}");
#undef AP
    return jsonBuf;
}

// -------- init / reset --------

static void initSimulation(void) {
    programFiles[0] = "programs/program1.txt";
    programFiles[1] = "programs/program2.txt";
    programFiles[2] = "programs/program3.txt";
    arrivalTimes[0] = 0;
    arrivalTimes[1] = 1;
    arrivalTimes[2] = 4;
    n = 3;

    initMemory();
    initMutexes();

    tick = 0;
    simFinished = 0;
    runningPid = -1;
    currentRunning = -1;
    readyQueueSize = 0;
    blockedQueueSize = 0;
    numberOfInstructionsRan = 0;
    queue1Size = queue2Size = queue3Size = queue4Size = 0;
    prevMLFQRunner = -1;
    logCount = 0;
    memset(processQueueLevel, 0, sizeof(processQueueLevel));

    for (int i = 0; i < n; i++) {
        processes[i].id = i + 1;
        processes[i].arrivalTime = arrivalTimes[i];
        processes[i].pcb.processId = i + 1;
        processes[i].pcb.programCounter = 0;
        strcpy(processes[i].pcb.state, "NEW");
        processes[i].isCreated = 0;
        processes[i].isSwappedOut = 0;
        processes[i].quantumUsed = 0;
        processes[i].codeLineCount = 0;

        processList[i].processID = i + 1;
        processList[i].arrivalTime = arrivalTimes[i];
        processList[i].burstTime = 0;
        processList[i].waitingTime = 0;
        processList[i].finishTick = 0;
    }
}

static void resetSimulation(void) {
    sys_clearInputQueue();
    // Clear shared simulation files to prevent cross-session leaks
    remove("test.txt"); 
    
    // Wait, do not reset algo and quantum here, otherwise API changes are ignored.
    
    initSimulation();
    printf("[OS] Simulation RESET performed - all states cleared.\n");
}

// -------- step simulation (one clock cycle) --------

static void stepSimulation(void) {
    if (simFinished) return;

    char evtBuf[LOG_EVENT_LEN];

    printf("\n+-----------------------------------+\n");
    printf("|         Clock Cycle: %-4d         |\n", tick);
    printf("+-----------------------------------+\n");

    // 1. update waiting times (before arrivals so they don't count)
    if (algo == 3) {
        // MLFQ: currentRunning is always -1 here (reset each tick).
        // Use prevMLFQRunner — the PID that actually RAN last tick —
        // so that we don't count its last-tick execution as waiting.
        int *qs[4] = { queue1, queue2, queue3, queue4 };
        int  sz[4] = { queue1Size, queue2Size, queue3Size, queue4Size };
        for (int qi = 0; qi < 4; qi++)
            for (int k = 0; k < sz[qi]; k++) {
                int pid = qs[qi][k];
                if (pid != prevMLFQRunner)
                    processList[pid - 1].waitingTime++;
            }
    } else {
        for (int k = 0; k < readyQueueSize; k++) {
            int pid = readyQueue[k];
            if (pid != currentRunning)
                processList[pid - 1].waitingTime++;
        }
    }

    // 2. process arrivals
    for (int j = 0; j < n; j++) {
        if (processList[j].arrivalTime != tick) continue;
        if (processes[j].isCreated) continue;

        printf(">> Process %d has ARRIVED\n", processes[j].id);

        if (loadProgram(&processes[j], programFiles[j]) < 0) {
            printf("[OS ERROR] Failed to load %s\n", programFiles[j]);
            strcpy(processes[j].pcb.state, "FINISHED");
            continue;
        }
        processList[j].burstTime = processes[j].codeLineCount;

        int result = allocateProcess(&processes[j]);
        if (result == -1) {
            printf("[OS] Not enough memory for P%d — swapping...\n", processes[j].id);
            for (int k = 0; k < n; k++) {
                if (!processes[k].isCreated) continue;
                if (processes[k].isSwappedOut) continue;
                if (processes[k].id == currentRunning) continue;
                if (strcmp(processes[k].pcb.state, "FINISHED") == 0) continue;
                snprintf(evtBuf, sizeof(evtBuf),
                         "P%d swapped out — making room for P%d",
                         processes[k].id, processes[j].id);
                swapOut(&processes[k]);
                addLog(processes[k].id, evtBuf, "memory-swap");
                result = allocateProcess(&processes[j]);
                if (result != -1) break;
            }
            if (result == -1) {
                printf("[OS ERROR] Cannot allocate memory for P%d!\n", processes[j].id);
                continue;
            }
        }

        processes[j].isCreated = 1;
        strcpy(processes[j].pcb.state, "READY");
        printf("[OS] P%d created -> memory [%d-%d] (burst:%d)\n",
               processes[j].id,
               processes[j].pcb.lowerBound,
               processes[j].pcb.upperBound,
               processes[j].codeLineCount);

        snprintf(evtBuf, sizeof(evtBuf),
                 "Process %d arrived — allocated memory [%d-%d]",
                 processes[j].id,
                 processes[j].pcb.lowerBound,
                 processes[j].pcb.upperBound);
        addLog(processes[j].id, evtBuf, "memory-swap");

        if (algo == 3) {
            queue1[queue1Size++] = processes[j].id;
            processQueueLevel[j] = 0;
        } else {
            readyQueue[readyQueueSize++] = processes[j].id;
        }
    }

    // 3. reset unblock signal
    unblockedPID = -1;

    // ======== SCHEDULING ALGORITHMS ========

    // ---- HRRN (non-preemptive) ----
    if (algo == 1) {
        if (currentRunning == -1 && readyQueueSize > 0) {
            currentRunning = selectHRRN(readyQueue, readyQueueSize, processList, processes);
            removeFromQueue(readyQueue, &readyQueueSize, currentRunning);
            strcpy(processes[currentRunning - 1].pcb.state, "RUNNING");
            runningPid = currentRunning;

            printf(">> P%d selected (HRRN)\n", currentRunning);
            snprintf(evtBuf, sizeof(evtBuf),
                     "Process %d scheduled -> RUNNING (HRRN)", currentRunning);
            addLog(currentRunning, evtBuf, "scheduling");

            printQueues(readyQueue, readyQueueSize,
                        blockedQueue, blockedQueueSize, currentRunning);
        }

        if (currentRunning != -1) {
            if (!ensureInMemory(currentRunning, processes, n, currentRunning)) {
                readyQueue[readyQueueSize++] = currentRunning;
                strcpy(processes[currentRunning - 1].pcb.state, "READY");
                currentRunning = -1;
            } else {
                int idx = currentRunning - 1;
                int pcBefore = processes[idx].pcb.programCounter;
                int execRes = executeInstruction(&processes[idx]);

                if (pcBefore < processes[idx].codeLineCount) {
                    snprintf(evtBuf, sizeof(evtBuf), "P%d: %s",
                             currentRunning, processes[idx].codeLines[pcBefore]);
                    addLog(currentRunning, evtBuf, "execution");
                }

                if (execRes == EXEC_FINISHED) {
                    strcpy(processes[idx].pcb.state, "FINISHED");
                    processList[idx].finishTick = tick + 1;
                    freeProcess(&processes[idx]);
                    printf(">> P%d FINISHED (memory freed)\n", currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d finished", currentRunning);
                    addLog(currentRunning, evtBuf, "finish");
                    runningPid = -1;
                    currentRunning = -1;
                } else if (execRes == EXEC_BLOCKED) {
                    strcpy(processes[idx].pcb.state, "BLOCKED");
                    blockedQueue[blockedQueueSize++] = currentRunning;
                    printf(">> P%d BLOCKED\n", currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d BLOCKED on %s",
                             currentRunning, blockedOn(currentRunning));
                    addLog(currentRunning, evtBuf, "blocking");
                    runningPid = -1;
                    currentRunning = -1;
                }
                printQueues(readyQueue, readyQueueSize,
                            blockedQueue, blockedQueueSize, currentRunning);
            }
        }
    }

    // ---- Round Robin ----
    else if (algo == 2) {
        if (readyQueueSize > 0) {
            currentRunning = readyQueue[0];
            strcpy(processes[currentRunning - 1].pcb.state, "RUNNING");
            runningPid = currentRunning;

            if (!ensureInMemory(currentRunning, processes, n, currentRunning)) {
                // rotate to back and try next tick
                int tmp = readyQueue[0];
                for (int p = 0; p < readyQueueSize - 1; p++)
                    readyQueue[p] = readyQueue[p + 1];
                readyQueue[readyQueueSize - 1] = tmp;
                strcpy(processes[currentRunning - 1].pcb.state, "READY");
                currentRunning = -1;
                numberOfInstructionsRan = 0;
            } else {
                printf(">> Running: P%d\n", currentRunning);
                int idx = currentRunning - 1;
                int pcBefore = processes[idx].pcb.programCounter;
                int execRes = executeInstruction(&processes[idx]);
                numberOfInstructionsRan++;

                if (pcBefore < processes[idx].codeLineCount) {
                    snprintf(evtBuf, sizeof(evtBuf), "P%d: %s",
                             currentRunning, processes[idx].codeLines[pcBefore]);
                    addLog(currentRunning, evtBuf, "execution");
                }

                if (execRes == EXEC_FINISHED) {
                    for (int p = 0; p < readyQueueSize - 1; p++)
                        readyQueue[p] = readyQueue[p + 1];
                    readyQueueSize--;
                    strcpy(processes[idx].pcb.state, "FINISHED");
                    processList[idx].finishTick = tick + 1;
                    freeProcess(&processes[idx]);
                    printf(">> P%d FINISHED\n", currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d finished", currentRunning);
                    addLog(currentRunning, evtBuf, "finish");
                    runningPid = -1;
                    currentRunning = -1;
                    numberOfInstructionsRan = 0;
                } else if (execRes == EXEC_BLOCKED) {
                    for (int p = 0; p < readyQueueSize - 1; p++)
                        readyQueue[p] = readyQueue[p + 1];
                    readyQueueSize--;
                    blockedQueue[blockedQueueSize++] = currentRunning;
                    strcpy(processes[idx].pcb.state, "BLOCKED");
                    printf(">> P%d BLOCKED\n", currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d BLOCKED on %s",
                             currentRunning, blockedOn(currentRunning));
                    addLog(currentRunning, evtBuf, "blocking");
                    runningPid = -1;
                    currentRunning = -1;
                    numberOfInstructionsRan = 0;
                } else if (numberOfInstructionsRan >= quantum) {
                    int tmp = readyQueue[0];
                    for (int p = 0; p < readyQueueSize - 1; p++)
                        readyQueue[p] = readyQueue[p + 1];
                    readyQueue[readyQueueSize - 1] = tmp;
                    strcpy(processes[idx].pcb.state, "READY");
                    printf(">> P%d quantum expired\n", currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "P%d preempted - quantum expired (RR)", currentRunning);
                    addLog(currentRunning, evtBuf, "scheduling");
                    currentRunning = -1;
                    numberOfInstructionsRan = 0;
                }
                printQueues(readyQueue, readyQueueSize,
                            blockedQueue, blockedQueueSize, currentRunning);
            }
        }
    }

    // ---- MLFQ (Multi-Level Feedback Queue) ----
    else if (algo == 3) {
        int selPID = -1, selQ = 0;
        if      (queue1Size > 0) { selPID = queue1[0]; selQ = 1; }
        else if (queue2Size > 0) { selPID = queue2[0]; selQ = 2; }
        else if (queue3Size > 0) { selPID = queue3[0]; selQ = 3; }
        else if (queue4Size > 0) { selPID = queue4[0]; selQ = 4; }

        if (selPID != -1) {
            currentRunning = selPID;
            runningPid = selPID;
            strcpy(processes[selPID - 1].pcb.state, "RUNNING");

            if (!ensureInMemory(currentRunning, processes, n, currentRunning)) {
                strcpy(processes[currentRunning - 1].pcb.state, "READY");
                currentRunning = -1;
            } else {
                printf(">> P%d running (Q%d)\n", currentRunning, selQ);
                int idx = currentRunning - 1;
                int pcBefore = processes[idx].pcb.programCounter;
                int execRes = executeInstruction(&processes[idx]);

                if (pcBefore < processes[idx].codeLineCount) {
                    snprintf(evtBuf, sizeof(evtBuf), "P%d: %s",
                             currentRunning, processes[idx].codeLines[pcBefore]);
                    addLog(currentRunning, evtBuf, "execution");
                }

                if (execRes == EXEC_FINISHED) {
                    strcpy(processes[idx].pcb.state, "FINISHED");
                    processList[idx].finishTick = tick + 1;
                    freeProcess(&processes[idx]);
                    processes[selPID - 1].quantumUsed = 0;
                    printf(">> P%d FINISHED\n", currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d finished", currentRunning);
                    addLog(currentRunning, evtBuf, "finish");
                    runningPid = -1;
                    switch (selQ) {
                        case 1: removeFromQueue(queue1, &queue1Size, selPID); break;
                        case 2: removeFromQueue(queue2, &queue2Size, selPID); break;
                        case 3: removeFromQueue(queue3, &queue3Size, selPID); break;
                        case 4: removeFromQueue(queue4, &queue4Size, selPID); break;
                    }
                } else if (execRes == EXEC_BLOCKED) {
                    strcpy(processes[idx].pcb.state, "BLOCKED");
                    blockedQueue[blockedQueueSize++] = currentRunning;
                    processes[selPID - 1].quantumUsed = 0;
                    printf(">> P%d BLOCKED\n", currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d BLOCKED on %s",
                             currentRunning, blockedOn(currentRunning));
                    addLog(currentRunning, evtBuf, "blocking");
                    runningPid = -1;
                    switch (selQ) {
                        case 1: removeFromQueue(queue1, &queue1Size, selPID); break;
                        case 2: removeFromQueue(queue2, &queue2Size, selPID); break;
                        case 3: removeFromQueue(queue3, &queue3Size, selPID); break;
                        case 4: removeFromQueue(queue4, &queue4Size, selPID); break;
                    }
                } else {
                    // EXEC_CONTINUE - check quantum expiry
                    processes[selPID - 1].quantumUsed++;

                    if (selQ == 1) {
                        // Q1 quantum = 1: always demote immediately
                        demoteProcess(queue1, queue2, &queue1Size, &queue2Size, processQueueLevel);
                        processes[selPID - 1].quantumUsed = 0;
                        printf(">> P%d demoted Q1->Q2\n", selPID);
                        snprintf(evtBuf, sizeof(evtBuf),
                                 "P%d preempted - demoted Q1->Q2", selPID);
                        addLog(selPID, evtBuf, "scheduling");
                    } else if (selQ == 2 && processes[selPID - 1].quantumUsed >= 2) {
                        demoteProcess(queue2, queue3, &queue2Size, &queue3Size, processQueueLevel);
                        processes[selPID - 1].quantumUsed = 0;
                        printf(">> P%d demoted Q2->Q3\n", selPID);
                        snprintf(evtBuf, sizeof(evtBuf),
                                 "P%d preempted - demoted Q2->Q3", selPID);
                        addLog(selPID, evtBuf, "scheduling");
                    } else if (selQ == 3 && processes[selPID - 1].quantumUsed >= 4) {
                        demoteProcess(queue3, queue4, &queue3Size, &queue4Size, processQueueLevel);
                        processes[selPID - 1].quantumUsed = 0;
                        printf(">> P%d demoted Q3->Q4\n", selPID);
                        snprintf(evtBuf, sizeof(evtBuf),
                                 "P%d preempted - demoted Q3->Q4", selPID);
                        addLog(selPID, evtBuf, "scheduling");
                    } else if (selQ == 4 && processes[selPID - 1].quantumUsed >= 8) {
                        rotateQueue4(queue4, &queue4Size);
                        processes[selPID - 1].quantumUsed = 0;
                        printf(">> P%d rotated in Q4\n", selPID);
                        snprintf(evtBuf, sizeof(evtBuf),
                                 "P%d rotated in Q4 (RR)", selPID);
                        addLog(selPID, evtBuf, "scheduling");
                    }
                }

                printMLFQQueues(queue1, queue1Size, queue2, queue2Size,
                                queue3, queue3Size, queue4, queue4Size,
                                blockedQueue, blockedQueueSize, currentRunning);
                prevMLFQRunner = currentRunning; // remember who ran for next-tick wait calc
                currentRunning = -1; // MLFQ re-selects each tick
            }
        }
    }

    // 4. handle processes unblocked by semSignal
    if (unblockedPID != -1) {
        for (int i = 0; i < blockedQueueSize; i++) {
            if (blockedQueue[i] != unblockedPID) continue;
            for (int j = i; j < blockedQueueSize - 1; j++)
                blockedQueue[j] = blockedQueue[j + 1];
            blockedQueueSize--;
            break;
        }
        strcpy(processes[unblockedPID - 1].pcb.state, "READY");
        printf(">> P%d UNBLOCKED -> ready\n", unblockedPID);
        snprintf(evtBuf, sizeof(evtBuf),
                 "Process %d unblocked -> ready queue", unblockedPID);
        addLog(unblockedPID, evtBuf, "blocking");

        if (algo == 3) {
            int lvl = processQueueLevel[unblockedPID - 1];
            switch (lvl) {
                case 0: queue1[queue1Size++] = unblockedPID; break;
                case 1: queue2[queue2Size++] = unblockedPID; break;
                case 2: queue3[queue3Size++] = unblockedPID; break;
                case 3: queue4[queue4Size++] = unblockedPID; break;
            }
            printMLFQQueues(queue1, queue1Size, queue2, queue2Size,
                            queue3, queue3Size, queue4, queue4Size,
                            blockedQueue, blockedQueueSize, -1);
        } else {
            readyQueue[readyQueueSize++] = unblockedPID;
            printQueues(readyQueue, readyQueueSize,
                        blockedQueue, blockedQueueSize, -1);
        }
    }

    // 5. sync PCBs to memory and print memory state
    for (int i = 0; i < n; i++) {
        if (processes[i].isCreated && !processes[i].isSwappedOut &&
                strcmp(processes[i].pcb.state, "FINISHED") != 0)
            syncPCBToMemory(&processes[i]);
    }
    printMemory();

    tick++;

    if (isFinished(processes, n)) {
        simFinished = 1;
        addLog(-1, "All processes finished", "finish");
        printf("\n+-----------------------------------+\n");
        printf("|     ALL PROCESSES FINISHED        |\n");
        printf("+-----------------------------------+\n");
        printf("Total clock cycles: %d\n", tick);
        printDisk();
    }
}

// -------- HTTP server --------

#ifdef _WIN32
static void writeAll(int fd, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int w = send(fd, buf + sent, len - sent, 0);
        if (w <= 0) return;
        sent += w;
    }
}
#else
static void writeAll(int fd, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int w = write(fd, buf + sent, len - sent);
        if (w <= 0) return;
        sent += w;
    }
}
#endif

static void sendResponse(int fd, int status, const char *body, int bodyLen) {
    const char *statusStr =
        (status == 200) ? "200 OK" :
        (status == 404) ? "404 Not Found" : "500 Internal Server Error";

    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        statusStr, bodyLen);

    writeAll(fd, header, hlen);
    if (bodyLen > 0) writeAll(fd, body, bodyLen);
}

// extract a simple quoted string value after key in json
static int jsonGet(const char *json, const char *key, char *out, int cap) {
    const char *p = strstr(json, key);
    if (!p) return 0;
    p = strchr(p + strlen(key), '"');
    if (!p) return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < cap - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return 1;
}

// extract a numeric value after key in json
static int jsonGetInt(const char *json, const char *key, int *out) {
    const char *p = strstr(json, key);
    if (!p) return 0;
    p = strchr(p + strlen(key), ':');
    if (!p) return 0;
    while (*p == ':' || *p == ' ') p++;
    if (*p < '0' || *p > '9') return 0;
    *out = atoi(p);
    return 1;
}

// extract an integer array after key in json (e.g. "arrivals": [0,1,4])
static int jsonGetIntArray(const char *json, const char *key, int *out, int cap) {
    const char *p = strstr(json, key);
    if (!p) return 0;
    p = strchr(p + strlen(key), '[');
    if (!p) return 0;
    p++;
    int count = 0;
    while (*p && *p != ']' && count < cap) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']' || *p == '\0') break;
        if ((*p >= '0' && *p <= '9') || *p == '-') {
            out[count++] = atoi(p);
            if (*p == '-') p++;
            while (*p >= '0' && *p <= '9') p++;
        } else {
            break;
        }
    }
    return count;
}

static void handleRequest(int fd) {
    // read request
    char req[HTTP_RX_BUF];
    int total = 0;

    while (total < HTTP_RX_BUF - 1) {
#ifdef _WIN32
        int nr = recv(fd, req + total, HTTP_RX_BUF - total - 1, 0);
#else
        int nr = read(fd, req + total, HTTP_RX_BUF - total - 1);
#endif
        if (nr <= 0) break;
        total += nr;
        req[total] = '\0';
        if (strstr(req, "\r\n\r\n")) break;
    }
    if (total == 0) return;
    req[total] = '\0';

    // parse method + path
    char method[8] = {0};
    char path[256] = {0};
    sscanf(req, "%7s %255s", method, path);

    // CORS preflight
    if (strcmp(method, "OPTIONS") == 0) {
        sendResponse(fd, 200, "", 0);
        return;
    }

    // locate request body
    char body[1024] = {0};
    char *bodyStart = strstr(req, "\r\n\r\n");
    if (bodyStart) {
        bodyStart += 4;
        int blen = total - (int)(bodyStart - req);
        if (blen > 0 && blen < (int)sizeof(body)) {
            memcpy(body, bodyStart, blen);
            body[blen] = '\0';
        }
    }

    printf("[HTTP] %s %s\n", method, path);

    // route
    if (strcmp(path, "/api/state") == 0 && strcmp(method, "GET") == 0) {
        char *json = serializeState();
        sendResponse(fd, 200, json, (int)strlen(json));
    }
    else if (strcmp(path, "/api/step") == 0 && strcmp(method, "POST") == 0) {
        // optional input value for assign ... input instructions
        char inputVal[256] = {0};
        if (jsonGet(body, "\"input\"", inputVal, sizeof(inputVal)) && inputVal[0] != '\0')
            sys_pushInput(inputVal);

        if (!simFinished)
            stepSimulation();

        char *json = serializeState();
        sendResponse(fd, 200, json, (int)strlen(json));
    }
    else if (strcmp(path, "/api/reset") == 0 && strcmp(method, "POST") == 0) {
        // optional algorithm selection: {"algo": 1|2|3}
        int newAlgo = 0;
        if (jsonGetInt(body, "\"algo\"", &newAlgo) && newAlgo >= 1 && newAlgo <= 3)
            algo = newAlgo;

        // optional quantum: {"quantum": N}
        int newQuantum = 0;
        if (jsonGetInt(body, "\"quantum\"", &newQuantum) && newQuantum >= 1 && newQuantum <= 16)
            quantum = newQuantum;

        // optional arrival times: {"arrivals": [0, 1, 4]}
        int tmpArrivals[MAX_PROCESSES];
        int nArr = jsonGetIntArray(body, "\"arrivals\"", tmpArrivals, n);
        if (nArr == n) {
            for (int i = 0; i < n; i++)
                arrivalTimes[i] = tmpArrivals[i];
        }

        resetSimulation();

        char *json = serializeState();
        sendResponse(fd, 200, json, (int)strlen(json));
    }
    else {
        const char *e404 = "{\"error\":\"not found\"}";
        sendResponse(fd, 404, e404, (int)strlen(e404));
    }
}

static void runServer(int port) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(srv, 16) < 0) {
        perror("listen"); exit(1);
    }

    const char *algoStr = (algo == 1) ? "HRRN" :
                          (algo == 2) ? "RR"   : "MLFQ";
    printf("[HTTP] SimOS server listening on http://localhost:%d\n", port);
    printf("[HTTP] Algorithm: %s   (POST /api/reset {\"algo\":1|2|3} to change)\n", algoStr);
    printf("[HTTP] GET  /api/state -> full simulation state as JSON\n");
    printf("[HTTP] POST /api/step  -> advance one clock tick\n");
    printf("[HTTP] POST /api/reset -> reset simulation\n");

    while (1) {
        int client = accept(srv, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        // receive timeout (5 seconds)
#ifdef _WIN32
        DWORD tv = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

        handleRequest(client);
        close(client);
    }

    close(srv);
#ifdef _WIN32
    WSACleanup();
#endif
}

// -------- standalone mode --------

static void runStandalone(void) {
    const char *algoStr = (algo == 1) ? "HRRN" :
                          (algo == 2) ? "RR"   : "MLFQ";
    printf("+------------------------------------------+\n");
    printf("|          SimOS - Standalone Mode          |\n");
    printf("|  Algorithm: %-5s   Quantum: %-2d           |\n",
           algoStr, quantum);
    printf("+------------------------------------------+\n\n");

    while (!simFinished) {
        stepSimulation();
    }

    // final statistics
    printf("\n+------------------------------------------+\n");
    printf("|              FINAL STATISTICS             |\n");
    printf("+------------------------------------------+\n");
    printf("%-6s %-10s %-12s %-12s\n",
           "PID", "BurstTime", "WaitingTime", "TurnaroundTime");
    printf("------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        int bt = processList[i].burstTime;
        int wt = processList[i].waitingTime;
        int ta = processList[i].finishTick - processList[i].arrivalTime;
        printf("P%-5d %-10d %-12d %-12d\n",
               processes[i].id, bt, wt, ta);
    }
    printf("------------------------------------------\n");
    printf("Total clock cycles: %d\n\n", tick);
}

// -------- main --------

int main(int argc, char *argv[]) {
    int standalone = 0;

    // parse args: --standalone [algo] or just [algo]
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--standalone") == 0) {
            standalone = 1;
        } else {
            int a = atoi(argv[i]);
            if (a >= 1 && a <= 3) algo = a;
        }
    }

    initSimulation();

    if (standalone) {
        runStandalone();
    } else {
        runServer(HTTP_PORT);
    }
    return 0;
}

// -------- helper functions --------

bool isFinished(Process procs[], int count) {
    for (int i = 0; i < count; i++)
        if (strcmp(procs[i].pcb.state, "FINISHED") != 0) return false;
    return true;
}

int selectHRRN(int rq[], int rqsz, SchedulerInfo pl[], Process procs[]) {
    float best = -1;
    int sel = rq[0];
    for (int i = 0; i < rqsz; i++) {
        int pid = rq[i];
        int wt = pl[pid - 1].waitingTime;
        int bt = procs[pid - 1].codeLineCount; // total burst
        if (bt <= 0) bt = 1;
        float rr = (float)(wt + bt) / bt;      // (W + BT) / BT
        if (rr > best) { best = rr; sel = pid; }
    }
    printf("  HRRN: P%d selected (ratio: %.2f)\n", sel, best);
    return sel;
}

void removeFromQueue(int q[], int *sz, int pid) {
    for (int i = 0; i < *sz; i++) {
        if (q[i] != pid) continue;
        for (int j = i; j < *sz - 1; j++) q[j] = q[j + 1];
        (*sz)--;
        return;
    }
}

void demoteProcess(int currentQueue[], int nextQueue[],
                   int *currentQueueSize, int *nextQueueSize,
                   int pql[]) {
    if (*currentQueueSize <= 0) return;
    int pid = currentQueue[0];
    for (int i = 0; i < *currentQueueSize - 1; i++)
        currentQueue[i] = currentQueue[i + 1];
    (*currentQueueSize)--;
    nextQueue[*nextQueueSize] = pid;
    (*nextQueueSize)++;
    if (pql[pid - 1] < 3) pql[pid - 1]++;
}

void rotateQueue4(int q4[], int *q4sz) {
    if (*q4sz <= 1) return;
    int first = q4[0];
    for (int i = 0; i < *q4sz - 1; i++) q4[i] = q4[i + 1];
    q4[*q4sz - 1] = first;
}

void printQueues(int rq[], int rqsz, int bq[], int bqsz, int cr) {
    printf("--- Queue Status ---\n");
    printf("Ready:   [ ");
    for (int i = 0; i < rqsz; i++) printf("P%d ", rq[i]);
    printf("]\nBlocked: [ ");
    if (bqsz == 0) printf("empty ");
    else for (int i = 0; i < bqsz; i++) printf("P%d ", bq[i]);
    if (cr != -1) printf("]\nRunning: P%d\n", cr);
    else          printf("]\nRunning: none\n");
    printf("--------------------\n");
}

void printMLFQQueues(int q1[], int q1sz, int q2[], int q2sz,
                     int q3[], int q3sz, int q4[], int q4sz,
                     int bq[], int bqsz, int cr) {
    printf("--- MLFQ ---\n");
    printf("Q1(1): [ "); for(int i=0;i<q1sz;i++) printf("P%d ",q1[i]); printf("]\n");
    printf("Q2(2): [ "); for(int i=0;i<q2sz;i++) printf("P%d ",q2[i]); printf("]\n");
    printf("Q3(4): [ "); for(int i=0;i<q3sz;i++) printf("P%d ",q3[i]); printf("]\n");
    printf("Q4(8): [ "); for(int i=0;i<q4sz;i++) printf("P%d ",q4[i]); printf("]\n");
    printf("Blk:   [ ");
    if (bqsz == 0) printf("empty ");
    else for(int i=0;i<bqsz;i++) printf("P%d ",bq[i]);
    printf("]\nRun:   %s\n", cr != -1 ? "" : "none");
    if (cr != -1) printf("P%d\n", cr);
    printf("------------\n");
}

int ensureInMemory(int pid, Process procs[], int count, int cr) {
    (void)cr;
    if (!procs[pid - 1].isSwappedOut) return 1;

    for (int k = 0; k < count; k++) {
        if (procs[k].isCreated && !procs[k].isSwappedOut &&
            strcmp(procs[k].pcb.state, "FINISHED") == 0) {
            printf("[OS] Freeing finished P%d memory\n", procs[k].id);
            freeProcess(&procs[k]);
            procs[k].isSwappedOut = 1;
        }
    }

    if (swapIn(&procs[pid - 1]) != -1) return 1;

    for (int k = 0; k < count; k++) {
        if (!procs[k].isCreated || procs[k].isSwappedOut) continue;
        if (procs[k].id == pid) continue;
        if (strcmp(procs[k].pcb.state, "FINISHED") == 0) continue;
        swapOut(&procs[k]);
        if (swapIn(&procs[pid - 1]) != -1) return 1;
    }

    printf("[OS ERROR] Cannot swap in P%d\n", pid);
    return 0;
}
