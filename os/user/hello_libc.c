#include "libc/include/unistd.h"

int main(int argc, char *argv[]) {
    const char *msg = "Hello from Libc-powered Usermode!\n";
    write(1, msg, 36);

    for (int i = 0; i < argc; i++) {
        write(1, "arg: ", 5);
        int len = 0;
        while (argv[i][len]) len++;
        write(1, argv[i], len);
        write(1, "\n", 1);
    }

    char buf[128];
    int n = readlink("/proc/self/exe", buf, sizeof(buf));
    if (n > 0) {
        write(1, "Self: ", 6);
        write(1, buf, n);
        write(1, "\n", 1);
    }

    _exit(0);
    return 0;
}
