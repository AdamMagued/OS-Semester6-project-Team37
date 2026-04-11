#ifndef MEMORY_H
#define MEMORY_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MEMORY_SIZE 40
#define MAX_CODE_LINES 20
#define MAX_PROCESSES 3
#define MAX_VARS 3
#define PCB_FIELDS 5
#define NAME_LENGTH 50
#define VALUE_LENGTH 256
#define MAX_QUEUE 10

/* ── Memory Word ── */
typedef struct {
    char name[NAME_LENGTH];
    char value[VALUE_LENGTH];
    int isFree;
} MemoryWord;

/* ── Process Control Block ── */
typedef struct {
    int processId;
    char state[20];
    int programCounter;
    int lowerBound;
    int upperBound;
} PCB;

/* ── Process ── */
typedef struct {
    int id;
    int arrivalTime;
    char codeLines[MAX_CODE_LINES][VALUE_LENGTH];
    int codeLineCount;
    PCB pcb;
    int isSwappedOut;
    int isCreated;
} Process;

/* ── Scheduler Tracking ── */
typedef struct {
    int processID;
    int arrivalTime;
    int burstTime;
    int waitingTime;
} SchedulerInfo;

/* ── Disk Entry for Swapping ── */
typedef struct {
    int processId;
    MemoryWord block[MEMORY_SIZE];
    int blockSize;
    int isOccupied;
} DiskEntry;

/* ── Globals (defined in memory.c) ── */
extern MemoryWord memory[MEMORY_SIZE];
extern DiskEntry disk[MAX_PROCESSES];

/* ── Memory Functions ── */
void initMemory(void);
void writeWord(int index, const char *name, const char *value);
char* readWordValue(int index);
int allocateProcess(Process *p);
void freeProcess(Process *p);
char* getVariable(Process *p, const char *varName);
void setVariable(Process *p, const char *varName, const char *value);
void swapOut(Process *p);
int swapIn(Process *p);
void syncPCBToMemory(Process *p);
void printMemory(void);
void printDisk(void);
int loadProgram(Process *p, const char *filename);

#endif
