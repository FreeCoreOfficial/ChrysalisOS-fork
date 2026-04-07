#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Execute a program.
 * Currently supports ELF binaries from RAMFS or ChrysFS.
 * In this single-tasking environment, this will load and jump to the program.
 */
int execve(const char *filename, char *const argv[], char *const envp[]);

#ifdef __cplusplus
}
#endif
