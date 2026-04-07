// kernel/cmds/elf.cpp
// Command: elf <info|load|run> <path>
extern "C" {
#include "../elf/elf.h"   /* elf_load_from_buffer, elf_unload_kernel_space, types */
}
#include "elf.h"
#include "elf_debug.h"
#include <stdint.h>
#include <stddef.h>
#include "../terminal.h"
extern "C" int fs_readfile(const char* path, void** out_buf, uint32_t* out_len);
extern "C" int exec_from_path(const char* path, char* const argv[]); /* from kernel/proc/exec.c */
extern void terminal_printf(const char* fmt, ...);

/* Minimal string equality helper (avoids bringing libc++/string headers) */
static int str_eq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void usage() {
    terminal_printf("ELF helper:\n");
    terminal_printf("  elf info <file>              show ELF headers/segments\n");
    terminal_printf("  elf load <file>              load into kernel space (no exec)\n");
    terminal_printf("  elf run  <file> [args...]    execute ELF with optional args\n");
    terminal_printf("  elf help                      show this help\n");
    terminal_printf("Tip: use 'elf-debug <file>' for detailed segment info.\n");
}

/* simple helper to read file and parse ELF into info */
static int parse_elf_from_file(const char* path, elf_load_info_t* out) {
    void* buf = NULL;
    uint32_t len = 0;
    if (fs_readfile(path, &buf, &len) != 0 || !buf || len == 0) {
        terminal_printf("[elf] cannot read file: %s\n", path ? path : "(null)");
        return -1;
    }
    int r = elf_load_from_buffer(buf, len, out);
    /* Note: fs_readfile ownership semantics may vary. exec() frees filebuf after load.
       elf_load_from_buffer copies segment data into kernel buffers so freeing filebuf
       here is optional depending on your fs_readfile implementation. */
    return r;
}

int cmd_elf(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return -1;
    }

    const char* sub = argv[1];
    if (sub == NULL) {
        usage();
        return -1;
    }

    if (str_eq(sub, "help")) {
        usage();
        return 0;
    }

    if (str_eq(sub, "info")) {
        if (argc < 3) { terminal_printf("[elf] info requires <file>\n"); return -1; }
        const char* path = argv[2];
        elf_load_info_t info;
        int r = parse_elf_from_file(path, &info);
        if (r != 0) {
            terminal_printf("[elf] parse failed: %d\n", r);
            return -1;
        }
        elf_debug_print_info(&info);
        elf_unload_kernel_space(&info);
        return 0;
    }

    if (str_eq(sub, "load")) {
        if (argc < 3) { terminal_printf("[elf] load requires <file>\n"); return -1; }
        const char* path = argv[2];
        elf_load_info_t info;
        int r = parse_elf_from_file(path, &info);
        if (r != 0) {
            terminal_printf("[elf] load failed: %d\n", r);
            return -1;
        }
        terminal_printf("[elf] loaded %d segments, entry=0x%x\n", info.seg_count, info.entry_point);
        elf_unload_kernel_space(&info);
        return 0;
    }

    if (str_eq(sub, "run")) {
        if (argc < 3) { terminal_printf("[elf] run requires <file>\n"); return -1; }
        const char* path = argv[2];
        char* exec_argv[16];
        int exec_argc = 0;
        for (int i = 2; i < argc && exec_argc < 15; ++i) {
            exec_argv[exec_argc++] = argv[i];
        }
        exec_argv[exec_argc] = 0;

        int pid = exec_from_path(path, exec_argv);
        if (pid < 0) {
            terminal_printf("[elf] exec failed (pid=%d)\n", pid);
            return -1;
        }
        terminal_printf("[elf] exec pid=%d\n", pid);
        return 0;
    }

    usage();
    return -1;
}
