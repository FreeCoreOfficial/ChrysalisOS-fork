#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    printf("\n=== ChrysalisOS Xorg Direct Test ===\n");

    setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin", 1);
    setenv("DISPLAY", ":0", 1);
    setenv("HOME", "/root", 1);
    setenv("TERM", "xterm", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);

    char *argv[] = {
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

    printf("[xorg-direct] execve /usr/lib/xorg/Xorg\n");
    execve("/usr/lib/xorg/Xorg", argv, envp);
    perror("[xorg-direct] execve failed");
    return 1;
}
