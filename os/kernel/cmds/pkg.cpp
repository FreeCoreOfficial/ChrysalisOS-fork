/* kernel/cmds/pkg.cpp */
#include "pkg.h"
#include "../chryspkg/chryspkg.h"
#include "../terminal.h"
#include "../string.h"

extern "C" int cmd_pkg(int argc, char** argv) {
    if (argc < 2) {
        terminal_writestring("Usage: pkg <command>\n");
        terminal_writestring("Commands:\n");
        terminal_writestring("  init     Initialize native catalog\n");
        terminal_writestring("  list     List native catalog entries\n");
        return -1;
    }

    const char* sub = argv[1];

    if (strcmp(sub, "init") == 0) {
        chryspkg_init();
        return 0;
    }

    if (strcmp(sub, "list") == 0) {
        chryspkg_list();
        return 0;
    }

    terminal_writestring("Unknown pkg command.\n");
    return -1;
}
