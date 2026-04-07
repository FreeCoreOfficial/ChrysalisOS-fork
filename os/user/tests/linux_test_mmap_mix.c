#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define FBIOGET_FSCREENINFO 0x4602

struct fb_fix_screeninfo_local {
    char id[16];
    unsigned long smem_start;
    unsigned int smem_len;
    unsigned int type;
    unsigned int type_aux;
    unsigned int visual;
    unsigned short xpanstep;
    unsigned short ypanstep;
    unsigned short ywrapstep;
    unsigned int line_length;
    unsigned long mmio_start;
    unsigned int mmio_len;
    unsigned int accel;
    unsigned short capabilities;
    unsigned short reserved[2];
};

int main(void) {
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        perror("[mmap-mix] open /dev/fb0");
        return 1;
    }

    struct fb_fix_screeninfo_local fix;
    memset(&fix, 0, sizeof(fix));
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0) {
        perror("[mmap-mix] FBIOGET_FSCREENINFO");
        close(fd);
        return 1;
    }

    size_t page = 4096;
    size_t fb_len = (fix.smem_len + page - 1) & ~(page - 1);
    unsigned char *fb = mmap(NULL, fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) {
        perror("[mmap-mix] mmap fb0");
        close(fd);
        return 1;
    }

    unsigned char *anon = mmap(NULL, page * 3, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (anon == MAP_FAILED) {
        perror("[mmap-mix] mmap anon");
        munmap(fb, fb_len);
        close(fd);
        return 1;
    }

    memset(anon, 0x5A, page * 3);
    if (mprotect(anon + page, page, PROT_READ) != 0) {
        perror("[mmap-mix] mprotect ro");
        munmap(anon, page * 3);
        munmap(fb, fb_len);
        close(fd);
        return 1;
    }

    if (munmap(anon, page * 3) != 0) {
        perror("[mmap-mix] munmap anon");
        munmap(fb, fb_len);
        close(fd);
        return 1;
    }

    unsigned char *anon2 = mmap(NULL, page * 2, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (anon2 == MAP_FAILED) {
        perror("[mmap-mix] mmap anon2");
        munmap(fb, fb_len);
        close(fd);
        return 1;
    }

    memset(anon2, 0xA5, page * 2);
    if (munmap(anon2, page * 2) != 0) {
        perror("[mmap-mix] munmap anon2");
        munmap(fb, fb_len);
        close(fd);
        return 1;
    }

    if (munmap(fb, fb_len) != 0) {
        perror("[mmap-mix] munmap fb");
        close(fd);
        return 1;
    }

    if (close(fd) != 0) {
        perror("[mmap-mix] close");
        return 1;
    }

    printf("[mmap-mix] fb0 + anon coexistence OK\n");
    return 0;
}
