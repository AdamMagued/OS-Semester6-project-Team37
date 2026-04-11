#include "interpreter.h"
#include "mutex.h"

#define MAX_ARGS 10
#define ARG_LENGTH 256

static void parseLine(const char *line, char *cmd, char args[MAX_ARGS][ARG_LENGTH], int *argCount) {
    *argCount = 0;
    cmd[0] = '\0';

    char temp[VALUE_LENGTH];
    strncpy(temp, line, VALUE_LENGTH - 1);
    temp[VALUE_LENGTH - 1] = '\0';

    char *token = strtok(temp, " \t");
    if (token == NULL) return;
    strcpy(cmd, token);

    while ((token = strtok(NULL, " \t")) != NULL && *argCount < MAX_ARGS) {
        strcpy(args[*argCount], token);
        (*argCount)++;
    }
}

int executeInstruction(Process *p) {
    int pc = p->pcb.programCounter;

    if (pc >= p->codeLineCount) return EXEC_FINISHED;

    /* Copy instruction from memory (strtok modifies its input) */
    char instrCopy[VALUE_LENGTH];
    strncpy(instrCopy, memory[p->pcb.lowerBound + pc].value, VALUE_LENGTH - 1);
    instrCopy[VALUE_LENGTH - 1] = '\0';

    printf("  Process %d executing [PC=%d]: %s\n", p->id, pc, instrCopy);

    char cmd[64];
    char args[MAX_ARGS][ARG_LENGTH];
    int argCount;
    memset(args, 0, sizeof(args));
    parseLine(instrCopy, cmd, args, &argCount);

    int result = EXEC_CONTINUE;

    /* ── DISPATCH ── */

    if (strcmp(cmd, "semWait") == 0) {
        int acquired = mutexWait(args[0], p->id);
        if (!acquired) {
            result = EXEC_BLOCKED;
        }
    }
    else if (strcmp(cmd, "semSignal") == 0) {
        mutexSignal(args[0], p->id);
    }
    else if (strcmp(cmd, "assign") == 0) {
        if (argCount >= 2 && strcmp(args[1], "input") == 0) {
            char input[VALUE_LENGTH];
            printf("Please enter a value: ");
            fflush(stdout);
            if (scanf("%s", input) != 1) strcpy(input, "");
            setVariable(p, args[0], input);
            printf("  -> %s = %s\n", args[0], input);
        } else if (argCount >= 2) {
            setVariable(p, args[0], args[1]);
            printf("  -> %s = %s\n", args[0], args[1]);
        }
    }
    else if (strcmp(cmd, "print") == 0) {
        char *val = getVariable(p, args[0]);
        if (val != NULL)
            printf("  [OUTPUT] %s\n", val);
        else
            printf("  [OUTPUT] %s\n", args[0]);
    }
    else if (strcmp(cmd, "printFromTo") == 0) {
        char *v1 = getVariable(p, args[0]);
        char *v2 = getVariable(p, args[1]);
        int x = v1 ? atoi(v1) : 0;
        int y = v2 ? atoi(v2) : 0;
        printf("  [OUTPUT] Numbers from %d to %d:\n", x, y);
        if (x <= y) {
            for (int i = x; i <= y; i++) printf("  %d\n", i);
        } else {
            for (int i = x; i >= y; i--) printf("  %d\n", i);
        }
    }
    else if (strcmp(cmd, "writeFile") == 0) {
        char *filename = getVariable(p, args[0]);
        char *data = getVariable(p, args[1]);
        if (filename && data) {
            FILE *fp = fopen(filename, "w");
            if (fp) {
                fprintf(fp, "%s", data);
                fclose(fp);
                printf("  [FILE] Wrote to '%s': %s\n", filename, data);
            } else {
                printf("  [FILE ERROR] Cannot write to '%s'\n", filename);
            }
        } else {
            printf("  [FILE ERROR] Variable not found for writeFile\n");
        }
    }
    else if (strcmp(cmd, "readFile") == 0) {
        char *filename = getVariable(p, args[0]);
        if (filename) {
            FILE *fp = fopen(filename, "r");
            if (fp) {
                char content[VALUE_LENGTH];
                size_t bytes = fread(content, 1, VALUE_LENGTH - 1, fp);
                content[bytes] = '\0';
                fclose(fp);
                printf("  [FILE] Contents of '%s':\n  %s\n", filename, content);
            } else {
                printf("  [FILE ERROR] Cannot read '%s'\n", filename);
            }
        } else {
            printf("  [FILE ERROR] Variable not found for readFile\n");
        }
    }
    else {
        printf("  [WARN] Unknown instruction: %s\n", cmd);
    }

    /* Increment PC */
    p->pcb.programCounter++;

    /* If blocked, return blocked (takes priority even if last instruction) */
    if (result == EXEC_BLOCKED)
        return EXEC_BLOCKED;

    /* Check if process finished */
    if (p->pcb.programCounter >= p->codeLineCount)
        return EXEC_FINISHED;

    return EXEC_CONTINUE;
}
