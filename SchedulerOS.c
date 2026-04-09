#include <stdio.h>
#include <string.h>
#include <stdbool.h>

struct PcbDummy {
    int processID;
    char processState[10];
    int programCounter;
    int memoryLowerBound;
    int memoryUpperBound;
};

struct SchedulerInfo {
    int processID;
    int arrivalTime;
    int burstTime;
    int waitingTime;
};

bool isFinished(struct PcbDummy pcbList[]);
int selectHRRN(int readyQueue[], int readyQueueSize, struct SchedulerInfo processList[]);
int runInstruction(int currentRunning, struct PcbDummy pcbList[], struct SchedulerInfo processList[]);

int main() {
    int i = 0;
    int readyQueueSize = 0;
    int currentRunning = -1;
    int numberOfInstructionsRan = 0; // this variable is for the RR implementation to help

    struct PcbDummy pcbList[] = {
        {1, "waiting", 0, 0, 0},
        {2, "waiting", 0, 0, 0},
        {3, "waiting", 0, 0, 0}
    };

    struct SchedulerInfo processList[] = {
        {1, 0, 6, 0},
        {2, 1, 6, 0},
        {3, 4, 6, 0}
    };

    int readyQueue[3];
    int n = sizeof(processList) / sizeof(processList[0]); // size of processlist
    int algo;

    printf("Select your algorithm.\n Enter 1 for HRRN and 2 for RoundRobin\n");
    scanf("%d", &algo);

    while (!isFinished(pcbList)) {
        printf("current tick: %d\n", i);

        for (int j = 0; j < n; j++) {
            if (processList[j].arrivalTime == i && strcmp(pcbList[processList[j].processID - 1].processState, "waiting") == 0) {
                printf("process %d has arrived\n", processList[j].processID);
                readyQueue[readyQueueSize] = processList[j].processID;
                strcpy(pcbList[processList[j].processID - 1].processState, "ready");
                readyQueueSize++;
                // the above line loops through the process list that has reached the arrival time, then it will check that the state is waiting
                // it switches them to ready and adds them to readyqueue and increments readyqueuesize
            }
        }

        for (int k = 0; k < readyQueueSize; k++) {
            int processIDtemp = readyQueue[k];
            processList[processIDtemp - 1].waitingTime += 1;
            // increments the waiting time each clock cycle for "ready" processes
        }

        if (algo == 1) {
            if (currentRunning == -1 && readyQueueSize > 0) {
                bool foundCurrentRunning = false;
                currentRunning = selectHRRN(readyQueue, readyQueueSize, processList);
                strcpy(pcbList[currentRunning - 1].processState, "running");
                for (int m = 0; m < readyQueueSize - 1; m++) {
                    if (readyQueue[m] == currentRunning) {
                        foundCurrentRunning = true;
                    }
                    if (foundCurrentRunning) {
                        readyQueue[m] = readyQueue[m + 1];
                    }
                }
                readyQueueSize -= 1;
            }

            if (currentRunning != -1) {
                if (pcbList[currentRunning - 1].programCounter == processList[currentRunning - 1].burstTime) {
                    strcpy(pcbList[currentRunning - 1].processState, "finished");
                    printf("process %d has finished\n", currentRunning);
                    currentRunning = -1;
                } else {
                    pcbList[currentRunning - 1].programCounter += 1;
                }
            }
        }

        else if (algo == 2) {
            int runInstructionOutput;
            int tempProcessId;

            if (readyQueueSize > 0) {
                currentRunning = readyQueue[0];
                runInstructionOutput = runInstruction(readyQueue[0], pcbList, processList);
                numberOfInstructionsRan++;
                printf("no.instr ran %d\n", numberOfInstructionsRan);

                if (runInstructionOutput == 0) {
                    for (int p = 0; p < readyQueueSize - 1; p++) {
                        readyQueue[p] = readyQueue[p + 1];
                    }
                    readyQueueSize -= 1;
                    currentRunning = -1;
                    numberOfInstructionsRan = 0;
                }
                else if (numberOfInstructionsRan == 2) {
                    tempProcessId = readyQueue[0];
                    for (int p = 0; p < readyQueueSize - 1; p++) {
                        readyQueue[p] = readyQueue[p + 1];
                    }
                    readyQueue[readyQueueSize - 1] = tempProcessId;
                    // shift queue left and place process at back
                    currentRunning = -1;
                    numberOfInstructionsRan = 0;
                }
            }

            printf("Ready queue 0 has: %d\n", readyQueue[0]);
        }

        i++;
    }

    return 0;
}

bool isFinished(struct PcbDummy pcbList[]) {
    bool flag = true;
    for (int i = 0; i < 3; i++) {
        if (strcmp(pcbList[i].processState, "finished") != 0) {
            flag = false;
        }
    }
    if (flag) {
        return true;
    }
    return false;
    // this function just checks if all the processes are finished
}

int selectHRRN(int readyQueue[], int readyQueueSize, struct SchedulerInfo processList[]) {
    float maxratio = 0;
    int maxProcess = 0;
    for (int i = 0; i < readyQueueSize; i++) {
        int tempWaitTime = processList[readyQueue[i] - 1].waitingTime;
        int tempBurstTime = processList[readyQueue[i] - 1].burstTime;
        float tempResponseRatio = (float)(tempWaitTime + tempBurstTime) / tempBurstTime;
        if (tempResponseRatio > maxratio) {
            maxratio = tempResponseRatio;
            maxProcess = processList[readyQueue[i] - 1].processID;
        }
        // we collected the waiting and bursttime now we do (waitingTime + burstTime) / burstTime
        // for each process until we find the highest ratio and return it
    }
    printf("process %d has been selected to run\n", maxProcess);
    return maxProcess;
}

int runInstruction(int currentRunning, struct PcbDummy pcbList[], struct SchedulerInfo processList[]) {
    pcbList[currentRunning - 1].programCounter += 1;
    if (pcbList[currentRunning - 1].programCounter == processList[currentRunning - 1].burstTime) {
        strcpy(pcbList[currentRunning - 1].processState, "finished");
        printf("process %d has finished\n", currentRunning);
        return 0;
    } else {
        return 1;
    }
}