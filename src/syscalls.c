/**
 * syscalls.c - System Calls Implementation
 *
 * Implements the OS system call layer for I/O operations.
 * These are called by the interpreter when a process needs
 * to access hardware resources (screen, keyboard, disk).
 */

#include "syscalls.h"
#include <stdio.h>
#include <string.h>

void sys_print(int pid, const char *text) {
    printf("P%d: %s\n", pid, text);
    fflush(stdout);

    /* Also add to the system log so it appears in the GUI */
    char logMsg[256];
    snprintf(logMsg, sizeof(logMsg), "P%d: %s", pid, text);
    addLog(pid, logMsg, "output");
}

/* ── Input queue for HTTP-server mode ───────────────────────────
   sys_pushInput() pre-loads values; sys_input() dequeues them.
   Falls back to stdin when the queue is empty.
   ────────────────────────────────────────────────────────────── */
#define SYS_INPUT_QUEUE_CAP 64
static char s_inputQ[SYS_INPUT_QUEUE_CAP][256];
static int  s_inputHead  = 0;
static int  s_inputCount = 0;
static int  s_interactive = 0;

void sys_setInteractive(int enabled) {
    s_interactive = enabled;
}

void sys_pushInput(const char *value) {
    if (s_inputCount >= SYS_INPUT_QUEUE_CAP) return;   /* queue full, drop */
    int tail = (s_inputHead + s_inputCount) % SYS_INPUT_QUEUE_CAP;
    strncpy(s_inputQ[tail], value, 255);
    s_inputQ[tail][255] = '\0';
    s_inputCount++;
}

void sys_clearInputQueue(void) {
    s_inputHead  = 0;
    s_inputCount = 0;
    memset(s_inputQ, 0, sizeof(s_inputQ));
}

int sys_inputQueueEmpty(void) {
    return s_inputCount == 0;
}

void sys_input(char *buffer, int bufferSize) {
    if (s_inputCount > 0) {
        /* Dequeue the next pre-supplied value */
        int headIdx = s_inputHead;
        strncpy(buffer, s_inputQ[headIdx], bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        s_inputHead = (s_inputHead + 1) % SYS_INPUT_QUEUE_CAP;
        s_inputCount--;
        printf("  [SYSCALL] input: auto-supplied '%s'\n", buffer);
        return;
    }
    if (s_interactive) {
        printf("Please enter a value: ");
        fflush(stdout);
        if (scanf("%255s", buffer) != 1) {
            buffer[0] = '\0';
        }
        return;
    }
    /* In server mode, we MUST NOT block on scanf. 
       If no input is available, we return an empty string. 
       The scheduler logic (checkNeedsInput) should prevent us from hitting this 
       unprepared, but this safety check prevents the server from hanging. */
    buffer[0] = '\0';
}

int sys_writeFile(const char *filename, const char *data) {
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        printf("  [SYSCALL ERROR] Cannot open file '%s' for writing\n", filename);
        return -1;
    }
    fprintf(fp, "%s", data);
    fclose(fp);
    printf("  [SYSCALL] writeFile('%s', '%s') -> OK\n", filename, data);
    return 0;
}

int sys_readFile(const char *filename, char *buffer, int bufferSize) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        printf("  [SYSCALL ERROR] Cannot open file '%s' for reading\n", filename);
        buffer[0] = '\0';
        return -1;
    }
    size_t bytes = fread(buffer, 1, bufferSize - 1, fp);
    buffer[bytes] = '\0';
    fclose(fp);
    printf("  [SYSCALL] readFile('%s') -> %zu bytes\n", filename, bytes);
    return 0;
}
