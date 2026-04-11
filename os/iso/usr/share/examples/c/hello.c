#include <unistd.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    const char *msg = "Hello from ChrysalisOS C SDK!\n";
    write(1, msg, 34);
    _exit(0);
    return 0;
}
