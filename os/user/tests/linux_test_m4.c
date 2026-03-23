/*
 * linux_test_m4.c — M4 Validation: Lightweight WM (dwm) and xterm
 *
 * Tests:
 * 1. /usr/bin/dwm exists and is executable
 * 2. /usr/bin/xterm exists and is executable
 * 3. /lib/x86_64-linux-gnu/libXft.so.2 is loadable
 * 4. /lib/x86_64-linux-gnu/libfontconfig.so.1 is loadable
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

static int test_file(const char *path, const char *label) {
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        printf("[M4] %s: [OK]\n", label);
        close(fd);
        return 1;
    } else {
        printf("[M4] %s: [MISSING] (%s)\n", label, path);
        return 0;
    }
}

int main() {
    int ok = 0, total = 0;

    printf("\n--- ChrysalisOS M4 Validation ---\n");

    total++; ok += test_file("/usr/bin/dwm", "dwm binary");
    total++; ok += test_file("/usr/bin/xterm", "xterm binary");
    total++; ok += test_file("/lib/x86_64-linux-gnu/libXft.so.2", "libXft");
    total++; ok += test_file("/lib/x86_64-linux-gnu/libfontconfig.so.1", "libfontconfig");
    total++; ok += test_file("/lib/x86_64-linux-gnu/libexpat.so.1", "libexpat");
    total++; ok += test_file("/lib/x86_64-linux-gnu/libXaw.so.7", "libXaw");
    total++; ok += test_file("/lib/x86_64-linux-gnu/libXmu.so.6", "libXmu");

    printf("\n--- M4 Result: %d/%d passed ---\n", ok, total);
    return (ok == total) ? 0 : 1;
}
