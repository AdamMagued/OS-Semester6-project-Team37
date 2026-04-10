#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define MAX_ARGS 10
#define MAX_LINE 1000
#define MAX_MEM 100

// ---------------- SIMULATED MEMORY ----------------
typedef struct {
    char name[50];
    char value[50];
} MemoryCell;

MemoryCell memory[MAX_MEM];
int memSize = 0;

// set memory (store/update variable)
void setMemory(char *name, char *value) {

    if (memSize >= MAX_MEM) {
        printf("Memory Full!\n");
        return;
    }

    for (int i = 0; i < memSize; i++) {
        if (strcmp(memory[i].name, name) == 0) {
            strcpy(memory[i].value, value);
            return;
        }
    }

    strcpy(memory[memSize].name, name);
    strcpy(memory[memSize].value, value);
    memSize++;
}

// get memory value
char* getMemory(char *name) {
    for (int i = 0; i < memSize; i++) {
        if (strcmp(memory[i].name, name) == 0) {
            return memory[i].value;
        }
    }
    return NULL;
}

// ---------------- BURST TIME ----------------
int calculateBurstTime(char program[]) {

    int bt = 0;
    char temp[MAX_LINE];
    strcpy(temp, program);

    char *line = strtok(temp, "\n");

    while (line != NULL) {
        bt++;
        line = strtok(NULL, "\n");
    }

    return bt;
}

// ---------------- PARSER ----------------
void parseLine(char *line, char *cmd, char args[MAX_ARGS][50], int *argCount) {

    *argCount = 0;

    char *token = strtok(line, " ");
    strcpy(cmd, token);

    while ((token = strtok(NULL, " ")) != NULL) {
        strcpy(args[*argCount], token);
        (*argCount)++;
    }
}

// ---------------- SYSTEM CALLS ----------------
void sys_printFromTo(char args[MAX_ARGS][50]) {

    char *v1 = getMemory(args[0]);
    char *v2 = getMemory(args[1]);

    int x = v1 ? atoi(v1) : 0;
    int y = v2 ? atoi(v2) : 0;

    printf("[SYS_CALL] printFromTo\n");

    for (int i = x; i <= y; i++) {
        printf("%d\n", i);
    }
}

void sys_semWait(char args[MAX_ARGS][50]) {
    printf("[SYS_CALL] semWait %s\n", args[0]);
}

void sys_semSignal(char args[MAX_ARGS][50]) {
    printf("[SYS_CALL] semSignal %s\n", args[0]);
}

// PRINT
void sys_print(char args[MAX_ARGS][50]) {
    for (int i = 0; i < MAX_ARGS && args[i][0] != '\0'; i++) {
        printf("%s ", args[i]);
    }
    printf("\n");
}

// READ MEMORY
void sys_readMemory(char args[MAX_ARGS][50]) {
    char *val = getMemory(args[0]);

    if (val != NULL)
        printf("[SYS_CALL] readMemory %s = %s\n", args[0], val);
    else
        printf("[SYS_CALL] readMemory %s = NULL\n", args[0]);
}

// WRITE MEMORY
void sys_writeMemory(char args[MAX_ARGS][50]) {
    setMemory(args[0], args[1]);
    printf("[SYS_CALL] writeMemory %s = %s\n", args[0], args[1]);
}

// USER INPUT
void sys_userInput(char args[MAX_ARGS][50]) {
    char value[50];

    printf("Enter value for %s: ", args[0]);
    scanf("%s", value);

    setMemory(args[0], value);

    printf("[SYS_CALL] input %s = %s\n", args[0], value);
}

// READ FILE
void sys_readFile(char args[MAX_ARGS][50]) {
    FILE *fp = fopen(args[0], "r");

    if (fp == NULL) {
        printf("[SYS_CALL] readFile failed\n");
        return;
    }

    char line[100];

    printf("[SYS_CALL] readFile %s:\n", args[0]);

    while (fgets(line, sizeof(line), fp)) {
        printf("%s", line);
    }

    fclose(fp);
}

// WRITE FILE
void sys_writeFile(char args[MAX_ARGS][50]) {
    FILE *fp = fopen(args[0], "w");

    if (fp == NULL) {
        printf("[SYS_CALL] writeFile failed\n");
        return;
    }

    fprintf(fp, "%s", args[1]);

    fclose(fp);

    printf("[SYS_CALL] writeFile %s\n", args[0]);
}

// ---------------- MAIN INTERPRETER ----------------
int main() {

    char program[] =
        "semWait userInput\n"
        "assign x 1\n"
        "assign y 5\n"
        "semSignal userInput\n"
        "semWait userOutput\n"
        "printFromTo x y\n"
        "semSignal userOutput\n";

    int bt = calculateBurstTime(program);
    printf("Burst Time = %d\n\n", bt);

    char exec[MAX_LINE];
    strcpy(exec, program);

    char *saveptr;                                   // FIX 1: explicit save pointer for strtok_r
    char *line = strtok_r(exec, "\n", &saveptr);     // FIX 1: use strtok_r in outer loop

    while (line != NULL) {

        char cmd[50];
        char args[MAX_ARGS][50];
        int argCount;

        memset(args, 0, sizeof(args));               // FIX 2: zero out args before each parse

        parseLine(line, cmd, args, &argCount);

        // -------- DISPATCH --------
        if (strcmp(cmd, "printFromTo") == 0)
            sys_printFromTo(args);

        else if (strcmp(cmd, "semWait") == 0)
            sys_semWait(args);

        else if (strcmp(cmd, "semSignal") == 0)
            sys_semSignal(args);

        else if (strcmp(cmd, "assign") == 0)
            sys_writeMemory(args);

        else if (strcmp(cmd, "print") == 0)
            sys_print(args);

        else if (strcmp(cmd, "readMemory") == 0)
            sys_readMemory(args);

        else if (strcmp(cmd, "writeMemory") == 0)
            sys_writeMemory(args);

        else if (strcmp(cmd, "input") == 0)
            sys_userInput(args);

        else if (strcmp(cmd, "readFile") == 0)
            sys_readFile(args);

        else if (strcmp(cmd, "writeFile") == 0)
            sys_writeFile(args);

        line = strtok_r(NULL, "\n", &saveptr);       // FIX 1: use strtok_r in outer loop
    }

    return 0;
}