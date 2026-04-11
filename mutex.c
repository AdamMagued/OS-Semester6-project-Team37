#include "mutex.h"

Mutex mutexes[NUM_MUTEXES];
int unblockedPID = -1;

void initMutexes(void) {
    strcpy(mutexes[0].name, "userInput");
    strcpy(mutexes[1].name, "userOutput");
    strcpy(mutexes[2].name, "file");
    for (int i = 0; i < NUM_MUTEXES; i++) {
        mutexes[i].locked = 0;
        mutexes[i].owner = -1;
        mutexes[i].waitCount = 0;
    }
    printf("[OS] Mutexes initialized: userInput, userOutput, file\n");
}

static Mutex* getMutex(const char *name) {
    for (int i = 0; i < NUM_MUTEXES; i++) {
        if (strcmp(mutexes[i].name, name) == 0)
            return &mutexes[i];
    }
    printf("[OS ERROR] Unknown mutex: %s\n", name);
    return NULL;
}

int mutexWait(const char *resource, int pid) {
    Mutex *m = getMutex(resource);
    if (m == NULL) return 1; /* unknown resource, don't block */

    if (m->locked == 0) {
        m->locked = 1;
        m->owner = pid;
        printf("  [MUTEX] Process %d acquired '%s'\n", pid, resource);
        return 1; /* acquired */
    } else {
        m->waitQueue[m->waitCount++] = pid;
        printf("  [MUTEX] Process %d BLOCKED on '%s' (held by P%d)\n",
               pid, resource, m->owner);
        return 0; /* blocked */
    }
}

int mutexSignal(const char *resource, int pid) {
    Mutex *m = getMutex(resource);
    if (m == NULL) return -1;

    if (m->owner != pid) {
        printf("  [MUTEX ERROR] Process %d tried to release '%s' but doesn't own it\n",
               pid, resource);
        return -1;
    }

    if (m->waitCount > 0) {
        /* Pass lock to next waiter */
        int next = m->waitQueue[0];
        for (int i = 0; i < m->waitCount - 1; i++)
            m->waitQueue[i] = m->waitQueue[i + 1];
        m->waitCount--;
        m->owner = next;
        printf("  [MUTEX] Process %d released '%s' -> Process %d acquired it\n",
               pid, resource, next);
        unblockedPID = next;
        return next;
    } else {
        m->locked = 0;
        m->owner = -1;
        printf("  [MUTEX] Process %d released '%s' (no waiters)\n", pid, resource);
        return -1;
    }
}
