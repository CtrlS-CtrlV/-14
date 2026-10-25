/* неблокуючий I/O та цикл подій з select */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>

#define NUM_PIPES 3
#define MSG_COUNT 4

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void writer(int fd, int id, int delay_ms)
{
    for (int i = 0; i < MSG_COUNT; i++) {
        usleep(delay_ms * 1000);
        char msg[128];
        snprintf(msg, sizeof(msg), "pipe_%d:msg_%d", id, i + 1);
        write(fd, msg, strlen(msg) + 1);
    }
    close(fd);
    _exit(0);
}

int main(void)
{
    int pipes[NUM_PIPES][2];
    pid_t pids[NUM_PIPES];
    int delays[] = { 150, 400, 250 };

    printf("=== Event loop з select + O_NONBLOCK ===\n\n");

    for (int i = 0; i < NUM_PIPES; i++) {
        if (pipe(pipes[i]) == -1) { perror("pipe"); return 1; }

        set_nonblocking(pipes[i][0]);

        pid_t pid = fork();
        if (pid == 0) {
            close(pipes[i][0]);
            writer(pipes[i][1], i, delays[i]);
        }
        close(pipes[i][1]);
        pids[i] = pid;
    }

    int active = NUM_PIPES;

    while (active > 0) {
        fd_set readfds;
        int maxfd = -1;

        FD_ZERO(&readfds);
        for (int i = 0; i < NUM_PIPES; i++) {
            if (pipes[i][0] != -1) {
                FD_SET(pipes[i][0], &readfds);
                if (pipes[i][0] > maxfd) {
                    maxfd = pipes[i][0];
                }
            }
        }

        struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };

        int ready = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (ready == -1) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (ready == 0) {
            printf("[select] Тайм-аут...\n");
            break;
        }

        for (int i = 0; i < NUM_PIPES; i++) {
            if (pipes[i][0] == -1) continue;

            if (FD_ISSET(pipes[i][0], &readfds)) {
                char buf[256];
                ssize_t n = read(pipes[i][0], buf, sizeof(buf));
                if (n > 0) {
                    printf("  [select] отримано: %s\n", buf);
                } else if (n == 0) {
                    printf("  pipe %d закрито\n", i);
                    close(pipes[i][0]);
                    pipes[i][0] = -1;
                    active--;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Немає даних — це нормально для O_NONBLOCK
                } else {
                    perror("read");
                }
            }
        }
    }

    for (int i = 0; i < NUM_PIPES; i++)
        waitpid(pids[i], NULL, 0);

    printf("\n=== Порівняння механізмів мультиплексування I/O ===\n\n");
    printf("┌────────────┬────────────────┬───────────────────────┬──────────────┐\n");
    printf("│ Механізм   │ Складність     │ Обмеження             │ Портативність│\n");
    printf("├────────────┼────────────────┼───────────────────────┼──────────────┤\n");
    printf("│ select()   │ O(n)           │ FD_SETSIZE (1024)     │ POSIX, всі ОС│\n");
    printf("│ poll()     │ O(n)           │ Без обмеження кільк.  │ POSIX, всі ОС│\n");
    printf("│ epoll      │ O(1) на подію  │ Без обмеження         │ Лише Linux   │\n");
    printf("│ kqueue     │ O(1) на подію  │ Без обмеження         │ BSD / macOS  │\n");
    printf("│ IOCP       │ O(1) на подію  │ Без обмеження         │ Лише Windows │\n");
    printf("└────────────┴────────────────┴───────────────────────┴──────────────┘\n");

    printf("\n=== Висновок ===\n");
    printf("select: простий, портативний, але обмежений FD_SETSIZE.\n");
    printf("poll: знімає обмеження FD_SETSIZE, але O(n) при кожному виклику.\n");
    printf("epoll: оптимальний для Linux-серверів з тисячами з'єднань.\n");
    printf("O_NONBLOCK + EAGAIN: дозволяє не блокувати потік при відсутності даних.\n");

    return EXIT_SUCCESS;
}
