/* TCP echo-сервер на epoll */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT        9090
#define MAX_EVENTS  64
#define BUF_SIZE    1024

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(void)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind"); return 1;
    }
    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen"); return 1;
    }
    set_nonblocking(listen_fd);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) { perror("epoll_create1"); return 1; }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        perror("epoll_ctl listen_fd");
        return 1;
    }

    struct epoll_event events[MAX_EVENTS];

    printf("Echo-сервер запущено на порту %d (epoll)\n", PORT);
    printf("Для тестування: nc localhost %d\n\n", PORT);

    for (;;) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == listen_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd == -1) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                        perror("accept");
                    continue;
                }

                set_nonblocking(client_fd);
                ev.events = EPOLLIN | EPOLLET; // Можна і без EPOLLET, за бажанням
                ev.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl client_fd");
                    close(client_fd);
                    continue;
                }

                printf("[Server] Нове підключення з сокета %d\n", client_fd);
            } else {
                int client_fd = events[i].data.fd;
                char buf[BUF_SIZE];
                ssize_t n = read(client_fd, buf, sizeof(buf));

                if (n <= 0) {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                        continue;
                    printf("[Server] Клієнт на сокеті %d відключився\n", client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                } else {
                    printf("[Server] Отримано %zd байт від сокета %d\n", n, client_fd);
                    write(client_fd, buf, n); // Echo
                }
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);
    return EXIT_SUCCESS;
}
