#include "memory.h"

MemoryWord memory[MEMORY_SIZE];
DiskEntry disk[MAX_PROCESSES];

void initMemory(void) {
    for (int i = 0; i < MEMORY_SIZE; i++) {
        strcpy(memory[i].name, "");
        strcpy(memory[i].value, "");
        memory[i].isFree = 1;
    }
    for (int i = 0; i < MAX_PROCESSES; i++) {
        disk[i].isOccupied = 0;
        disk[i].processId = -1;
    }
    printf("[OS] Memory initialized: %d words available\n", MEMORY_SIZE);
}

void writeWord(int index, const char *name, const char *value) {
    strncpy(memory[index].name, name, NAME_LENGTH - 1);
    memory[index].name[NAME_LENGTH - 1] = '\0';
    strncpy(memory[index].value, value, VALUE_LENGTH - 1);
    memory[index].value[VALUE_LENGTH - 1] = '\0';
    memory[index].isFree = 0;
}

char* readWordValue(int index) {
    if (memory[index].isFree) return NULL;
    return memory[index].value;
}

int allocateProcess(Process *p) {
    int spaceNeeded = p->codeLineCount + MAX_VARS + PCB_FIELDS;

    /* Find contiguous free block */
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
    char buffer[VALUE_LENGTH];

    /* Write code lines */
    for (int i = 0; i < p->codeLineCount; i++) {
        char codeName[NAME_LENGTH];
        sprintf(codeName, "Instruction_%d", i);
        writeWord(index++, codeName, p->codeLines[i]);
    }

    /* Write 3 empty variable slots */
    for (int i = 0; i < MAX_VARS; i++)
        writeWord(index++, "", "");

    /* Write PCB fields */
    sprintf(buffer, "%d", p->id);
    writeWord(index++, "PCB_ID", buffer);
    writeWord(index++, "PCB_State", "READY");
    sprintf(buffer, "%d", p->pcb.programCounter);
    writeWord(index++, "PCB_PC", buffer);
    sprintf(buffer, "%d", start);
    writeWord(index++, "PCB_LowerBound", buffer);
    sprintf(buffer, "%d", start + spaceNeeded - 1);
    writeWord(index++, "PCB_UpperBound", buffer);

    /* Update PCB struct */
    p->pcb.processId = p->id;
    strcpy(p->pcb.state, "READY");
    p->pcb.lowerBound = start;
    p->pcb.upperBound = start + spaceNeeded - 1;
    p->isSwappedOut = 0;

    return start;
}

void freeProcess(Process *p) {
    for (int i = p->pcb.lowerBound; i <= p->pcb.upperBound; i++) {
        strcpy(memory[i].name, "");
        strcpy(memory[i].value, "");
        memory[i].isFree = 1;
    }
}

char* getVariable(Process *p, const char *varName) {
    int varStart = p->pcb.lowerBound + p->codeLineCount;
    int varEnd = varStart + MAX_VARS;
    for (int i = varStart; i < varEnd; i++) {
        if (strcmp(memory[i].name, varName) == 0)
            return memory[i].value;
    }
    return NULL;
}

void setVariable(Process *p, const char *varName, const char *value) {
    int varStart = p->pcb.lowerBound + p->codeLineCount;
    int varEnd = varStart + MAX_VARS;

    /* Check if variable already exists */
    for (int i = varStart; i < varEnd; i++) {
        if (strcmp(memory[i].name, varName) == 0) {
            strncpy(memory[i].value, value, VALUE_LENGTH - 1);
            memory[i].value[VALUE_LENGTH - 1] = '\0';
            return;
        }
    }

    /* Find first empty variable slot */
    for (int i = varStart; i < varEnd; i++) {
        if (strcmp(memory[i].name, "") == 0) {
            strncpy(memory[i].name, varName, NAME_LENGTH - 1);
            memory[i].name[NAME_LENGTH - 1] = '\0';
            strncpy(memory[i].value, value, VALUE_LENGTH - 1);
            memory[i].value[VALUE_LENGTH - 1] = '\0';
            return;
        }
    }

    printf("[OS ERROR] No free variable slots for process %d\n", p->id);
}

void syncPCBToMemory(Process *p) {
    if (p->isSwappedOut) return;
    int pcbStart = p->pcb.lowerBound + p->codeLineCount + MAX_VARS;
    char buf[VALUE_LENGTH];

    sprintf(buf, "%d", p->pcb.processId);
    strncpy(memory[pcbStart].value, buf, VALUE_LENGTH - 1);
    strncpy(memory[pcbStart + 1].value, p->pcb.state, VALUE_LENGTH - 1);
    sprintf(buf, "%d", p->pcb.programCounter);
    strncpy(memory[pcbStart + 2].value, buf, VALUE_LENGTH - 1);
    sprintf(buf, "%d", p->pcb.lowerBound);
    strncpy(memory[pcbStart + 3].value, buf, VALUE_LENGTH - 1);
    sprintf(buf, "%d", p->pcb.upperBound);
    strncpy(memory[pcbStart + 4].value, buf, VALUE_LENGTH - 1);
}

void swapOut(Process *p) {
    syncPCBToMemory(p);

    int diskSlot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (disk[i].isOccupied == 0) {
            diskSlot = i;
            break;
        }
    }
    if (diskSlot == -1) {
        printf("[OS ERROR] Disk full! Cannot swap out process %d\n", p->id);
        return;
    }

    int blockSize = p->pcb.upperBound - p->pcb.lowerBound + 1;
    for (int i = 0; i < blockSize; i++)
        disk[diskSlot].block[i] = memory[p->pcb.lowerBound + i];

    disk[diskSlot].processId = p->id;
    disk[diskSlot].blockSize = blockSize;
    disk[diskSlot].isOccupied = 1;

    printf("[OS] >>> Process %d SWAPPED OUT to disk (slot %d) <<<\n", p->id, diskSlot);
    printDisk();

    freeProcess(p);
    p->isSwappedOut = 1;
}

int swapIn(Process *p) {
    int diskSlot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (disk[i].processId == p->id && disk[i].isOccupied == 1) {
            diskSlot = i;
            break;
        }
    }
    if (diskSlot == -1) {
        printf("[OS ERROR] Process %d not found on disk\n", p->id);
        return -1;
    }

    int blockSize = disk[diskSlot].blockSize;

    /* Find contiguous free block */
    int start = -1, freeCount = 0;
    for (int i = 0; i < MEMORY_SIZE; i++) {
        if (memory[i].isFree) {
            if (freeCount == 0) start = i;
            freeCount++;
            if (freeCount == blockSize) break;
        } else {
            freeCount = 0;
            start = -1;
        }
    }
    if (start == -1 || freeCount < blockSize) return -1;

    /* Restore memory block from disk */
    for (int i = 0; i < blockSize; i++)
        memory[start + i] = disk[diskSlot].block[i];

    /* Update PCB bounds in memory (new location) */
    int pcbStart = start + p->codeLineCount + MAX_VARS;
    char buf[VALUE_LENGTH];
    sprintf(buf, "%d", start);
    strncpy(memory[pcbStart + 3].value, buf, VALUE_LENGTH - 1);
    sprintf(buf, "%d", start + blockSize - 1);
    strncpy(memory[pcbStart + 4].value, buf, VALUE_LENGTH - 1);

    /* Update Process struct */
    p->pcb.lowerBound = start;
    p->pcb.upperBound = start + blockSize - 1;
    p->isSwappedOut = 0;

    /* Free disk slot */
    disk[diskSlot].isOccupied = 0;
    disk[diskSlot].processId = -1;

    printf("[OS] >>> Process %d SWAPPED IN from disk (slot %d) to memory [%d-%d] <<<\n",
           p->id, diskSlot, start, start + blockSize - 1);
    printDisk();
    return start;
}

void printMemory(void) {
    printf("\n========== MEMORY STATE ==========\n");
    printf("%-5s | %-20s | %s\n", "Slot", "Name", "Value");
    printf("------|----------------------|--------------------\n");
    for (int i = 0; i < MEMORY_SIZE; i++) {
        if (memory[i].isFree)
            printf("%-5d | %-20s | %s\n", i, "(free)", "");
        else
            printf("%-5d | %-20s | %s\n", i, memory[i].name, memory[i].value);
    }
    printf("===================================\n");
}

void printDisk(void) {
    printf("\n========== DISK STATE ==========\n");
    int anyOccupied = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (disk[i].isOccupied) {
            anyOccupied = 1;
            printf("Disk Slot %d: Process %d (%d words)\n",
                   i, disk[i].processId, disk[i].blockSize);
            for (int j = 0; j < disk[i].blockSize; j++) {
                printf("  [%2d] %-20s = %s\n",
                       j, disk[i].block[j].name, disk[i].block[j].value);
            }
        }
    }
    if (!anyOccupied) printf("  (empty)\n");
    printf("=================================\n");
}

int loadProgram(Process *p, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        printf("[OS ERROR] Cannot open program file '%s'\n", filename);
        return -1;
    }

    p->codeLineCount = 0;
    char line[VALUE_LENGTH];
    while (fgets(line, sizeof(line), fp) != NULL && p->codeLineCount < MAX_CODE_LINES) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';
        if (strlen(line) > 0) {
            strcpy(p->codeLines[p->codeLineCount], line);
            p->codeLineCount++;
        }
    }
    fclose(fp);
    printf("[OS] Loaded program '%s': %d instructions\n", filename, p->codeLineCount);
    return p->codeLineCount;
}
