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
int selectMLFQ(int queue1[],int queue2[],int queue3[],int queue4[],int *queue1Size,int *queue2Size,int *queue3Size, int *queue4Size, struct PcbDummy pcbList[],int processQueueLevel[], struct SchedulerInfo processList[], int *NIR2, int *NIR3, int *NIR4);
void demoteProcess(int currentQueue[],int nextQueue[],int *currentQueueSize, int *nextQueueSize, int processQueueLevel[]);
void rotateQueue4(int queue4[], int *queue4Size);

int runInstruction(int currentRunning, struct PcbDummy pcbList[], struct SchedulerInfo processList[]);

int main() {

    int NIR2=0;
    int NIR3=0;
    int NIR4=0;
    int i = 0;
    int readyQueueSize = 0;
    int currentRunning = -1;
    int numberOfInstructionsRan = 0; // this variable is for the RR implementation to help

    //these  nextdeclarations are for the MLFQ implementation

    int queue1[3];
    int queue2[3];
    int queue3[3];
    int queue4[3];
    int queue1Size = 0;
    int queue2Size = 0;
    int queue3Size = 0;
    int queue4Size = 0;
    int processQueueLevel[3] = {0, 0, 0};

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

    printf("Select your algorithm.\n Enter 1 for HRRN and 2 for RoundRobin, 3 for MLFQ\n");
    scanf("%d", &algo);

    while (!isFinished(pcbList)) {
        printf("current tick: %d\n", i);

        for (int j = 0; j < n; j++) {
            if (processList[j].arrivalTime == i && strcmp(pcbList[processList[j].processID - 1].processState, "waiting") == 0) {
                printf("process %d has arrived\n", processList[j].processID);

                if(algo==3){
                    queue1[queue1Size]= processList[j].processID;
                    strcpy(pcbList[processList[j].processID - 1].processState, "ready");
                    queue1Size++;
                }
                
                else{
                
                readyQueue[readyQueueSize] = processList[j].processID;
                strcpy(pcbList[processList[j].processID - 1].processState, "ready");
                readyQueueSize++;}
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
            // ROUND ROBIN
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

        else if(algo==3){

            selectMLFQ(queue1,queue2,queue3,queue4,&queue1Size,&queue2Size,&queue3Size,&queue4Size,pcbList,processQueueLevel,processList,&NIR2,&NIR3,&NIR4);

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




int selectMLFQ(int queue1[],int queue2[],int queue3[],int queue4[],int *queue1Size,int *queue2Size,int *queue3Size, int *queue4Size, struct PcbDummy pcbList[],int processQueueLevel[], struct SchedulerInfo processList[], int *NIR2, int *NIR3, int*NIR4){
    

    if(*queue1Size>0){
        int tempProcId=queue1[0];
        if(runInstruction(queue1[0], pcbList, processList)==0){

            
            //this means the instruction finished, so cycle the queue and remove it completely
            for(int i=0;i<*queue1Size;i++){
                queue1[i]=queue1[i+1];

            }
            (*queue1Size)--;
            return tempProcId;
        }
        else{
            
            demoteProcess(queue1,queue2,queue1Size,queue2Size,processQueueLevel);
            printf("process %d has been demoted\n",tempProcId);
            return tempProcId;
            
        }

    }
    else if( *queue2Size>0){

        int tempProcId=queue2[0];

        if(runInstruction(queue2[0],pcbList,processList)==0){
            for(int i=0;i<*queue2Size;i++){
                queue2[i]=queue2[i+1];

            }
            (*queue2Size)--;
            return tempProcId;

        }else{
            (*NIR2)++;
            if(*NIR2==2){
                demoteProcess(queue2,queue3,queue2Size,queue3Size,processQueueLevel);
                *NIR2=0;
                printf("process %d has been demoted\n",tempProcId);
            return tempProcId;
            }
            return tempProcId;
            
        }

} else if (*queue3Size>0){

        int tempProcId=queue3[0];
    if(runInstruction(queue3[0],pcbList,processList)==0){
        for(int i=0;i<*queue3Size;i++){
                queue3[i]=queue3[i+1];


            }
            (*queue3Size)--;

            return tempProcId;


    }
    else{
        (*NIR3)++;
        if(*NIR3==4){
            demoteProcess(queue3,queue4,queue3Size,queue4Size,processQueueLevel);
            *NIR3=0;
            printf("process %d has been demoted\n",tempProcId);
            return tempProcId;

        }
        return tempProcId;
        

    }
}else if(*queue4Size>0){

        int tempProcId=queue4[0];
    if(runInstruction(queue4[0],pcbList,processList)==0){
        for(int i=0;i<*queue4Size;i++){
                queue4[i]=queue4[i+1];

            }
            (*queue4Size)--;
            return tempProcId;


    }
    else{
        (*NIR4)++;
        if(*NIR4==8){
            rotateQueue4(queue4,queue4Size);
            *NIR4=0;
            printf("queue %d has been sent to the back of the queue\n",tempProcId);
            return tempProcId;
        }
        return tempProcId;

        
    }


}
return -1;

}

void demoteProcess(int currentQueue[],int nextQueue[],int *currentQueueSize, int *nextQueueSize, int processQueueLevel[]){
    if(processQueueLevel[currentQueue[0]-1]!=3){ // if we are NOT in the last queue:
    int tempProcId=currentQueue[0];
    for(int i=0;i<*currentQueueSize-1;i++){
        currentQueue[i]=currentQueue[i+1];
    } // shift current queue to the left, overwrite current index 0
    *currentQueueSize-=1; 
    nextQueue[*nextQueueSize]=tempProcId; //place the demoted in end of next queue
    *nextQueueSize+=1;
processQueueLevel[tempProcId-1]++; //update the process queue level


}
    else{ // if we are in the last queue, we just place in back of current q
        int tempProcId=currentQueue[0];
    for(int i=0;i<*currentQueueSize-1;i++){
        currentQueue[i]=currentQueue[i+1];
    }
    currentQueue[*currentQueueSize]=tempProcId;
    



    }



}

void rotateQueue4(int queue4[], int *queue4Size){
    int tempProcId = queue4[0];
    for(int i = 0; i < *queue4Size; i++){
        queue4[i] = queue4[i+1];
    }
    queue4[*queue4Size - 1] = tempProcId;
}

