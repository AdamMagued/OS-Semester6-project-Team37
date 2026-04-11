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
 * Print text to the screen (userOutput resource).
 * @param text  String to display
 */
void sys_print(const char *text);

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

#endif /* SYSCALLS_H */
