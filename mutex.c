#include <stdio.h>

#define MAX_QUEUE 10
#define MAX_PROCESSES 10

// --- Enums & Structs ---

typedef enum { NEW, READY, RUNNING, BLOCKED, FINISHED } ProcessState;

// Process Control Block
typedef struct {
    int processID;          
    ProcessState state;     
    int programCounter;     
    int memLowerBound;      
    int memUpperBound;      
} PCB;

PCB processTable[MAX_PROCESSES];

// --- Queues ---

int readyQueue[MAX_QUEUE];
int rq_front = 0, rq_rear = 0;

int blockedQueue[MAX_QUEUE];
int bq_front = 0, bq_rear = 0;

// --- Mutex ---

typedef struct {
    int locked;           // 0 = free, 1 = taken
    int owner;            // pid of the lock holder

    int queue[MAX_QUEUE]; // processes waiting for this specific resource
    int front;
    int rear;
} Mutex;

// --- Queue Helpers ---

int isQueueFull(int front, int rear) {
    return ((rear + 1) % MAX_QUEUE) == front;
}

int isQueueEmpty(int front, int rear) {
    return front == rear;
}

void enqueue(int queue[MAX_QUEUE], int *rear, int value) {
    queue[*rear] = value;
    *rear = (*rear + 1) % MAX_QUEUE;
}

int dequeue(int queue[MAX_QUEUE], int *front) {
    int value = queue[*front];
    *front = (*front + 1) % MAX_QUEUE;
    return value;
}

// --- State Management ---

void setProcessState(int pid, ProcessState newState) {
    processTable[pid].state = newState;
    
    // TODO: add queue printing here later for the project output reqs
    if (newState == BLOCKED) {
        printf("Process %d -> BLOCKED\n", pid);
    } else if (newState == READY) {
        printf("Process %d -> READY\n", pid);
    }
}

// shift elements to remove a pid without leaving holes in the array

//not sure of this part....
void removeFromBlockedQueue(int pid) {
    int newQueue[MAX_QUEUE];
    int newRear = 0;

    for (int i = bq_front; i != bq_rear; i = (i + 1) % MAX_QUEUE) {
        if (blockedQueue[i] != pid) {
            newQueue[newRear++] = blockedQueue[i];
        }
    }

    // copy back
    for (int i = 0; i < newRear; i++) {
        blockedQueue[i] = newQueue[i];
    }

    bq_front = 0;
    bq_rear = newRear;
}

// --- Mutex Operations ---

void semWait(Mutex *m, int pid) {
    if (m->locked == 0) {
        // grab the lock
        m->locked = 1;
        m->owner = pid;
        printf("Process %d acquired resource\n", pid);
    } else {
        // resource taken, add to waitlists
        if (isQueueFull(m->front, m->rear)) {
            printf("Mutex queue full!\n");
            return;
        }

        enqueue(m->queue, &m->rear, pid);

        // sync with global blocked queue
        if (!isQueueFull(bq_front, bq_rear)) {
            enqueue(blockedQueue, &bq_rear, pid);
        }

        setProcessState(pid, BLOCKED);
    }
}

void semSignal(Mutex *m, int calling_pid) {
    // block unauthorized unlocking
    if (m->locked == 1 && m->owner != calling_pid) {
        printf("Error: Process %d tyna unlock unowned mutex\n", calling_pid);
        return;
    }

    if (!isQueueEmpty(m->front, m->rear)) {
        // pass lock to next in line
        int next = dequeue(m->queue, &m->front);
        m->locked = 1;
        m->owner = next;

        removeFromBlockedQueue(next);

        if (!isQueueFull(rq_front, rq_rear)) {
            enqueue(readyQueue, &rq_rear, next);
        }

        setProcessState(next, READY);
        printf("Process %d is awake and just got the resource\n", next);

    } else {
        // nobody waiting
        m->locked = 0;
        m->owner = -1;
        printf("Resource freed\n");
    }
}

// --- Test Main ---
//*
//int main() {
    // init dummy processes
//    for(int i = 0; i < MAX_PROCESSES; i++) {
  //      processTable[i].processID = i;
  //      processTable[i].state = NEW;
 //   }
//
  //  Mutex userOutput = {0, -1, {0}, 0, 0};
//
  //  semWait(&userOutput, 1); // 1 gets it
  //  semWait(&userOutput, 2); // 2 blocks
 //   semWait(&userOutput, 3); // 3 blocks

  //  printf("\n--- releasing ---\n\n");

   // semSignal(&userOutput, 2); // fail
   // semSignal(&userOutput, 1); // 1 releases, 2 gets it
   // semSignal(&userOutput, 2); // 2 releases, 3 gets it
   // semSignal(&userOutput, 3); // 3 releases, free

    return 0;
}//*