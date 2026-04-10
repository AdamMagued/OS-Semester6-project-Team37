/**
 * syscalls.c - System Calls Implementation
 * Team Member: System Calls Implementer
 * 
 * This file implements all system call functions.
 */

#include "syscalls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============== GLOBAL MEMORY ARRAY ==============
// 40 memory words as specified in project description
MemoryWord memory[40];

// ============== HELPER FUNCTIONS ==============

/**
 * Remove trailing newline from a string
 */
static void trim_newline(char* str) {
    size_t len = strlen(str);
    if (len > 0 && str[len-1] == '\n') {
        str[len-1] = '\0';
    }
}

// ============== SYSTEM CALL IMPLEMENTATIONS ==============

char* sys_input(void) {
    char buffer[256];
    
    printf("Please enter a value: ");
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return strdup("");  // EOF or error
    }
    
    trim_newline(buffer);
    return strdup(buffer);
}

int sys_writeFile(const char* filename, const char* data) {
    FILE* fp = fopen(filename, "w");
    if (fp == NULL) {
        fprintf(stderr, "[OS ERROR] Cannot open file '%s' for writing\n", filename);
        return -1;
    }
    
    fprintf(fp, "%s", data);
    fclose(fp);
    
    printf("[OS] File '%s' written successfully\n", filename);
    return 0;
}

char* sys_readFile(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "[OS ERROR] Cannot open file '%s' for reading\n", filename);
        return strdup("");  // Return empty string, not NULL
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    
    // Handle empty file
    if (size == 0) {
        fclose(fp);
        return strdup("");
    }
    
    // Allocate memory for content
    char* content = (char*)malloc(size + 1);
    if (content == NULL) {
        fprintf(stderr, "[OS ERROR] Memory allocation failed for file read\n");
        fclose(fp);
        return strdup("");
    }
    
    // Read entire file
    size_t bytes_read = fread(content, 1, size, fp);
    content[bytes_read] = '\0';
    
    fclose(fp);
    
    printf("[OS] File '%s' read successfully (%ld bytes)\n", filename, size);
    return content;
}

void sys_print(const char* text) {
    printf("%s", text);
    fflush(stdout);  // Ensure output appears immediately
}

// ============== MEMORY OPERATIONS ==============

void sys_memInit(void) {
    for (int i = 0; i < 40; i++) {
        memory[i].processID = -1;
        memset(memory[i].varName, 0, 32);
        memset(memory[i].value, 0, 256);
    }
    printf("[OS] Memory initialized: 40 words available\n");
}

void sys_memWrite(int processID, const char* varName, const char* value) {
    if (processID < 0) {
        fprintf(stderr, "[OS ERROR] Invalid process ID %d for memory write\n", processID);
        return;
    }
    
    // First, try to find existing variable for this process
    for (int i = 0; i < 40; i++) {
        if (memory[i].processID == processID && 
            strcmp(memory[i].varName, varName) == 0) {
            // Update existing variable
            strncpy(memory[i].value, value, 255);
            memory[i].value[255] = '\0';
            printf("[OS MEM] Process %d updated variable '%s' = '%s'\n", 
                   processID, varName, value);
            return;
        }
    }
    
    // Find empty slot
    for (int i = 0; i < 40; i++) {
        if (memory[i].processID == -1) {
            memory[i].processID = processID;
            strncpy(memory[i].varName, varName, 31);
            memory[i].varName[31] = '\0';
            strncpy(memory[i].value, value, 255);
            memory[i].value[255] = '\0';
            printf("[OS MEM] Process %d stored variable '%s' = '%s' at slot %d\n", 
                   processID, varName, value, i);
            return;
        }
    }
    
    fprintf(stderr, "[OS ERROR] No free memory slots for variable '%s' (process %d)\n", 
            varName, processID);
}

char* sys_memRead(int processID, const char* varName) {
    if (processID < 0) {
        fprintf(stderr, "[OS ERROR] Invalid process ID %d for memory read\n", processID);
        return strdup("");
    }
    
    for (int i = 0; i < 40; i++) {
        if (memory[i].processID == processID && 
            strcmp(memory[i].varName, varName) == 0) {
            printf("[OS MEM] Process %d read variable '%s' = '%s'\n", 
                   processID, varName, memory[i].value);
            return strdup(memory[i].value);
        }
    }
    
    fprintf(stderr, "[OS WARN] Variable '%s' not found for process %d\n", varName, processID);
    return strdup("");  // Return empty string if not found
}

void sys_memPrint(void) {
    printf("\n========== MEMORY STATE ==========\n");
    printf("Slot | ProcID | Variable | Value\n");
    printf("-----|--------|----------|------\n");
    
    for (int i = 0; i < 40; i++) {
        if (memory[i].processID != -1) {
            printf("%4d | %6d | %-8s | %s\n", 
                   i, memory[i].processID, memory[i].varName, memory[i].value);
        } else {
            printf("%4d | %6s | %-8s | %s\n", i, "-", "-", "-");
        }
    }
    printf("=================================\n\n");
}