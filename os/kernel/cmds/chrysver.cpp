#include "../terminal.h"

#define CHRYSALIS_VERSION "0.0.4-prealpha"

extern "C" void cmd_chrysver(const char* args)
{
    (void)args;

    terminal_writestring("========================================\n");
    terminal_writestring(" Chrysalis OS\n");
    terminal_writestring(" Version ");
    terminal_writestring(CHRYSALIS_VERSION);
    terminal_writestring("\n\n");

    terminal_writestring(" (C) 2026 mihai209\n");
    terminal_writestring(" Licensed under the MIT License\n");
    terminal_writestring("========================================\n");
}
