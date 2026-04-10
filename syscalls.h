/**
 * syscalls.h - System Calls Interface
 * Team Member: System Calls Implementer
 * 
 * This file declares all system call functions that processes use
 * to request OS services.
 */

#ifndef SYSCALLS_H
#define SYSCALLS_H

// ============== MEMORY WORD STRUCTURE ==============
// This must match what the Memory teammate defines
typedef struct {
    int processID;           // Which process owns this variable (-1 if empty)
    char varName[32];        // Variable name (e.g., "x", "a", "b")
    char value[256];         // Variable value (string or number as string)
} MemoryWord;

// ============== SYSTEM CALLS ==============

/**
 * Get user input from keyboard
 * Called when program has "assign x input"
 * Note: semWait/semSignal for userInput is handled by interpreter/mutex teammate
 * 
 * @return String entered by user (caller must free with free())
 */
char* sys_input(void);

/**
 * Write data to a file on disk
 * Called when program has "writeFile filename data"
 * Note: semWait/semSignal for file is handled by interpreter/mutex teammate
 * 
 * @param filename Path/name of file to create/write to
 * @param data     String to write to the file
 * @return 0 on success, -1 on error
 */
int sys_writeFile(const char* filename, const char* data);

/**
 * Read entire contents of a file from disk
 * Called when program has "assign var readFile filename"
 * Note: semWait/semSignal for file is handled by interpreter/mutex teammate
 * 
 * @param filename Path/name of file to read
 * @return File contents as string (caller must free with free())
 *         Returns empty string "" if file cannot be opened
 */
char* sys_readFile(const char* filename);

/**
 * Print text to the screen
 * Called when program has "print variable"
 * Note: semWait/semSignal for userOutput is handled by interpreter/mutex teammate
 * 
 * @param text String to print to console
 */
void sys_print(const char* text);

// ============== MEMORY OPERATIONS ==============
// These work with the Memory teammate's global memory array

/**
 * Write a variable value to simulated memory
 * 
 * @param processID ID of the process owning this variable
 * @param varName   Name of the variable (e.g., "x", "a", "b")
 * @param value     Value to store (as string)
 */
void sys_memWrite(int processID, const char* varName, const char* value);

/**
 * Read a variable value from simulated memory
 * 
 * @param processID ID of the process owning this variable
 * @param varName   Name of the variable to read
 * @return Value as string (caller must free with free())
 *         Returns empty string "" if variable not found
 */
char* sys_memRead(int processID, const char* varName);

/**
 * Initialize memory (called once at OS startup)
 * Sets all memory words to empty state
 */
void sys_memInit(void);

/**
 * Print memory contents (for debugging)
 * Called by scheduler to show memory state
 */
void sys_memPrint(void);

// ============== GLOBAL MEMORY ARRAY ==============
// This will be defined in syscalls.c
// Other teammates can access it using "extern MemoryWord memory[40];"
extern MemoryWord memory[40];

#endif /* SYSCALLS_H */