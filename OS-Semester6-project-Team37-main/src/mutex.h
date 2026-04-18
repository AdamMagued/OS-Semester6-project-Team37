#ifndef MUTEX_H
#define MUTEX_H

#include "memory.h"

#define NUM_MUTEXES 3

typedef struct {
    char name[20];
    int locked;          /* 0 = free, 1 = taken */
    int owner;           /* pid of lock holder, -1 if free */
    int waitQueue[MAX_QUEUE];
    int waitCount;
} Mutex;

/* Globals (defined in mutex.c) */
extern Mutex mutexes[NUM_MUTEXES];
extern int unblockedPID;  /* set by mutexSignal when a process is unblocked */

void initMutexes(void);
int  mutexWait(const char *resource, int pid);   /* returns 1=acquired, 0=blocked */
int  mutexSignal(const char *resource, int pid); /* returns unblocked pid or -1 */

#endif
