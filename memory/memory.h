#ifndef MEMORY_H
#define MEMORY_H

#include <stdio.h>
#include <string.h>

#define MEMORY_SIZE 40
#define MAX_CODE_LINES 20
#define MAX_PROCESSES 10
#define WORD_LENGTH 50

typedef struct {
    char name[WORD_LENGTH];
    char value[WORD_LENGTH];
    int isFree;
} MemoryWord;

typedef struct {
    int processId;
    char state[20];
    int programCounter;
    int lowerBound;
    int upperBound;
} PCB;

typedef struct {
    int id;
    int arrivalTime;
    char codeLines[MAX_CODE_LINES][WORD_LENGTH];
    int codeLineCount;
    PCB pcb;
} Process;

MemoryWord memory[MEMORY_SIZE];

typedef struct {
    int processId;
    MemoryWord block[MEMORY_SIZE];
    int blockSize;
    int isOccupied;
} DiskEntry;

DiskEntry disk[MAX_PROCESSES];

#endif