// kernel/cmds/credits.cpp
#include "../terminal.h"

extern "C" void cmd_credits(const char* args)
{
    /* short version */
    if (args && args[0] == '-' && args[1] == '-') {
        terminal_writestring("Chrysalis OS by Mihai (MIT License)\n");
        return;
    }

    terminal_writestring("========================================\n");
    terminal_writestring("              CHRYSALIS OS\n");
    terminal_writestring("========================================\n\n");

    terminal_writestring(" Creator:\n");
    terminal_writestring("  - Mihai\n\n");

    terminal_writestring(" License:\n");
    terminal_writestring("  - MIT License\n");
    terminal_writestring("  - Modifications are allowed\n");
    terminal_writestring("  - Original author must be credited\n\n");

    terminal_writestring(" Core:\n");
    terminal_writestring("  - Custom x86 Kernel\n");
    terminal_writestring("  - Preemptive Scheduler\n");
    terminal_writestring("  - Paging & Virtual Memory\n\n");

    terminal_writestring(" Drivers:\n");
    terminal_writestring("  - Keyboard (IRQ1)\n");
    terminal_writestring("  - PIT Timer (IRQ0)\n");
    terminal_writestring("  - PC Speaker\n\n");

    terminal_writestring(" Build:\n");
    terminal_writestring("  - Freestanding C/C++\n");
    terminal_writestring("  - GRUB Multiboot\n\n");

    terminal_writestring(" Notes for Modders:\n");
    terminal_writestring("  - You may modify and redistribute\n");
    terminal_writestring("  - You must mention the original owner\n");
    terminal_writestring("  - Keep this license notice intact\n\n");

    terminal_writestring("========================================\n");
}
