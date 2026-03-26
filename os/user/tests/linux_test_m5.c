/*
 * linux_test_m5.c - A minimal init process for ChrysalisOS
 *
 * This program acts as PID 1 for the 64-bit Linux ABI environment.
 * It sets up the environment and launches Xorg via xinit.
 *
 * Uses fork() + execve() in the child, and wait() in the parent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

/* Verify a file exists on the ramfs / sysroot */
static int check_file(const char *path, const char *label) {
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        printf("[M5] %s: [OK]\n", label);
        close(fd);
        return 1;
    }
    printf("[M5] %s: [MISSING] (%s)\n", label, path);
    return 0;
}

int main(void) {
    printf("\n=== ChrysalisOS M5: Launching GUI ===\n\n");

    /* ---- pre-flight checks ---- */
    int ok = 0, total = 0;

    total++; ok += check_file("/usr/lib/xorg/Xorg",   "Xorg server");
    total++; ok += check_file("/usr/bin/xinit",        "xinit");
    total++; ok += check_file("/usr/bin/dwm",          "dwm");
    total++; ok += check_file("/etc/X11/xinitrc",      "xinitrc");
    total++; ok += check_file("/lib/x86_64-linux-gnu/libX11.so.6", "libX11");

    printf("\n[M5] Pre-flight: %d/%d passed\n\n", ok, total);

    if (ok < total) {
        printf("[M5] ABORT: pre-flight checks failed, cannot launch Xorg.\n");
        return 1;
    }

    /* ---- set minimal environment ---- */
    setenv("PATH",    "/usr/bin:/bin:/usr/sbin:/sbin", 1);
    setenv("DISPLAY", ":0", 1);
    setenv("HOME",    "/root", 1);
    setenv("TERM",    "xterm", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);

    /* ---- fork + exec ---- */
    pid_t pid = fork();

    if (pid < 0) {
        perror("[M5] fork failed");
        return 1;
    }

    if (pid == 0) {
        /* Child process: launch xinit */
        printf("[M5-child] Executing /usr/bin/xinit with Xorg args...\n");

        char *argv[] = {
            "xinit",
            "/usr/bin/dwm",
            "--",
            "/usr/lib/xorg/Xorg",
            ":0",
            "-retro",
            "-noreset",
            "-config", "/etc/X11/xorg.conf",
            "-logfile", "/var/log/Xorg.0.log",
            NULL
        };
        char *envp[] = {
            "PATH=/usr/bin:/bin:/usr/sbin:/sbin",
            "DISPLAY=:0",
            "HOME=/root",
            "TERM=xterm",
            "XDG_RUNTIME_DIR=/tmp",
            NULL
        };

        execve("/usr/bin/xinit", argv, envp);

        /* If execve returns, it failed */
        perror("[M5-child] execve /usr/bin/xinit failed");
        _exit(1);
    }

    /* Parent process: wait for child */
    printf("[M5-parent] Child PID = %d, waiting...\n", pid);

    int status;
    pid_t waited = wait(&status);

    if (waited > 0) {
        printf("[M5-parent] Child %d exited with status %d\n", waited, status);
    } else {
        perror("[M5-parent] wait failed");
    }

    printf("[M5-parent] All children exited. System halted.\n");
    return 0;
}
