#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
    const size_t page = 4096;
    unsigned char *base = mmap(NULL, page * 3, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        perror("[mmap-test] mmap");
        return 1;
    }

    memset(base + 0 * page, 0x11, page);
    memset(base + 1 * page, 0x22, page);
    memset(base + 2 * page, 0x33, page);

    if (mprotect(base + page, page, PROT_READ) != 0) {
        perror("[mmap-test] mprotect ro");
        return 1;
    }
    if (mprotect(base + page, page, PROT_READ | PROT_WRITE) != 0) {
        perror("[mmap-test] mprotect rw");
        return 1;
    }

    unsigned char *grown = mremap(base, page * 3, page * 5, MREMAP_MAYMOVE);
    if (grown == MAP_FAILED) {
        perror("[mmap-test] mremap");
        return 1;
    }

    if (grown[0] != 0x11 || grown[page] != 0x22 || grown[page * 2] != 0x33) {
        fprintf(stderr, "[mmap-test] preserved contents mismatch\n");
        return 1;
    }

    memset(grown + page * 3, 0x44, page);
    memset(grown + page * 4, 0x55, page);

    if (munmap(grown + page, page) != 0) {
        perror("[mmap-test] munmap middle");
        return 1;
    }

    if (grown[0] != 0x11 || grown[page * 2] != 0x33 ||
        grown[page * 3] != 0x44 || grown[page * 4] != 0x55) {
        fprintf(stderr, "[mmap-test] tail corruption after middle unmap\n");
        return 1;
    }

    if (munmap(grown, page) != 0) {
        perror("[mmap-test] munmap head");
        return 1;
    }
    if (munmap(grown + page * 2, page * 3) != 0) {
        perror("[mmap-test] munmap tail");
        return 1;
    }

    printf("[mmap-test] anonymous mmap/mprotect/mremap/munmap OK\n");
    return 0;
}
