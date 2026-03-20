#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

int main(void) {
  struct utsname u;
  if (uname(&u) == 0) {
    printf("uname: %s %s %s %s %s\n", u.sysname, u.nodename, u.release,
           u.version, u.machine);
  } else {
    printf("uname failed\n");
  }

  printf("pid: %d\n", (int)getpid());

  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
    printf("clock: %ld.%09ld\n", (long)ts.tv_sec, (long)ts.tv_nsec);
  } else {
    printf("clock_gettime failed\n");
  }

  size_t len = 4096;
  void *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) {
    printf("mmap failed\n");
    return 1;
  }

  const char *msg = "mmap ok";
  memcpy(p, msg, strlen(msg) + 1);
  printf("mmap: %s\n", (char *)p);
  munmap(p, len);

  struct timespec req;
  req.tv_sec = 0;
  req.tv_nsec = 50 * 1000 * 1000;
  nanosleep(&req, NULL);

  printf("done\n");
  return 0;
}
