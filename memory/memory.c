#include "memory.h"

void initMemory() {
    for (int i = 0; i < MEMORY_SIZE; i++) {
        strcpy(memory[i].name, "");
        strcpy(memory[i].value, "");
        memory[i].isFree = 1;
    }
}

void writeWord(int index, char *name, char *value) {
    strcpy(memory[index].name, name);
    strcpy(memory[index].value, value);
    memory[index].isFree = 0;
}

char* readWord(int index) {
    if (memory[index].isFree) return NULL;
    return memory[index].value;
}

int allocateProcess(Process *p) {
    int spaceNeeded = p->codeLineCount + 3 + 5; // code + 3 vars + 5 PCB fields

    // find contiguous free block
    int start = -1, freeCount = 0;
    for (int i = 0; i < MEMORY_SIZE; i++) {
        if (memory[i].isFree) {
            if (freeCount == 0) start = i;
            freeCount++;
            if (freeCount == spaceNeeded) break;
        } else {
            freeCount = 0;
            start = -1;
        }
    }

    if (start == -1 || freeCount < spaceNeeded) return -1;

    int index = start;
    char buffer[WORD_LENGTH];

    // write code lines
    for (int i = 0; i < p->codeLineCount; i++)
        writeWord(index++, "code", p->codeLines[i]);

    // write 3 empty variable slots
    for (int i = 0; i < 3; i++)
        writeWord(index++, "", "");

    // write PCB
    sprintf(buffer, "%d", p->id);
    writeWord(index++, "processId", buffer);
    writeWord(index++, "state", "Ready");
    writeWord(index++, "programCounter", "0");
    sprintf(buffer, "%d", start);
    writeWord(index++, "lowerBound", buffer);
    sprintf(buffer, "%d", start + spaceNeeded - 1);
    writeWord(index++, "upperBound", buffer);

    // update PCB struct
    p->pcb.processId = p->id;
    strcpy(p->pcb.state, "Ready");
    p->pcb.programCounter = 0;
    p->pcb.lowerBound = start;
    p->pcb.upperBound = start + spaceNeeded - 1;

    return start;
}

void freeProcess(Process *p) {
    for (int i = p->pcb.lowerBound; i <= p->pcb.upperBound; i++) {
        strcpy(memory[i].name, "");
        strcpy(memory[i].value, "");
        memory[i].isFree = 1;
    }
}

char* getVariable(Process *p, char *varName) {
    for (int i = p->pcb.lowerBound; i <= p->pcb.upperBound; i++) {
        if (strcmp(memory[i].name, varName) == 0)
            return memory[i].value;
    }
    return NULL; // variable not found
}

void setVariable(Process *p, char *varName, char *value) {
    for (int i = p->pcb.lowerBound; i <= p->pcb.upperBound; i++) {
        if (strcmp(memory[i].name, varName) == 0) {
            strcpy(memory[i].value, value);
            return;
        }
    }
    // variable doesn't exist yet, write to first empty variable slot
    for (int i = p->pcb.lowerBound; i <= p->pcb.upperBound; i++) {
        if (strcmp(memory[i].name, "") == 0 && memory[i].isFree == 0) {
            strcpy(memory[i].name, varName);
            strcpy(memory[i].value, value);
            return;
        }
    }
}

void swapOut(Process *p) {
    // find a free disk slot
    int diskSlot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (disk[i].isOccupied == 0) {
            diskSlot = i;
            break;
        }
    }

    if (diskSlot == -1) {
        printf("Disk is full!\n");
        return;
    }

    // copy process's memory block to disk
    int blockSize = p->pcb.upperBound - p->pcb.lowerBound + 1;
    for (int i = 0; i < blockSize; i++)
        disk[diskSlot].block[i] = memory[p->pcb.lowerBound + i];

    disk[diskSlot].processId = p->id;
    disk[diskSlot].blockSize = blockSize;
    disk[diskSlot].isOccupied = 1;

    // free the memory
    freeProcess(p);
}

void swapIn(Process *p) {
    // find the process on disk
    int diskSlot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (disk[i].processId == p->id && disk[i].isOccupied == 1) {
            diskSlot = i;
            break;
        }
    }

    if (diskSlot == -1) {
        printf("Process not found on disk!\n");
        return;
    }

    // allocate space in memory
    int start = allocateProcess(p);
    if (start == -1) {
        printf("Not enough memory to swap in!\n");
        return;
    }

    // restore the block from disk
    for (int i = 0; i < disk[diskSlot].blockSize; i++)
        memory[start + i] = disk[diskSlot].block[i];

    // free the disk slot
    disk[diskSlot].isOccupied = 0;
}

void printMemory() {
    printf("\n========== MEMORY STATE ==========\n");
    for (int i = 0; i < MEMORY_SIZE; i++) {
        if (memory[i].isFree)
            printf("Slot %d: FREE\n", i);
        else
            printf("Slot %d: [%s] = [%s]\n", i, memory[i].name, memory[i].value);
    }
    printf("===================================\n");
}