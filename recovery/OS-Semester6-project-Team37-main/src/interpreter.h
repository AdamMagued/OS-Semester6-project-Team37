#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "memory.h"

#define EXEC_CONTINUE  1
#define EXEC_FINISHED  0
#define EXEC_BLOCKED  -1

/* Execute one instruction for the given process.
 * Returns EXEC_CONTINUE, EXEC_FINISHED, or EXEC_BLOCKED. */
int executeInstruction(Process *p);

#endif
