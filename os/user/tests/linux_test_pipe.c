#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <string.h>

int main() {
    printf("--- Linux Pipe & Epoll Test ---\n");
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return 1;
    }
    printf("Pipe created: r=%d w=%d\n", pipefd[0], pipefd[1]);

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        return 1;
    }
    printf("Epoll created: %d\n", epfd);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = pipefd[0];
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev) < 0) {
        perror("epoll_ctl");
        return 1;
    }

    const char *msg = "hello from pipe";
    write(pipefd[1], msg, strlen(msg));
    printf("Written to pipe\n");

    struct epoll_event events[1];
    int nfds = epoll_wait(epfd, events, 1, 1000);
    if (nfds > 0) {
        printf("Epoll wait success: count=%d\n", nfds);
        char buf[32];
        int n = read(pipefd[0], buf, sizeof(buf)-1);
        if (n > 0) {
            buf[n] = 0;
            printf("Read from pipe: '%s' [OK]\n", buf);
        }
    } else {
        printf("Epoll wait failed or timed out\n");
    }

    close(pipefd[0]);
    close(pipefd[1]);
    close(epfd);
    printf("--- Pipe/Epoll Test Complete ---\n");
    return 0;
}
