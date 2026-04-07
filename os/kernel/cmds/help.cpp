#include "../terminal.h"
#include "../string.h"
#include "registry.h"

#define COMMANDS_PER_PAGE 8

typedef struct {
    const char* name;
    const char* usage;
    const char* desc;
} help_entry_t;

static const help_entry_t help_entries[] = {
    { "buildinfo", "buildinfo", "Show build metadata" },
    { "beep", "beep", "Play a short PC speaker beep" },
    { "cs", "cs <script.csr>", "Run a Chrysalis script file" },
    { "chrysver", "chrysver", "Show OS version" },
    { "cd", "cd <dir>", "Change current directory" },
    { "crash", "crash", "Trigger a kernel crash (testing)" },
    { "cat", "cat <file>", "Print file contents" },
    { "color", "color <fg> <bg>", "Change terminal colors" },
    { "clear", "clear", "Clear the terminal" },
    { "curl", "curl <url>", "HTTP client (basic)" },
    { "credits", "credits", "Show project credits" },
    { "date", "date", "Show current date/time" },
    { "disk", "disk <cmd>", "Disk utilities" },
    { "echo", "echo <text>", "Print text" },
    { "elf", "elf <info|load|run> <file>", "ELF loader helper" },
    { "elf-debug", "elf-debug <file>", "Dump ELF metadata" },
    { "elf-crash", "elf-crash", "ELF crash test" },
    { "exec", "exec <file>", "Run ELF directly" },
    { "exit", "exit", "Exit shell / shutdown" },
    { "fat", "fat <cmd>", "FAT32 utilities" },
    { "fortune", "fortune", "Print a random quote" },
    { "help", "help [cmd]", "Show help and usage" },
    { "get", "get <url>", "Download file" },
    { "ls", "ls [dir]", "List directory" },
    { "launch", "launch <app>", "Launch GUI app" },
    { "launch-exit", "launch-exit <app>", "Launch app then exit" },
    { "mkdir", "mkdir <dir>", "Create directory" },
    { "login", "login", "Login prompt" },
    { "mem", "mem", "Memory stats" },
    { "net", "net <cmd>", "Network utilities" },
    { "pmm", "pmm", "Physical memory manager info" },
    { "pkg", "pkg <cmd>", "Package manager" },
    { "play", "play <file>", "Play audio (basic)" },
    { "rm", "rm <file>", "Delete file" },
    { "reboot", "reboot", "Reboot system" },
    { "shutdown", "shutdown", "Shutdown system" },
    { "sysfetch", "sysfetch", "Show system summary" },
    { "ticks", "ticks", "Show PIT ticks" },
    { "touch", "touch <file>", "Create empty file" },
    { "uptime", "uptime", "Show uptime" },
    { "vfs", "vfs", "VFS info" },
    { "write", "write <file>", "Interactive line editor" },
    { "win", "win <app>", "Launch GUI app" },
    { "vt", "vt <cmd>", "Virtual terminal control" },

    { "pwd", "pwd", "Print current directory" },
    { "cp", "cp <src> <dst>", "Copy file" },
    { "mv", "mv <src> <dst>", "Move/rename file" },
    { "head", "head [-n N] <file>", "Show first lines" },
    { "tail", "tail [-n N] <file>", "Show last lines" },
    { "wc", "wc [file]", "Count lines/words/bytes" },
    { "hexdump", "hexdump [file]", "Hex dump of data" },
    { "grep", "grep [-n] <pattern> [file]", "Filter lines by pattern" },
    { "tee", "tee <file>", "Write stdin to file and stdout" },
    { "sha256", "sha256 [file]", "Compute SHA-256 hash" },
    { "sleep", "sleep <ms|Ns|Nms>", "Sleep for a duration" },
    { "which", "which <command>", "Locate a command" },
    { "size", "size <file>", "Show file size" },
};

static const help_entry_t* help_find(const char* name) {
    for (unsigned i = 0; i < sizeof(help_entries) / sizeof(help_entries[0]); ++i) {
        if (strcmp(help_entries[i].name, name) == 0) return &help_entries[i];
    }
    return 0;
}

static int has_flag(const char* args, const char* flag) {
    if (!args || !*args) return 0;
    return strstr(args, flag) != 0;
}

static int parse_page_arg(const char* args) {
    if (!args || !*args) return 1;

    const char* p = strstr(args, "--page");
    if (!p) return 1;

    p += 6;
    while (*p == ' ') p++;

    int val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }

    if (val <= 0) return 1;
    return val;
}

static void print_entry_short(const help_entry_t* e) {
    terminal_writestring(" - ");
    terminal_writestring(e->name);
    terminal_writestring(" : ");
    terminal_writestring(e->desc);
    terminal_writestring("\n");
}

static void print_entry_long(const help_entry_t* e) {
    terminal_writestring(e->name);
    terminal_writestring("\n  usage: ");
    terminal_writestring(e->usage);
    terminal_writestring("\n  ");
    terminal_writestring(e->desc);
    terminal_writestring("\n\n");
}

static void list_page(int page, int per_page) {
    int total = (int)(sizeof(help_entries) / sizeof(help_entries[0]));
    int max_pages = (total + per_page - 1) / per_page;

    if (page < 1) page = 1;
    if (page > max_pages) page = max_pages;

    int start = (page - 1) * per_page;
    int end = start + per_page;
    if (end > total) end = total;

    terminal_writestring("Commands (page ");
    terminal_printf("%d/%d", page, max_pages);
    terminal_writestring("):\n");

    for (int i = start; i < end; ++i) {
        print_entry_short(&help_entries[i]);
    }

    terminal_writestring("Use: help <cmd> or help --all\n");
}

static void list_all_long(void) {
    for (unsigned i = 0; i < sizeof(help_entries) / sizeof(help_entries[0]); ++i) {
        print_entry_long(&help_entries[i]);
    }
}

__attribute__((unused)) static void list_all_short(void) {
    for (unsigned i = 0; i < sizeof(help_entries) / sizeof(help_entries[0]); ++i) {
        print_entry_short(&help_entries[i]);
    }
}

static int first_token(const char* args, char* out, int out_len) {
    if (!args || !*args) return 0;

    while (*args == ' ') args++;
    if (*args == 0) return 0;

    int i = 0;
    while (*args && *args != ' ' && i < out_len - 1) {
        out[i++] = *args++;
    }
    out[i] = 0;
    return i > 0;
}

extern "C" void cmd_help(const char* args) {
    int total = (int)(sizeof(help_entries) / sizeof(help_entries[0]));
    int per_page = COMMANDS_PER_PAGE;
    int max_pages = (total + per_page - 1) / per_page;

    if (args && args[0]) {
        if (has_flag(args, "--max")) {
            terminal_writestring("max pages: ");
            terminal_printf("%d\n", max_pages);
            return;
        }

        if (has_flag(args, "--all")) {
            list_all_long();
            return;
        }

        char token[64];
        if (first_token(args, token, sizeof(token))) {
            if (token[0] != '-') {
                const help_entry_t* e = help_find(token);
                if (e) {
                    print_entry_long(e);
                } else {
                    terminal_printf("help: no info for '%s'\n", token);
                }
                return;
            }
        }
    }

    int page = parse_page_arg(args);
    list_page(page, per_page);
}
