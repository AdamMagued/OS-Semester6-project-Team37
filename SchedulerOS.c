#include "memory.h"
#include "mutex.h"
#include "interpreter.h"
#include <stdbool.h>

#define RR_QUANTUM 2

/* ── Forward Declarations ── */
bool isFinished(Process processes[], int n);
int  selectHRRN(int readyQueue[], int readyQueueSize, SchedulerInfo processList[], Process processes[]);
void removeFromQueue(int queue[], int *queueSize, int pid);
void demoteProcess(int currentQueue[], int nextQueue[],
                   int *currentQueueSize, int *nextQueueSize,
                   int processQueueLevel[]);
void rotateQueue4(int queue4[], int *queue4Size);
void printQueues(int readyQueue[], int readyQueueSize,
                 int blockedQueue[], int blockedQueueSize, int currentRunning);
void printMLFQQueues(int queue1[], int queue1Size,
                     int queue2[], int queue2Size,
                     int queue3[], int queue3Size,
                     int queue4[], int queue4Size,
                     int blockedQueue[], int blockedQueueSize, int currentRunning);
int  ensureInMemory(int pid, Process processes[], int n, int currentRunning);

/* ════════════════════════════════════════════════════════════════ */
/*                             MAIN                                */
/* ════════════════════════════════════════════════════════════════ */

int main() {
    initMemory();
    initMutexes();

    /* ── Program files and arrival times ── */
    char *programFiles[] = {"Program 1.txt", "Program_2.txt", "Program_3.txt"};
    int arrivalTimes[] = {0, 1, 4};
    int n = 3;

    /* ── Initialize process structs (programs loaded at arrival time) ── */
    Process processes[MAX_PROCESSES];
    memset(processes, 0, sizeof(processes));

    for (int i = 0; i < n; i++) {
        processes[i].id = i + 1;
        processes[i].arrivalTime = arrivalTimes[i];
        processes[i].pcb.processId = i + 1;
        processes[i].pcb.programCounter = 0;
        strcpy(processes[i].pcb.state, "NEW");
        processes[i].isCreated = 0;
        processes[i].isSwappedOut = 0;
        processes[i].quantumUsed = 0;
    }

    /* ── Scheduler tracking info ── */
    SchedulerInfo processList[MAX_PROCESSES];
    for (int i = 0; i < n; i++) {
        processList[i].processID = processes[i].id;
        processList[i].arrivalTime = processes[i].arrivalTime;
        processList[i].burstTime = processes[i].codeLineCount;
        processList[i].waitingTime = 0;
    }

    /* ── Queues ── */
    int readyQueue[MAX_PROCESSES];
    int readyQueueSize = 0;
    int blockedQueueArr[MAX_PROCESSES];
    int blockedQueueSize = 0;
    int currentRunning = -1;
    int numberOfInstructionsRan = 0;

    /* ── MLFQ data ── */
    int queue1[MAX_PROCESSES], queue2[MAX_PROCESSES];
    int queue3[MAX_PROCESSES], queue4[MAX_PROCESSES];
    int queue1Size = 0, queue2Size = 0, queue3Size = 0, queue4Size = 0;
    int processQueueLevel[MAX_PROCESSES];
    memset(processQueueLevel, 0, sizeof(processQueueLevel));

    /* ── Algorithm selection ── */
    int algo;
    printf("\nSelect scheduling algorithm:\n");
    printf("  1 = HRRN (Highest Response Ratio Next)\n");
    printf("  2 = Round Robin (quantum = %d)\n", RR_QUANTUM);
    printf("  3 = MLFQ (Multi-Level Feedback Queue)\n");
    printf("> ");
    scanf("%d", &algo);
    printf("\n");

    int tick = 0;

    /* ══════════════ MAIN SIMULATION LOOP ══════════════ */

    while (!isFinished(processes, n)) {
        printf("\n╔═══════════════════════════════════╗\n");
        printf("║         Clock Cycle: %-4d         ║\n", tick);
        printf("╚═══════════════════════════════════╝\n");

        /* ── 1. Update waiting times for processes already in ready queue ── */
        /*    (before arrivals, so new arrivals don't get an extra tick)   */
        if (algo != 3) {
            for (int k = 0; k < readyQueueSize; k++) {
                int pid = readyQueue[k];
                if (pid != currentRunning) {
                    processList[pid - 1].waitingTime++;
                }
            }
        }

        /* ── 2. Check for process arrivals ── */
        for (int j = 0; j < n; j++) {
            if (processList[j].arrivalTime == tick && !processes[j].isCreated) {
                printf(">> Process %d has ARRIVED\n", processes[j].id);

                /* Load program file at arrival time (not before simulation) */
                if (loadProgram(&processes[j], programFiles[j]) < 0) {
                    printf("[OS ERROR] Failed to load %s\n", programFiles[j]);
                    strcpy(processes[j].pcb.state, "FINISHED");
                    continue;
                }
                processList[j].burstTime = processes[j].codeLineCount;

                int result = allocateProcess(&processes[j]);
                if (result == -1) {
                    /* Not enough memory - swap out another process */
                    printf("[OS] Not enough memory for Process %d, swapping...\n",
                           processes[j].id);
                    for (int k = 0; k < n; k++) {
                        if (processes[k].isCreated && !processes[k].isSwappedOut &&
                            processes[k].id != currentRunning &&
                            strcmp(processes[k].pcb.state, "FINISHED") != 0) {
                            swapOut(&processes[k]);
                            result = allocateProcess(&processes[j]);
                            if (result != -1) break;
                        }
                    }
                    if (result == -1) {
                        printf("[OS ERROR] Cannot allocate memory for Process %d!\n",
                               processes[j].id);
                        continue;
                    }
                }

                processes[j].isCreated = 1;
                strcpy(processes[j].pcb.state, "READY");
                printf("[OS] Process %d created -> memory [%d-%d] (burst: %d)\n",
                       processes[j].id, processes[j].pcb.lowerBound,
                       processes[j].pcb.upperBound, processes[j].codeLineCount);

                if (algo == 3) {
                    queue1[queue1Size++] = processes[j].id;
                    processQueueLevel[processes[j].id - 1] = 0;
                } else {
                    readyQueue[readyQueueSize++] = processes[j].id;
                }
            }
        }

        /* ── 3. Reset unblockedPID before execution ── */
        unblockedPID = -1;

        /* ══════════════════════════════════════════════════════ */
        /*                   SCHEDULING ALGORITHMS                */
        /* ══════════════════════════════════════════════════════ */

        if (algo == 1) {
            /* ──── HRRN (Non-Preemptive) ──── */

            if (currentRunning == -1 && readyQueueSize > 0) {
                /* Select process with highest response ratio */
                currentRunning = selectHRRN(readyQueue, readyQueueSize, processList, processes);
                removeFromQueue(readyQueue, &readyQueueSize, currentRunning);
                strcpy(processes[currentRunning - 1].pcb.state, "RUNNING");

                printf(">> Process %d selected to run (HRRN)\n", currentRunning);
                printQueues(readyQueue, readyQueueSize,
                           blockedQueueArr, blockedQueueSize, currentRunning);
            }

            if (currentRunning != -1) {
                if (!ensureInMemory(currentRunning, processes, n, currentRunning)) {
                    readyQueue[readyQueueSize++] = currentRunning;
                    strcpy(processes[currentRunning - 1].pcb.state, "READY");
                    currentRunning = -1;
                } else {
                    int execResult = executeInstruction(&processes[currentRunning - 1]);

                    if (execResult == EXEC_FINISHED) {
                        strcpy(processes[currentRunning - 1].pcb.state, "FINISHED");
                        freeProcess(&processes[currentRunning - 1]);
                        printf(">> Process %d has FINISHED (memory freed)\n", currentRunning);
                        printQueues(readyQueue, readyQueueSize,
                                   blockedQueueArr, blockedQueueSize, -1);
                        currentRunning = -1;
                    }
                    else if (execResult == EXEC_BLOCKED) {
                        strcpy(processes[currentRunning - 1].pcb.state, "BLOCKED");
                        blockedQueueArr[blockedQueueSize++] = currentRunning;
                        printf(">> Process %d is BLOCKED\n", currentRunning);
                        printQueues(readyQueue, readyQueueSize,
                                   blockedQueueArr, blockedQueueSize, -1);
                        currentRunning = -1;
                    }
                }
            }
        }

        else if (algo == 2) {
            /* ──── ROUND ROBIN ──── */

            if (readyQueueSize > 0) {
                currentRunning = readyQueue[0];
                strcpy(processes[currentRunning - 1].pcb.state, "RUNNING");

                if (!ensureInMemory(currentRunning, processes, n, currentRunning)) {
                    /* Can't swap in - move to back of queue and try next */
                    int tempPid = readyQueue[0];
                    for (int p = 0; p < readyQueueSize - 1; p++)
                        readyQueue[p] = readyQueue[p + 1];
                    readyQueue[readyQueueSize - 1] = tempPid;
                    strcpy(processes[currentRunning - 1].pcb.state, "READY");
                    currentRunning = -1;
                    numberOfInstructionsRan = 0;
                } else {
                    printf(">> Currently running: Process %d\n", currentRunning);
                    int execResult = executeInstruction(&processes[currentRunning - 1]);
                    numberOfInstructionsRan++;

                    if (execResult == EXEC_FINISHED) {
                        for (int p = 0; p < readyQueueSize - 1; p++)
                            readyQueue[p] = readyQueue[p + 1];
                        readyQueueSize--;
                        strcpy(processes[currentRunning - 1].pcb.state, "FINISHED");
                        freeProcess(&processes[currentRunning - 1]);
                        printf(">> Process %d has FINISHED (memory freed)\n", currentRunning);
                        printQueues(readyQueue, readyQueueSize,
                                   blockedQueueArr, blockedQueueSize, -1);
                        currentRunning = -1;
                        numberOfInstructionsRan = 0;
                    }
                    else if (execResult == EXEC_BLOCKED) {
                        for (int p = 0; p < readyQueueSize - 1; p++)
                            readyQueue[p] = readyQueue[p + 1];
                        readyQueueSize--;
                        blockedQueueArr[blockedQueueSize++] = currentRunning;
                        strcpy(processes[currentRunning - 1].pcb.state, "BLOCKED");
                        printf(">> Process %d is BLOCKED\n", currentRunning);
                        printQueues(readyQueue, readyQueueSize,
                                   blockedQueueArr, blockedQueueSize, -1);
                        currentRunning = -1;
                        numberOfInstructionsRan = 0;
                    }
                    else if (numberOfInstructionsRan >= RR_QUANTUM) {
                        int tempPid = readyQueue[0];
                        for (int p = 0; p < readyQueueSize - 1; p++)
                            readyQueue[p] = readyQueue[p + 1];
                        readyQueue[readyQueueSize - 1] = tempPid;
                        strcpy(processes[currentRunning - 1].pcb.state, "READY");
                        printf(">> Process %d time slice expired\n", currentRunning);
                        printQueues(readyQueue, readyQueueSize,
                                   blockedQueueArr, blockedQueueSize, -1);
                        currentRunning = -1;
                        numberOfInstructionsRan = 0;
                    }
                }
            }
        }

        else if (algo == 3) {
            /* ──── MLFQ (Multi-Level Feedback Queue) ──── */

            int selectedPID = -1;
            int selectedQueue = 0;

            /* Select from highest non-empty queue */
            if (queue1Size > 0) {
                selectedPID = queue1[0]; selectedQueue = 1;
            } else if (queue2Size > 0) {
                selectedPID = queue2[0]; selectedQueue = 2;
            } else if (queue3Size > 0) {
                selectedPID = queue3[0]; selectedQueue = 3;
            } else if (queue4Size > 0) {
                selectedPID = queue4[0]; selectedQueue = 4;
            }

            if (selectedPID != -1) {
                currentRunning = selectedPID;
                strcpy(processes[currentRunning - 1].pcb.state, "RUNNING");

                if (!ensureInMemory(currentRunning, processes, n, currentRunning)) {
                    strcpy(processes[currentRunning - 1].pcb.state, "READY");
                    currentRunning = -1;
                } else {
                    printf(">> Currently running: Process %d (Queue %d)\n",
                           currentRunning, selectedQueue);
                    int execResult = executeInstruction(&processes[currentRunning - 1]);

                    if (execResult == EXEC_FINISHED) {
                        strcpy(processes[currentRunning - 1].pcb.state, "FINISHED");
                        freeProcess(&processes[currentRunning - 1]);
                        processes[selectedPID - 1].quantumUsed = 0;
                        printf(">> Process %d has FINISHED (memory freed)\n", currentRunning);
                        switch (selectedQueue) {
                            case 1: removeFromQueue(queue1, &queue1Size, selectedPID); break;
                            case 2: removeFromQueue(queue2, &queue2Size, selectedPID); break;
                            case 3: removeFromQueue(queue3, &queue3Size, selectedPID); break;
                            case 4: removeFromQueue(queue4, &queue4Size, selectedPID); break;
                        }
                    }
                    else if (execResult == EXEC_BLOCKED) {
                        strcpy(processes[currentRunning - 1].pcb.state, "BLOCKED");
                        blockedQueueArr[blockedQueueSize++] = currentRunning;
                        processes[selectedPID - 1].quantumUsed = 0;
                        printf(">> Process %d is BLOCKED\n", currentRunning);
                        switch (selectedQueue) {
                            case 1: removeFromQueue(queue1, &queue1Size, selectedPID); break;
                            case 2: removeFromQueue(queue2, &queue2Size, selectedPID); break;
                            case 3: removeFromQueue(queue3, &queue3Size, selectedPID); break;
                            case 4: removeFromQueue(queue4, &queue4Size, selectedPID); break;
                        }
                    }
                    else {
                        /* Per-process quantum tracking */
                        processes[selectedPID - 1].quantumUsed++;

                        if (selectedQueue == 1) {
                            /* Q1 quantum = 1 (2^0): always demote after 1 instruction */
                            demoteProcess(queue1, queue2, &queue1Size, &queue2Size,
                                         processQueueLevel);
                            processes[selectedPID - 1].quantumUsed = 0;
                            printf(">> Process %d demoted from Q1 to Q2\n", selectedPID);
                        }
                        else if (selectedQueue == 2) {
                            /* Q2 quantum = 2 (2^1) */
                            if (processes[selectedPID - 1].quantumUsed >= 2) {
                                demoteProcess(queue2, queue3, &queue2Size, &queue3Size,
                                             processQueueLevel);
                                processes[selectedPID - 1].quantumUsed = 0;
                                printf(">> Process %d demoted from Q2 to Q3\n", selectedPID);
                            }
                        }
                        else if (selectedQueue == 3) {
                            /* Q3 quantum = 4 (2^2) */
                            if (processes[selectedPID - 1].quantumUsed >= 4) {
                                demoteProcess(queue3, queue4, &queue3Size, &queue4Size,
                                             processQueueLevel);
                                processes[selectedPID - 1].quantumUsed = 0;
                                printf(">> Process %d demoted from Q3 to Q4\n", selectedPID);
                            }
                        }
                        else if (selectedQueue == 4) {
                            /* Q4 quantum = 8 (2^3): RR rotation */
                            if (processes[selectedPID - 1].quantumUsed >= 8) {
                                rotateQueue4(queue4, &queue4Size);
                                processes[selectedPID - 1].quantumUsed = 0;
                                printf(">> Process %d rotated in Q4\n", selectedPID);
                            }
                        }
                    }
                }

                printMLFQQueues(queue1, queue1Size, queue2, queue2Size,
                               queue3, queue3Size, queue4, queue4Size,
                               blockedQueueArr, blockedQueueSize, currentRunning);
                currentRunning = -1;
            }
        }

        /* ── 4. Handle unblocked processes (from semSignal) ── */
        if (unblockedPID != -1) {
            /* Remove from blocked queue */
            for (int i = 0; i < blockedQueueSize; i++) {
                if (blockedQueueArr[i] == unblockedPID) {
                    for (int j = i; j < blockedQueueSize - 1; j++)
                        blockedQueueArr[j] = blockedQueueArr[j + 1];
                    blockedQueueSize--;
                    break;
                }
            }
            strcpy(processes[unblockedPID - 1].pcb.state, "READY");
            printf(">> Process %d UNBLOCKED -> moved to ready queue\n", unblockedPID);

            if (algo == 3) {
                /* Return to the queue at the process's current level */
                int level = processQueueLevel[unblockedPID - 1];
                switch (level) {
                    case 0: queue1[queue1Size++] = unblockedPID; break;
                    case 1: queue2[queue2Size++] = unblockedPID; break;
                    case 2: queue3[queue3Size++] = unblockedPID; break;
                    case 3: queue4[queue4Size++] = unblockedPID; break;
                }
                printMLFQQueues(queue1, queue1Size, queue2, queue2Size,
                               queue3, queue3Size, queue4, queue4Size,
                               blockedQueueArr, blockedQueueSize, -1);
            } else {
                readyQueue[readyQueueSize++] = unblockedPID;
                printQueues(readyQueue, readyQueueSize,
                           blockedQueueArr, blockedQueueSize, -1);
            }
        }

        /* ── 5. Sync PCBs and print memory ── */
        for (int i = 0; i < n; i++) {
            if (processes[i].isCreated && !processes[i].isSwappedOut) {
                syncPCBToMemory(&processes[i]);
            }
        }
        printMemory();

        tick++;
    }

    /* ── Simulation complete ── */
    printf("\n╔═══════════════════════════════════╗\n");
    printf("║     ALL PROCESSES FINISHED        ║\n");
    printf("╚═══════════════════════════════════╝\n");
    printf("Total clock cycles: %d\n", tick);
    printDisk();

    return 0;
}

/* ════════════════════════════════════════════════════════════════ */
/*                       HELPER FUNCTIONS                          */
/* ════════════════════════════════════════════════════════════════ */

bool isFinished(Process processes[], int n) {
    for (int i = 0; i < n; i++) {
        if (strcmp(processes[i].pcb.state, "FINISHED") != 0)
            return false;
    }
    return true;
}

int selectHRRN(int readyQueue[], int readyQueueSize, SchedulerInfo processList[], Process processes[]) {
    float maxRatio = -1;
    int maxProcess = readyQueue[0];
    for (int i = 0; i < readyQueueSize; i++) {
        int pid = readyQueue[i];
        int wt = processList[pid - 1].waitingTime;
        int bt = processes[pid - 1].codeLineCount - processes[pid - 1].pcb.programCounter;
        if (bt <= 0) bt = 1; /* safety: avoid division by zero */
        float ratio = (float)(wt + bt) / bt;
        if (ratio > maxRatio) {
            maxRatio = ratio;
            maxProcess = pid;
        }
    }
    printf("  HRRN: Process %d selected (ratio: %.2f)\n", maxProcess, maxRatio);
    return maxProcess;
}

void removeFromQueue(int queue[], int *queueSize, int pid) {
    for (int i = 0; i < *queueSize; i++) {
        if (queue[i] == pid) {
            for (int j = i; j < *queueSize - 1; j++)
                queue[j] = queue[j + 1];
            (*queueSize)--;
            return;
        }
    }
}

void demoteProcess(int currentQueue[], int nextQueue[],
                   int *currentQueueSize, int *nextQueueSize,
                   int processQueueLevel[]) {
    if (*currentQueueSize <= 0) return;
    int pid = currentQueue[0];
    /* Remove from front of current queue */
    for (int i = 0; i < *currentQueueSize - 1; i++)
        currentQueue[i] = currentQueue[i + 1];
    (*currentQueueSize)--;
    /* Add to end of next queue */
    nextQueue[*nextQueueSize] = pid;
    (*nextQueueSize)++;
    /* Update queue level */
    if (processQueueLevel[pid - 1] < 3)
        processQueueLevel[pid - 1]++;
}

void rotateQueue4(int queue4[], int *queue4Size) {
    if (*queue4Size <= 1) return;
    int first = queue4[0];
    for (int i = 0; i < *queue4Size - 1; i++)
        queue4[i] = queue4[i + 1];
    queue4[*queue4Size - 1] = first;
}

void printQueues(int readyQueue[], int readyQueueSize,
                 int blockedQueue[], int blockedQueueSize, int currentRunning) {
    printf("--- Queue Status ---\n");
    printf("Ready Queue:   [ ");
    for (int i = 0; i < readyQueueSize; i++)
        printf("P%d ", readyQueue[i]);
    printf("]\n");
    printf("Blocked Queue: [ ");
    if (blockedQueueSize == 0)
        printf("empty ");
    else
        for (int i = 0; i < blockedQueueSize; i++)
            printf("P%d ", blockedQueue[i]);
    printf("]\n");
    if (currentRunning != -1)
        printf("Running:       P%d\n", currentRunning);
    else
        printf("Running:       none\n");
    printf("--------------------\n");
}

void printMLFQQueues(int queue1[], int queue1Size,
                     int queue2[], int queue2Size,
                     int queue3[], int queue3Size,
                     int queue4[], int queue4Size,
                     int blockedQueue[], int blockedQueueSize, int currentRunning) {
    printf("--- MLFQ Queue Status ---\n");
    printf("Queue 1 (quantum 1): [ ");
    for (int i = 0; i < queue1Size; i++) printf("P%d ", queue1[i]);
    printf("]\n");
    printf("Queue 2 (quantum 2): [ ");
    for (int i = 0; i < queue2Size; i++) printf("P%d ", queue2[i]);
    printf("]\n");
    printf("Queue 3 (quantum 4): [ ");
    for (int i = 0; i < queue3Size; i++) printf("P%d ", queue3[i]);
    printf("]\n");
    printf("Queue 4 (quantum 8): [ ");
    for (int i = 0; i < queue4Size; i++) printf("P%d ", queue4[i]);
    printf("]\n");
    printf("Blocked Queue:       [ ");
    if (blockedQueueSize == 0)
        printf("empty ");
    else
        for (int i = 0; i < blockedQueueSize; i++)
            printf("P%d ", blockedQueue[i]);
    printf("]\n");
    if (currentRunning != -1)
        printf("Running:             P%d\n", currentRunning);
    else
        printf("Running:             none\n");
    printf("-------------------------\n");
}

int ensureInMemory(int pid, Process processes[], int n, int currentRunning) {
    if (!processes[pid - 1].isSwappedOut) return 1;

    /* First, free memory of any finished processes still in memory */
    for (int k = 0; k < n; k++) {
        if (processes[k].isCreated && !processes[k].isSwappedOut &&
            strcmp(processes[k].pcb.state, "FINISHED") == 0) {
            printf("[OS] Freeing memory of finished Process %d\n", processes[k].id);
            freeProcess(&processes[k]);
            /* Mark as "swapped out" so we don't try to access its memory */
            processes[k].isSwappedOut = 1;
        }
    }

    /* Try swap in */
    if (swapIn(&processes[pid - 1]) != -1) return 1;

    /* Not enough space - swap out a live process */
    for (int k = 0; k < n; k++) {
        if (processes[k].isCreated && !processes[k].isSwappedOut &&
            processes[k].id != pid &&
            strcmp(processes[k].pcb.state, "FINISHED") != 0) {
            swapOut(&processes[k]);
            if (swapIn(&processes[pid - 1]) != -1) return 1;
        }
    }

    printf("[OS ERROR] Cannot swap in Process %d\n", pid);
    return 0;
}
