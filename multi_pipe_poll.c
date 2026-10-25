/* multi_pipe_poll.c — мультиплексування читання з кількох pipe через poll */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>

#define NUM_WRITERS 4
#define MSG_COUNT   3

static void writer_process(int write_fd, int id, int delay_ms)
{
    srand(time(NULL) ^ (getpid() << 16));

    for (int i = 0; i < MSG_COUNT; i++) {
        usleep(delay_ms * 1000);

        char msg[128];
        snprintf(msg, sizeof(msg), "Писець %d: повідомлення %d (затримка %d мс)",
                 id, i + 1, delay_ms);

        write(write_fd, msg, strlen(msg) + 1);
    }

    close(write_fd);
    _exit(0);
}

int main(void)
{
    int pipes[NUM_WRITERS][2];
    pid_t pids[NUM_WRITERS];
    int delays[] = { 100, 300, 200, 500 };

    printf("=== Мультиплексування pipe через poll() ===\n\n");

    for (int i = 0; i < NUM_WRITERS; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            return EXIT_FAILURE;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return EXIT_FAILURE;
        }

        if (pid == 0) {
            close(pipes[i][0]);
            writer_process(pipes[i][1], i + 1, delays[i]);
        }

        close(pipes[i][1]);
        pids[i] = pid;
    }

    struct pollfd fds[NUM_WRITERS];
    int active = NUM_WRITERS;

    for (int i = 0; i < NUM_WRITERS; i++) {
        fds[i].fd     = pipes[i][0];
        fds[i].events = POLLIN;
    }

    while (active > 0) {
        int ready = poll(fds, NUM_WRITERS, 5000);
        if (ready == -1) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (ready == 0) {
            printf("[main] Тайм-аут poll()...\n");
            break;
        }

        for (int i = 0; i < NUM_WRITERS; i++) {
            if (fds[i].fd == -1) continue;

            if (fds[i].revents & POLLIN) {
                char buf[256];
                ssize_t n = read(fds[i].fd, buf, sizeof(buf));
                if (n > 0) {
                    time_t now = time(NULL);
                    struct tm *tm_info = localtime(&now);
                    char time_buf[32];
                    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
                    printf("[%s] Отримано -> %s\n", time_buf, buf);
                }
            }

            if (fds[i].revents & (POLLHUP | POLLERR)) {
                close(fds[i].fd);
                fds[i].fd = -1;
                active--;
            }
        }
    }

    for (int i = 0; i < NUM_WRITERS; i++)
        waitpid(pids[i], NULL, 0);

    printf("\nУсі писці завершили роботу.\n");
    return EXIT_SUCCESS;
}
