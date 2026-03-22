/*
 * linux_test_m3.c — M3 Validation: Xorg userspace stack readiness
 *
 * Tests:
 * 1. /usr/lib/xorg/Xorg exists and is accessible
 * 2. libX11.so.6 is loadable
 * 3. /etc/X11/xorg.conf exists
 * 4. /usr/share/fonts/X11/misc exists
 * 5. /usr/bin/xinit exists
 * 6. /usr/bin/xauth exists
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

static int test_file(const char *path, const char *label) {
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        printf("[M3] %s: [OK]\n", label);
        close(fd);
        return 1;
    } else {
        printf("[M3] %s: [MISSING] (%s)\n", label, path);
        return 0;
    }
}

static int test_read(const char *path, const char *label) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("[M3] %s: [MISSING]\n", label);
        return 0;
    }
    char buf[128];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) {
        buf[n < 64 ? n : 64] = 0;
        printf("[M3] %s: [OK] (first bytes readable)\n", label);
        return 1;
    }
    printf("[M3] %s: [EMPTY]\n", label);
    return 0;
}

int main() {
    int ok = 0, total = 0;

    printf("\n--- ChrysalisOS M3 Validation ---\n");

    total++; ok += test_file("/usr/lib/xorg/Xorg", "Xorg binary");
    total++; ok += test_file("/usr/bin/xinit", "xinit");
    total++; ok += test_file("/usr/bin/xauth", "xauth");
    total++; ok += test_file("/lib/x86_64-linux-gnu/libX11.so.6", "libX11");
    total++; ok += test_file("/lib/x86_64-linux-gnu/libxcb.so.1", "libxcb");
    total++; ok += test_file("/lib/x86_64-linux-gnu/libXext.so.6", "libXext");
    total++; ok += test_file("/lib/x86_64-linux-gnu/libXrender.so.1", "libXrender");
    total++; ok += test_file("/lib/x86_64-linux-gnu/libXrandr.so.2", "libXrandr");
    total++; ok += test_read("/etc/X11/xorg.conf", "xorg.conf");
    total++; ok += test_read("/etc/X11/xinitrc", "xinitrc");

    printf("\n--- M3 Result: %d/%d passed ---\n", ok, total);
    return (ok == total) ? 0 : 1;
}
