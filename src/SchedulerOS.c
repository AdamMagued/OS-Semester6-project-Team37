/**
 * SchedulerOS.c
 * ─────────────────────────────────────────────────────────────────
 * OS scheduler simulation with a built-in HTTP/1.1 server.
 *
 * Usage:
 *   ./bin/scheduler [1|2|3]   1=HRRN  2=RR  3=MLFQ (default)
 *
 * HTTP endpoints (all return application/json + CORS headers):
 *   GET  /api/state   → current simulation state
 *   POST /api/step    → advance one clock tick, return new state
 *                       body (optional): {"input":"<value>"}
 *   POST /api/reset   → reset simulation, return initial state
 *                       body (optional): {"algo":<1|2|3>}
 * ─────────────────────────────────────────────────────────────────
 */

#include "memory.h"
#include "mutex.h"
#include "interpreter.h"
#include "syscalls.h"
#include <stdbool.h>
#include <ctype.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

/* ═══════════════════════════ CONSTANTS ════════════════════════════ */
#define RR_QUANTUM      2
#define HTTP_PORT       8080
#define MAX_LOG         200          /* ring-buffer cap for log entries */
#define LOG_EVENT_LEN   256
#define LOG_TYPE_LEN    32
#define JSON_BUF_SIZE   (192 * 1024) /* 192 KB – more than enough */
#define HTTP_RX_BUF     8192

/* ══════════════════════════ LOG ENTRY ════════════════════════════ */
typedef struct {
    int  clock;
    int  pid;                       /* -1 for system-level events */
    char event[LOG_EVENT_LEN];
    char type[LOG_TYPE_LEN];        /* scheduling|blocking|memory-swap|execution|finish */
} LogEntry;

/* ════════════════════════ GLOBAL SIM STATE ════════════════════════ */
static LogEntry      g_log[MAX_LOG];
static int           g_logCount      = 0;

static int           g_tick          = 0;
static int           g_algo          = 3;   /* 1=HRRN 2=RR 3=MLFQ */
static int           g_simFinished   = 0;
static int           g_runningPid    = -1;  /* PID shown in RUNNING card */

static int           g_n             = 3;
static const char   *g_programFiles[MAX_PROCESSES];
static int           g_arrivalTimes[MAX_PROCESSES];

static Process       g_processes[MAX_PROCESSES];
static SchedulerInfo g_processList[MAX_PROCESSES];

/* RR / HRRN queues */
static int  g_readyQueue[MAX_PROCESSES];
static int  g_readyQueueSize   = 0;
static int  g_blockedQueue[MAX_PROCESSES];
static int  g_blockedQueueSize = 0;
static int  g_currentRunning   = -1;
static int  g_niRan            = 0;   /* instructions run this quantum (RR) */

/* MLFQ queues */
static int  g_q1[MAX_PROCESSES], g_q2[MAX_PROCESSES];
static int  g_q3[MAX_PROCESSES], g_q4[MAX_PROCESSES];
static int  g_q1sz = 0, g_q2sz = 0, g_q3sz = 0, g_q4sz = 0;
static int  g_pLevel[MAX_PROCESSES]; /* 0=Q1 1=Q2 2=Q3 3=Q4, indexed by pid-1 */

/* JSON serialisation buffer */
static char s_jsonBuf[JSON_BUF_SIZE];

/* ═══════════════════════ FORWARD DECLARATIONS ═════════════════════ */
bool isFinished(Process procs[], int n);
int  selectHRRN(int rq[], int rqsz, SchedulerInfo pl[], Process procs[]);
void removeFromQueue(int q[], int *sz, int pid);
void demoteProcess(int cq[], int nq[], int *csz, int *nsz, int pql[]);
void rotateQueue4(int q4[], int *sz);
void printQueues(int rq[], int rqsz, int bq[], int bqsz, int cr);
void printMLFQQueues(int q1[], int q1sz, int q2[], int q2sz,
                     int q3[], int q3sz, int q4[], int q4sz,
                     int bq[], int bqsz, int cr);
int  ensureInMemory(int pid, Process procs[], int n, int cr);

/* ═════════════════════════ LOG HELPERS ════════════════════════════ */

static void addLog(int pid, const char *event, const char *type) {
    if (g_logCount >= MAX_LOG) {
        /* Ring: drop oldest entry */
        memmove(&g_log[0], &g_log[1], (MAX_LOG - 1) * sizeof(LogEntry));
        g_logCount = MAX_LOG - 1;
    }
    g_log[g_logCount].clock = g_tick;
    g_log[g_logCount].pid   = pid;
    strncpy(g_log[g_logCount].event, event, LOG_EVENT_LEN - 1);
    g_log[g_logCount].event[LOG_EVENT_LEN - 1] = '\0';
    strncpy(g_log[g_logCount].type, type, LOG_TYPE_LEN - 1);
    g_log[g_logCount].type[LOG_TYPE_LEN - 1] = '\0';
    g_logCount++;
}

/* ═════════════════════════ JSON HELPERS ═══════════════════════════ */

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

/* Returns the PID that owns memory slot `idx`, or -1 if free/unknown. */
static int slotPid(int idx) {
    for (int i = 0; i < g_n; i++) {
        Process *p = &g_processes[i];
        if (!p->isCreated || p->isSwappedOut) continue;
        if (strcmp(p->pcb.state, "FINISHED") == 0) continue;
        if (idx >= p->pcb.lowerBound && idx <= p->pcb.upperBound)
            return p->id;
    }
    return -1;
}

/* Returns the mutex resource name `pid` is blocked on, or "" if none. */
static const char *blockedOn(int pid) {
    for (int i = 0; i < NUM_MUTEXES; i++)
        for (int j = 0; j < mutexes[i].waitCount; j++)
            if (mutexes[i].waitQueue[j] == pid)
                return mutexes[i].name;
    return "";
}

/* ═══════════════════════ SERIALIZE STATE ══════════════════════════ */

char *serializeState(void) {
    int pos = 0;
    char e1[512], e2[512], st[32];

#define AP(fmt, ...) do { \
    int _n = snprintf(s_jsonBuf + pos, JSON_BUF_SIZE - pos, fmt, ##__VA_ARGS__); \
    if (_n > 0 && _n < JSON_BUF_SIZE - pos) pos += _n; \
} while(0)

    const char *algoStr = (g_algo == 1) ? "HRRN" :
                          (g_algo == 2) ? "RR"   : "MLFQ";
    int tsSlice = (g_algo == 2) ? RR_QUANTUM : 2;

    AP("{");
    AP("\"clock\":%d,", g_tick);
    AP("\"algorithm\":\"%s\",", algoStr);
    AP("\"timeSlice\":%d,", tsSlice);

    if (g_runningPid != -1)
        AP("\"runningPid\":%d,", g_runningPid);
    else
        AP("\"runningPid\":null,");

    /* ── readyQueue ─────────────────────────────────────────────── */
    AP("\"readyQueue\":[");
    {
        int first = 1;
        if (g_algo == 3) {
            /* MLFQ: merge all levels, Q1 (highest) first */
            int *qs[4] = { g_q1,   g_q2,   g_q3,   g_q4   };
            int  sz[4] = { g_q1sz, g_q2sz, g_q3sz, g_q4sz };
            for (int qi = 0; qi < 4; qi++)
                for (int k = 0; k < sz[qi]; k++) {
                    if (!first) AP(",");
                    AP("%d", qs[qi][k]);
                    first = 0;
                }
        } else {
            for (int k = 0; k < g_readyQueueSize; k++) {
                if (!first) AP(",");
                AP("%d", g_readyQueue[k]);
                first = 0;
            }
        }
    }
    AP("],");

    /* ── blockedQueue ───────────────────────────────────────────── */
    AP("\"blockedQueue\":[");
    for (int k = 0; k < g_blockedQueueSize; k++) {
        if (k > 0) AP(",");
        int pid = g_blockedQueue[k];
        jsonEsc(blockedOn(pid), e1, sizeof(e1));
        AP("{\"pid\":%d,\"resource\":\"%s\"}", pid, e1);
    }
    AP("],");

    /* ── processes ──────────────────────────────────────────────── */
    AP("\"processes\":[");
    {
        int first = 1;
        for (int i = 0; i < g_n; i++) {
            Process *p = &g_processes[i];
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

            /* queueLevel — MLFQ only */
            if (g_algo == 3)
                AP("\"queueLevel\":%d,", g_pLevel[i] + 1);
            else
                AP("\"queueLevel\":null,");

            AP("\"waitingTime\":%d,", g_processList[i].waitingTime);

            /* responseRatio — HRRN only */
            if (g_algo == 1) {
                int wt = g_processList[i].waitingTime;
                int bt = p->codeLineCount - p->pcb.programCounter;
                if (bt <= 0) bt = 1;
                float rr = (float)(wt + bt) / bt;
                AP("\"responseRatio\":%.2f,", rr);
            } else {
                AP("\"responseRatio\":null,");
            }

            /* vars — read from RAM or disk */
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

            /* instructions */
            AP("\"instructions\":[");
            for (int j = 0; j < p->codeLineCount; j++) {
                if (j > 0) AP(",");
                jsonEsc(p->codeLines[j], e1, sizeof(e1));
                AP("\"%s\"", e1);
            }
            AP("],");

            /* heldMutexes */
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

    /* ── memory ─────────────────────────────────────────────────── */
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

    /* ── disk ───────────────────────────────────────────────────── */
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

    /* ── log ────────────────────────────────────────────────────── */
    AP("\"log\":[");
    for (int i = 0; i < g_logCount; i++) {
        if (i > 0) AP(",");
        jsonEsc(g_log[i].event, e1, sizeof(e1));
        jsonEsc(g_log[i].type,  e2, sizeof(e2));
        AP("{\"clock\":%d,\"pid\":%d,\"event\":\"%s\",\"type\":\"%s\"}",
           g_log[i].clock, g_log[i].pid, e1, e2);
    }
    AP("],");

    /* ── mutexes ────────────────────────────────────────────────── */
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
    AP("}");   /* close mutexes */

    AP("}");   /* close root */
#undef AP
    return s_jsonBuf;
}

/* ═══════════════════════ INIT / RESET ════════════════════════════ */

static void initSimulation(void) {
    g_programFiles[0] = "programs/program1.txt";
    g_programFiles[1] = "programs/program2.txt";
    g_programFiles[2] = "programs/program3.txt";
    g_arrivalTimes[0] = 0;
    g_arrivalTimes[1] = 1;
    g_arrivalTimes[2] = 4;
    g_n = 3;

    initMemory();
    initMutexes();

    g_tick           = 0;
    g_simFinished    = 0;
    g_runningPid     = -1;
    g_currentRunning = -1;
    g_readyQueueSize  = 0;
    g_blockedQueueSize = 0;
    g_niRan          = 0;
    g_q1sz = g_q2sz = g_q3sz = g_q4sz = 0;
    g_logCount       = 0;
    memset(g_pLevel, 0, sizeof(g_pLevel));

    for (int i = 0; i < g_n; i++) {
        g_processes[i].id              = i + 1;
        g_processes[i].arrivalTime     = g_arrivalTimes[i];
        g_processes[i].pcb.processId   = i + 1;
        g_processes[i].pcb.programCounter = 0;
        strcpy(g_processes[i].pcb.state, "NEW");
        g_processes[i].isCreated       = 0;
        g_processes[i].isSwappedOut    = 0;
        g_processes[i].quantumUsed     = 0;
        g_processes[i].codeLineCount   = 0;

        g_processList[i].processID     = i + 1;
        g_processList[i].arrivalTime   = g_arrivalTimes[i];
        g_processList[i].burstTime     = 0;
        g_processList[i].waitingTime   = 0;
    }
}

static void resetSimulation(void) {
    sys_clearInputQueue();
    initSimulation();
}

/* ═══════════════════════ STEP SIMULATION ══════════════════════════ */

static void stepSimulation(void) {
    if (g_simFinished) return;

    char evtBuf[LOG_EVENT_LEN];

    printf("\n╔═══════════════════════════════════╗\n");
    printf("║         Clock Cycle: %-4d         ║\n", g_tick);
    printf("╚═══════════════════════════════════╝\n");

    /* ── 1. Update waiting times (before arrivals, so they don't count) ── */
    for (int k = 0; k < g_readyQueueSize; k++) {
        int pid = g_readyQueue[k];
        if (pid != g_currentRunning)
            g_processList[pid - 1].waitingTime++;
    }

    /* ── 2. Process arrivals ─────────────────────────────────────── */
    for (int j = 0; j < g_n; j++) {
        if (g_processList[j].arrivalTime != g_tick) continue;
        if (g_processes[j].isCreated) continue;

        printf(">> Process %d has ARRIVED\n", g_processes[j].id);

        if (loadProgram(&g_processes[j], g_programFiles[j]) < 0) {
            printf("[OS ERROR] Failed to load %s\n", g_programFiles[j]);
            strcpy(g_processes[j].pcb.state, "FINISHED");
            continue;
        }
        g_processList[j].burstTime = g_processes[j].codeLineCount;

        int result = allocateProcess(&g_processes[j]);
        if (result == -1) {
            printf("[OS] Not enough memory for P%d — swapping...\n", g_processes[j].id);
            for (int k = 0; k < g_n; k++) {
                if (!g_processes[k].isCreated)   continue;
                if ( g_processes[k].isSwappedOut) continue;
                if (g_processes[k].id == g_currentRunning) continue;
                if (strcmp(g_processes[k].pcb.state, "FINISHED") == 0) continue;
                snprintf(evtBuf, sizeof(evtBuf),
                         "P%d swapped out — making room for P%d",
                         g_processes[k].id, g_processes[j].id);
                swapOut(&g_processes[k]);
                addLog(g_processes[k].id, evtBuf, "memory-swap");
                result = allocateProcess(&g_processes[j]);
                if (result != -1) break;
            }
            if (result == -1) {
                printf("[OS ERROR] Cannot allocate memory for P%d!\n", g_processes[j].id);
                continue;
            }
        }

        g_processes[j].isCreated = 1;
        strcpy(g_processes[j].pcb.state, "READY");
        printf("[OS] P%d created → memory [%d–%d] (burst:%d)\n",
               g_processes[j].id,
               g_processes[j].pcb.lowerBound,
               g_processes[j].pcb.upperBound,
               g_processes[j].codeLineCount);

        snprintf(evtBuf, sizeof(evtBuf),
                 "Process %d arrived — allocated memory [%d–%d]",
                 g_processes[j].id,
                 g_processes[j].pcb.lowerBound,
                 g_processes[j].pcb.upperBound);
        addLog(g_processes[j].id, evtBuf, "memory-swap");

        if (g_algo == 3) {
            g_q1[g_q1sz++] = g_processes[j].id;
            g_pLevel[j] = 0;
        } else {
            g_readyQueue[g_readyQueueSize++] = g_processes[j].id;
        }
    }

    /* ── 3. Reset unblock signal ────────────────────────────────── */
    unblockedPID = -1;

    /* ════════════════════ SCHEDULING ALGORITHMS ═══════════════════ */

    /* ── HRRN (non-preemptive) ────────────────────────────────────── */
    if (g_algo == 1) {

        if (g_currentRunning == -1 && g_readyQueueSize > 0) {
            g_currentRunning = selectHRRN(g_readyQueue, g_readyQueueSize,
                                          g_processList, g_processes);
            removeFromQueue(g_readyQueue, &g_readyQueueSize, g_currentRunning);
            strcpy(g_processes[g_currentRunning - 1].pcb.state, "RUNNING");
            g_runningPid = g_currentRunning;

            printf(">> P%d selected (HRRN)\n", g_currentRunning);
            snprintf(evtBuf, sizeof(evtBuf),
                     "Process %d scheduled → RUNNING (HRRN)", g_currentRunning);
            addLog(g_currentRunning, evtBuf, "scheduling");

            printQueues(g_readyQueue, g_readyQueueSize,
                        g_blockedQueue, g_blockedQueueSize, g_currentRunning);
        }

        if (g_currentRunning != -1) {
            if (!ensureInMemory(g_currentRunning, g_processes, g_n, g_currentRunning)) {
                g_readyQueue[g_readyQueueSize++] = g_currentRunning;
                strcpy(g_processes[g_currentRunning - 1].pcb.state, "READY");
                g_currentRunning = -1;
            } else {
                int idx      = g_currentRunning - 1;
                int pcBefore = g_processes[idx].pcb.programCounter;
                int execRes  = executeInstruction(&g_processes[idx]);

                if (pcBefore < g_processes[idx].codeLineCount) {
                    snprintf(evtBuf, sizeof(evtBuf), "P%d: %s",
                             g_currentRunning, g_processes[idx].codeLines[pcBefore]);
                    addLog(g_currentRunning, evtBuf, "execution");
                }

                if (execRes == EXEC_FINISHED) {
                    strcpy(g_processes[idx].pcb.state, "FINISHED");
                    freeProcess(&g_processes[idx]);
                    printf(">> P%d FINISHED (memory freed)\n", g_currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d finished", g_currentRunning);
                    addLog(g_currentRunning, evtBuf, "finish");
                    g_runningPid     = -1;
                    g_currentRunning = -1;
                } else if (execRes == EXEC_BLOCKED) {
                    strcpy(g_processes[idx].pcb.state, "BLOCKED");
                    g_blockedQueue[g_blockedQueueSize++] = g_currentRunning;
                    printf(">> P%d BLOCKED\n", g_currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d BLOCKED on %s",
                             g_currentRunning, blockedOn(g_currentRunning));
                    addLog(g_currentRunning, evtBuf, "blocking");
                    g_runningPid     = -1;
                    g_currentRunning = -1;
                }
                printQueues(g_readyQueue, g_readyQueueSize,
                            g_blockedQueue, g_blockedQueueSize, g_currentRunning);
            }
        }
    }

    /* ── Round Robin ──────────────────────────────────────────────── */
    else if (g_algo == 2) {

        if (g_readyQueueSize > 0) {
            g_currentRunning = g_readyQueue[0];
            strcpy(g_processes[g_currentRunning - 1].pcb.state, "RUNNING");
            g_runningPid = g_currentRunning;

            if (!ensureInMemory(g_currentRunning, g_processes, g_n, g_currentRunning)) {
                /* Rotate to back and try next tick */
                int tmp = g_readyQueue[0];
                for (int p = 0; p < g_readyQueueSize - 1; p++)
                    g_readyQueue[p] = g_readyQueue[p + 1];
                g_readyQueue[g_readyQueueSize - 1] = tmp;
                strcpy(g_processes[g_currentRunning - 1].pcb.state, "READY");
                g_currentRunning = -1;
                g_niRan = 0;
            } else {
                printf(">> Running: P%d\n", g_currentRunning);
                int idx      = g_currentRunning - 1;
                int pcBefore = g_processes[idx].pcb.programCounter;
                int execRes  = executeInstruction(&g_processes[idx]);
                g_niRan++;

                if (pcBefore < g_processes[idx].codeLineCount) {
                    snprintf(evtBuf, sizeof(evtBuf), "P%d: %s",
                             g_currentRunning, g_processes[idx].codeLines[pcBefore]);
                    addLog(g_currentRunning, evtBuf, "execution");
                }

                if (execRes == EXEC_FINISHED) {
                    for (int p = 0; p < g_readyQueueSize - 1; p++)
                        g_readyQueue[p] = g_readyQueue[p + 1];
                    g_readyQueueSize--;
                    strcpy(g_processes[idx].pcb.state, "FINISHED");
                    freeProcess(&g_processes[idx]);
                    printf(">> P%d FINISHED\n", g_currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d finished", g_currentRunning);
                    addLog(g_currentRunning, evtBuf, "finish");
                    g_runningPid     = -1;
                    g_currentRunning = -1;
                    g_niRan = 0;
                } else if (execRes == EXEC_BLOCKED) {
                    for (int p = 0; p < g_readyQueueSize - 1; p++)
                        g_readyQueue[p] = g_readyQueue[p + 1];
                    g_readyQueueSize--;
                    g_blockedQueue[g_blockedQueueSize++] = g_currentRunning;
                    strcpy(g_processes[idx].pcb.state, "BLOCKED");
                    printf(">> P%d BLOCKED\n", g_currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d BLOCKED on %s",
                             g_currentRunning, blockedOn(g_currentRunning));
                    addLog(g_currentRunning, evtBuf, "blocking");
                    g_runningPid     = -1;
                    g_currentRunning = -1;
                    g_niRan = 0;
                } else if (g_niRan >= RR_QUANTUM) {
                    int tmp = g_readyQueue[0];
                    for (int p = 0; p < g_readyQueueSize - 1; p++)
                        g_readyQueue[p] = g_readyQueue[p + 1];
                    g_readyQueue[g_readyQueueSize - 1] = tmp;
                    strcpy(g_processes[idx].pcb.state, "READY");
                    printf(">> P%d quantum expired\n", g_currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "P%d preempted — quantum expired (RR)", g_currentRunning);
                    addLog(g_currentRunning, evtBuf, "scheduling");
                    /* Keep g_runningPid for the UI — same process may run next tick */
                    g_currentRunning = -1;
                    g_niRan = 0;
                }
                printQueues(g_readyQueue, g_readyQueueSize,
                            g_blockedQueue, g_blockedQueueSize, g_currentRunning);
            }
        }
    }

    /* ── MLFQ (Multi-Level Feedback Queue) ───────────────────────── */
    else if (g_algo == 3) {

        int selPID = -1, selQ = 0;
        if      (g_q1sz > 0) { selPID = g_q1[0]; selQ = 1; }
        else if (g_q2sz > 0) { selPID = g_q2[0]; selQ = 2; }
        else if (g_q3sz > 0) { selPID = g_q3[0]; selQ = 3; }
        else if (g_q4sz > 0) { selPID = g_q4[0]; selQ = 4; }

        if (selPID != -1) {
            g_currentRunning = selPID;
            g_runningPid     = selPID;
            strcpy(g_processes[selPID - 1].pcb.state, "RUNNING");

            if (!ensureInMemory(g_currentRunning, g_processes, g_n, g_currentRunning)) {
                strcpy(g_processes[g_currentRunning - 1].pcb.state, "READY");
                g_currentRunning = -1;
            } else {
                printf(">> P%d running (Q%d)\n", g_currentRunning, selQ);
                int idx      = g_currentRunning - 1;
                int pcBefore = g_processes[idx].pcb.programCounter;
                int execRes  = executeInstruction(&g_processes[idx]);

                if (pcBefore < g_processes[idx].codeLineCount) {
                    snprintf(evtBuf, sizeof(evtBuf), "P%d: %s",
                             g_currentRunning, g_processes[idx].codeLines[pcBefore]);
                    addLog(g_currentRunning, evtBuf, "execution");
                }

                if (execRes == EXEC_FINISHED) {
                    strcpy(g_processes[idx].pcb.state, "FINISHED");
                    freeProcess(&g_processes[idx]);
                    g_processes[selPID - 1].quantumUsed = 0;
                    printf(">> P%d FINISHED\n", g_currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d finished", g_currentRunning);
                    addLog(g_currentRunning, evtBuf, "finish");
                    g_runningPid = -1;
                    switch (selQ) {
                        case 1: removeFromQueue(g_q1, &g_q1sz, selPID); break;
                        case 2: removeFromQueue(g_q2, &g_q2sz, selPID); break;
                        case 3: removeFromQueue(g_q3, &g_q3sz, selPID); break;
                        case 4: removeFromQueue(g_q4, &g_q4sz, selPID); break;
                    }
                } else if (execRes == EXEC_BLOCKED) {
                    strcpy(g_processes[idx].pcb.state, "BLOCKED");
                    g_blockedQueue[g_blockedQueueSize++] = g_currentRunning;
                    g_processes[selPID - 1].quantumUsed = 0;
                    printf(">> P%d BLOCKED\n", g_currentRunning);
                    snprintf(evtBuf, sizeof(evtBuf),
                             "Process %d BLOCKED on %s",
                             g_currentRunning, blockedOn(g_currentRunning));
                    addLog(g_currentRunning, evtBuf, "blocking");
                    g_runningPid = -1;
                    switch (selQ) {
                        case 1: removeFromQueue(g_q1, &g_q1sz, selPID); break;
                        case 2: removeFromQueue(g_q2, &g_q2sz, selPID); break;
                        case 3: removeFromQueue(g_q3, &g_q3sz, selPID); break;
                        case 4: removeFromQueue(g_q4, &g_q4sz, selPID); break;
                    }
                } else {
                    /* EXEC_CONTINUE — check quantum expiry */
                    g_processes[selPID - 1].quantumUsed++;

                    if (selQ == 1) {
                        /* Q1 quantum = 1: always demote immediately */
                        demoteProcess(g_q1, g_q2, &g_q1sz, &g_q2sz, g_pLevel);
                        g_processes[selPID - 1].quantumUsed = 0;
                        printf(">> P%d demoted Q1→Q2\n", selPID);
                        snprintf(evtBuf, sizeof(evtBuf),
                                 "P%d preempted — demoted Q1→Q2", selPID);
                        addLog(selPID, evtBuf, "scheduling");
                    } else if (selQ == 2 && g_processes[selPID - 1].quantumUsed >= 2) {
                        /* Q2 quantum = 2 */
                        demoteProcess(g_q2, g_q3, &g_q2sz, &g_q3sz, g_pLevel);
                        g_processes[selPID - 1].quantumUsed = 0;
                        printf(">> P%d demoted Q2→Q3\n", selPID);
                        snprintf(evtBuf, sizeof(evtBuf),
                                 "P%d preempted — demoted Q2→Q3", selPID);
                        addLog(selPID, evtBuf, "scheduling");
                    } else if (selQ == 3 && g_processes[selPID - 1].quantumUsed >= 4) {
                        /* Q3 quantum = 4 */
                        demoteProcess(g_q3, g_q4, &g_q3sz, &g_q4sz, g_pLevel);
                        g_processes[selPID - 1].quantumUsed = 0;
                        printf(">> P%d demoted Q3→Q4\n", selPID);
                        snprintf(evtBuf, sizeof(evtBuf),
                                 "P%d preempted — demoted Q3→Q4", selPID);
                        addLog(selPID, evtBuf, "scheduling");
                    } else if (selQ == 4 && g_processes[selPID - 1].quantumUsed >= 8) {
                        /* Q4 quantum = 8: RR rotation */
                        rotateQueue4(g_q4, &g_q4sz);
                        g_processes[selPID - 1].quantumUsed = 0;
                        printf(">> P%d rotated in Q4\n", selPID);
                        snprintf(evtBuf, sizeof(evtBuf),
                                 "P%d rotated in Q4 (RR)", selPID);
                        addLog(selPID, evtBuf, "scheduling");
                    }
                    /* Keep g_runningPid = selPID so the UI shows last runner */
                }

                printMLFQQueues(g_q1, g_q1sz, g_q2, g_q2sz,
                                g_q3, g_q3sz, g_q4, g_q4sz,
                                g_blockedQueue, g_blockedQueueSize, g_currentRunning);
                g_currentRunning = -1;  /* MLFQ re-selects each tick */
            }
        }
    }

    /* ── 4. Handle processes unblocked by semSignal ─────────────── */
    if (unblockedPID != -1) {
        for (int i = 0; i < g_blockedQueueSize; i++) {
            if (g_blockedQueue[i] != unblockedPID) continue;
            for (int j = i; j < g_blockedQueueSize - 1; j++)
                g_blockedQueue[j] = g_blockedQueue[j + 1];
            g_blockedQueueSize--;
            break;
        }
        strcpy(g_processes[unblockedPID - 1].pcb.state, "READY");
        printf(">> P%d UNBLOCKED → ready\n", unblockedPID);
        snprintf(evtBuf, sizeof(evtBuf),
                 "Process %d unblocked → ready queue", unblockedPID);
        addLog(unblockedPID, evtBuf, "blocking");

        if (g_algo == 3) {
            int lvl = g_pLevel[unblockedPID - 1];
            switch (lvl) {
                case 0: g_q1[g_q1sz++] = unblockedPID; break;
                case 1: g_q2[g_q2sz++] = unblockedPID; break;
                case 2: g_q3[g_q3sz++] = unblockedPID; break;
                case 3: g_q4[g_q4sz++] = unblockedPID; break;
            }
            printMLFQQueues(g_q1, g_q1sz, g_q2, g_q2sz,
                            g_q3, g_q3sz, g_q4, g_q4sz,
                            g_blockedQueue, g_blockedQueueSize, -1);
        } else {
            g_readyQueue[g_readyQueueSize++] = unblockedPID;
            printQueues(g_readyQueue, g_readyQueueSize,
                        g_blockedQueue, g_blockedQueueSize, -1);
        }
    }

    /* ── 5. Sync PCBs and print memory ──────────────────────────── */
    for (int i = 0; i < g_n; i++) {
        if (g_processes[i].isCreated && !g_processes[i].isSwappedOut)
            syncPCBToMemory(&g_processes[i]);
    }
    printMemory();

    g_tick++;

    if (isFinished(g_processes, g_n)) {
        g_simFinished = 1;
        addLog(-1, "All processes finished", "finish");
        printf("\n╔═══════════════════════════════════╗\n");
        printf("║     ALL PROCESSES FINISHED        ║\n");
        printf("╚═══════════════════════════════════╝\n");
        printf("Total clock cycles: %d\n", g_tick);
        printDisk();
    }
}

/* ═══════════════════════ HTTP SERVER ══════════════════════════════ */

static void writeAll(int fd, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = (int)write(fd, buf + sent, len - sent);
        if (n <= 0) return;
        sent += n;
    }
}

static void sendResponse(int fd, int status,
                         const char *body, int bodyLen) {
    const char *statusStr =
        (status == 200) ? "200 OK" :
        (status == 404) ? "404 Not Found" : "500 Internal Server Error";

    char header[512];
    int  hlen = snprintf(header, sizeof(header),
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

/* Extract a simple quoted string value after `key` in `json`.
   Writes at most cap-1 characters into out.  Returns 1 on success. */
static int jsonGet(const char *json, const char *key, char *out, int cap) {
    const char *p = strstr(json, key);
    if (!p) return 0;
    p = strchr(p + strlen(key), '"');
    if (!p) return 0;
    p++;                               /* skip opening quote */
    int i = 0;
    while (*p && *p != '"' && i < cap - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return 1;
}

/* Extract a numeric value after `key` in `json`.  Returns 0 on fail. */
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

static void handleRequest(int fd) {
    /* ── Read request ────────────────────────────────────────────── */
    char req[HTTP_RX_BUF];
    int  total = 0;

    while (total < HTTP_RX_BUF - 1) {
        int n = (int)read(fd, req + total, HTTP_RX_BUF - total - 1);
        if (n <= 0) break;
        total += n;
        req[total] = '\0';
        if (strstr(req, "\r\n\r\n")) break;
    }
    if (total == 0) return;
    req[total] = '\0';

    /* ── Parse method + path ──────────────────────────────────────── */
    char method[8]  = {0};
    char path[256]  = {0};
    sscanf(req, "%7s %255s", method, path);

    /* ── CORS preflight ───────────────────────────────────────────── */
    if (strcmp(method, "OPTIONS") == 0) {
        sendResponse(fd, 200, "", 0);
        return;
    }

    /* ── Locate request body ──────────────────────────────────────── */
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

    /* ── Route ────────────────────────────────────────────────────── */
    if (strcmp(path, "/api/state") == 0 && strcmp(method, "GET") == 0) {
        char *json = serializeState();
        sendResponse(fd, 200, json, (int)strlen(json));
    }
    else if (strcmp(path, "/api/step") == 0 && strcmp(method, "POST") == 0) {
        /* Optional input value for assign … input instructions */
        char inputVal[256] = "0";
        jsonGet(body, "\"input\"", inputVal, sizeof(inputVal));
        sys_pushInput(inputVal);

        if (!g_simFinished)
            stepSimulation();

        char *json = serializeState();
        sendResponse(fd, 200, json, (int)strlen(json));
    }
    else if (strcmp(path, "/api/reset") == 0 && strcmp(method, "POST") == 0) {
        /* Optional algorithm selection: {"algo": 1|2|3} */
        int newAlgo = 0;
        if (jsonGetInt(body, "\"algo\"", &newAlgo) && newAlgo >= 1 && newAlgo <= 3)
            g_algo = newAlgo;

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
    /* Ignore SIGPIPE so a closed client socket doesn't kill the server */
    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(srv, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(srv, 16) < 0) {
        perror("listen"); exit(1);
    }

    const char *algoStr = (g_algo == 1) ? "HRRN" :
                          (g_algo == 2) ? "RR"   : "MLFQ";
    printf("[HTTP] SimOS server listening on http://localhost:%d\n", port);
    printf("[HTTP] Algorithm: %s   (POST /api/reset {\"algo\":1|2|3} to change)\n", algoStr);
    printf("[HTTP] GET  /api/state  → full simulation state as JSON\n");
    printf("[HTTP] POST /api/step   → advance one clock tick\n");
    printf("[HTTP] POST /api/reset  → reset simulation\n");

    while (1) {
        int client = accept(srv, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        /* 5-second receive timeout so a stalled client can't block forever */
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        handleRequest(client);
        close(client);
    }

    close(srv);
}

/* ════════════════════════════════════════════════════════════════ */
/*                              MAIN                               */
/* ════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    /* Optional positional arg: algorithm (1 HRRN | 2 RR | 3 MLFQ) */
    if (argc >= 2) {
        int a = atoi(argv[1]);
        if (a >= 1 && a <= 3) g_algo = a;
    }

    initSimulation();
    runServer(HTTP_PORT);
    return 0;
}

/* ════════════════════════════════════════════════════════════════ */
/*                        HELPER FUNCTIONS                         */
/* ════════════════════════════════════════════════════════════════ */

bool isFinished(Process procs[], int n) {
    for (int i = 0; i < n; i++)
        if (strcmp(procs[i].pcb.state, "FINISHED") != 0) return false;
    return true;
}

int selectHRRN(int rq[], int rqsz,
               SchedulerInfo pl[], Process procs[]) {
    float best = -1;
    int   sel  = rq[0];
    for (int i = 0; i < rqsz; i++) {
        int   pid = rq[i];
        int   wt  = pl[pid - 1].waitingTime;
        int   bt  = procs[pid - 1].codeLineCount
                  - procs[pid - 1].pcb.programCounter;
        if (bt <= 0) bt = 1;
        float rr  = (float)(wt + bt) / bt;
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

void demoteProcess(int cq[], int nq[],
                   int *csz, int *nsz,
                   int pql[]) {
    if (*csz <= 0) return;
    int pid = cq[0];
    for (int i = 0; i < *csz - 1; i++) cq[i] = cq[i + 1];
    (*csz)--;
    nq[*nsz] = pid;
    (*nsz)++;
    if (pql[pid - 1] < 3) pql[pid - 1]++;
}

void rotateQueue4(int q4[], int *sz) {
    if (*sz <= 1) return;
    int first = q4[0];
    for (int i = 0; i < *sz - 1; i++) q4[i] = q4[i + 1];
    q4[*sz - 1] = first;
}

void printQueues(int rq[], int rqsz,
                 int bq[], int bqsz, int cr) {
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

int ensureInMemory(int pid, Process procs[], int n, int cr) {
    (void)cr;  /* parameter kept for API compatibility; not needed here */
    if (!procs[pid - 1].isSwappedOut) return 1;

    /* Free any finished processes that are still in memory */
    for (int k = 0; k < n; k++) {
        if (procs[k].isCreated && !procs[k].isSwappedOut &&
            strcmp(procs[k].pcb.state, "FINISHED") == 0) {
            printf("[OS] Freeing finished P%d memory\n", procs[k].id);
            freeProcess(&procs[k]);
            procs[k].isSwappedOut = 1;
        }
    }

    if (swapIn(&procs[pid - 1]) != -1) return 1;

    /* Need to evict a live process */
    for (int k = 0; k < n; k++) {
        if (!procs[k].isCreated || procs[k].isSwappedOut) continue;
        if (procs[k].id == pid)  continue;
        if (strcmp(procs[k].pcb.state, "FINISHED") == 0) continue;
        swapOut(&procs[k]);
        if (swapIn(&procs[pid - 1]) != -1) return 1;
    }

    printf("[OS ERROR] Cannot swap in P%d\n", pid);
    return 0;
}
