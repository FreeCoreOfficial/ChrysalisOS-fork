#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Execute a program.
 * Currently supports ELF binaries from RAMFS or ChrysFS.
 * In this single-tasking environment, this will load and jump to the program.
 */
int execve(const char *filename, char *const argv[], char *const envp[]);
int execve_linux_i386(const char *filename, char *const argv[]);
int execve_linux_x86_64(const char *filename, char *const argv[]);
int execve_linux_auto(const char *filename, char *const argv[]);
int exec_from_path_linux_i386(const char *path, char *const argv[]);
int exec_from_path_linux_x86_64(const char *path, char *const argv[]);
int exec_from_path_linux_auto(const char *path, char *const argv[]);

#ifdef __cplusplus
}
#endif
