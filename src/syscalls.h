/**
 * syscalls.h - System Calls Interface
 *
 * System calls provide the interface between processes and OS services.
 * The interpreter calls these functions when a process requests
 * hardware access (screen, keyboard, disk).
 *
 * Memory read/write system calls are provided by memory.h
 * (getVariable / setVariable).
 */

#ifndef SYSCALLS_H
#define SYSCALLS_H

/**
 * @param pid   PID of the process generating the output
 * @param text  String to display
 */
void sys_print(int pid, const char *text);

/**
 * Log an event to the system log (used for GUI visualization).
 */
void addLog(int pid, const char *event, const char *type);

/**
 * Read text input from the user (userInput resource).
 * Prints "Please enter a value: " prompt, then reads one token.
 * @param buffer      Destination buffer
 * @param bufferSize  Size of the destination buffer
 */
void sys_input(char *buffer, int bufferSize);

/**
 * Write data to a file on disk (file resource).
 * @param filename  Path/name of file to create/overwrite
 * @param data      String to write
 * @return 0 on success, -1 on error
 */
int sys_writeFile(const char *filename, const char *data);

/**
 * Read entire contents of a file from disk (file resource).
 * @param filename    Path/name of file to read
 * @param buffer      Destination buffer for file contents
 * @param bufferSize  Size of the destination buffer
 * @return 0 on success, -1 on error
 */
int sys_readFile(const char *filename, char *buffer, int bufferSize);

/**
 * Push a pre-supplied value into the input queue so that the next
 * sys_input() call dequeues it instead of blocking on stdin.
 * Used by the HTTP server when the frontend supplies an input value.
 */
void sys_pushInput(const char *value);

/**
 * Discard all queued input values.  Called on simulation reset.
 */
void sys_clearInputQueue(void);

/**
 * Returns 1 if the input queue is empty, 0 otherwise.
 */
int sys_inputQueueEmpty(void);

/**
 * Enable or disable interactive terminal input for sys_input().
 * When enabled, sys_input() will block on scanf() if the queue is empty.
 */
void sys_setInteractive(int enabled);

#endif /* SYSCALLS_H */
