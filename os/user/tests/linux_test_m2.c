#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdint.h>

/* DRM IOCTL VERSION Constant (0xC0406400) */
#define DRM_IOCTL_VERSION 0xC0406400

struct drm_version {
  int version_major;
  int version_minor;
  int version_patchlevel;
  uint64_t name_len;
  char *name;
  uint64_t date_len;
  char *date;
  uint64_t desc_len;
  char *desc;
};

int main() {
    printf("\n--- ChrysalisOS M2 Validation ---\n");
    
    /* 1. Identity Test */
    uid_t uid = getuid();
    gid_t gid = getgid();
    printf("[1] Identity: UID=%d, GID=%d %s\n", uid, gid, (uid == 0 && gid == 0) ? "[OK]" : "[WARN: not root]");

    /* 2. DevFS Test - Input */
    int fd_input = open("/dev/input/event0", O_RDONLY);
    if (fd_input >= 0) {
        printf("[2] /dev/input/event0: [OK]\n");
        close(fd_input);
    } else {
        printf("[2] /dev/input/event0: [FAILED]\n");
    }

    /* 3. DevFS Test - DRM */
    int fd_drm = open("/dev/dri/card0", O_RDWR);
    if (fd_drm >= 0) {
        printf("[3] /dev/dri/card0: [OK]\n");
        
        struct drm_version v;
        memset(&v, 0, sizeof(v));
        if (ioctl(fd_drm, DRM_IOCTL_VERSION, &v) == 0) {
            printf("    DRM Driver Version: %d.%d.%d [OK]\n", 
                   v.version_major, v.version_minor, v.version_patchlevel);
        } else {
            printf("    DRM IOCTL_VERSION: [FAILED]\n");
        }
        close(fd_drm);
    } else {
        printf("[3] /dev/dri/card0: [FAILED]\n");
    }

    /* 4. ProcFS Test */
    FILE* f = fopen("/proc/self/maps", "r");
    if (f) {
        printf("[4] /proc/self/maps: [OK]\n");
        char buf[256];
        printf("    Content Sample:\n");
        while (fgets(buf, sizeof(buf), f)) {
            printf("      %s", buf);
        }
        fclose(f);
    } else {
        printf("[4] /proc/self/maps: [FAILED]\n");
    }

    /* 5. Console Test */
    int fd_cons = open("/dev/console", O_WRONLY);
    if (fd_cons >= 0) {
        const char *msg = "[5] /dev/console: [OK] (written via /dev/console)\n";
        write(fd_cons, msg, strlen(msg));
        close(fd_cons);
    } else {
        printf("[5] /dev/console: [FAILED]\n");
    }

    printf("--- Validation Complete ---\n\n");
    return 0;
}
