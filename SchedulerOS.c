#include <stdio.h>
#include <string.h>

struct PcbDummy{
    int processID;
    char processState[10];
    int programCounter;
    int memoryLowerBound;
    int memoryUpperBound;
    


};

struct SchedulerInfo{
    int processID;
    int arrivalTime;
    int burstTime;
    int waitingTime;
    


};

int main() {
    int i=0;
    int readyQueueSize=0;


    struct  PcbDummy pcbList[] = {
        {1,"waiting",0,0,0},
        {2,"waiting",0,0,0},
        {3,"waiting",0,0,0}

    };


    struct SchedulerInfo processList[] = {
        {1,0,6,0},
        {2,1,6,0},
        {3,4,6,0}

    };

    int readyQueue [3];

    

    int n= sizeof(processList)/sizeof(processList[0]);

    while(!isFinished()){
        printf("current tick: %d\n", i);
        for(int j=0;j<n;j++){
            if(processList[j].arrivalTime==i&&strcmp(pcbList[processList[j].processID-1].processState,"waiting")==0){
                readyQueue[readyQueueSize]=processList[j].processID;
                strcpy(pcbList[processList[j].processID-1].processState,"ready");
                readyQueueSize++;

            }
        
        }

        for(int k=0;k<readyQueueSize;k++){
            int processIDtemp= readyQueue[k];
            processList[processIDtemp-1].waitingTime +=1;

        }
        i++;



    }
    return 0;


}