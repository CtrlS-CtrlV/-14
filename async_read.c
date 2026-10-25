/* асинхронне читання файлів через POSIX AIO */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <aio.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#define NUM_FILES 3
#define BUF_SIZE  (1024 * 1024)  /* 1 МБ на файл */

static double elapsed_ms(struct timespec *start, struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * 1000.0 +
           (end->tv_nsec - start->tv_nsec) / 1e6;
}

static void create_test_file(const char *path, size_t size)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) { perror(path); return; }
    char *buf = calloc(1, size);
    memset(buf, 'A', size);
    write(fd, buf, size);
    free(buf);
    close(fd);
}

static void read_sequential(const char **paths, int count)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < count; i++) {
        int fd = open(paths[i], O_RDONLY);
        if (fd == -1) { perror(paths[i]); continue; }

        char *buf = malloc(BUF_SIZE);
        ssize_t n;
        size_t total = 0;
        while ((n = read(fd, buf, BUF_SIZE)) > 0)
            total += n;

        printf("  [послід.] %s: %zu байт\n", paths[i], total);
        free(buf);
        close(fd);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  Час послідовного читання: %.2f мс\n", elapsed_ms(&t0, &t1));
}

static void read_async(const char **paths, int count)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int fds[NUM_FILES];
    struct aiocb cb[NUM_FILES];
    char *buffers[NUM_FILES];

    for (int i = 0; i < count; i++) {
        fds[i] = open(paths[i], O_RDONLY);
        if (fds[i] == -1) { perror(paths[i]); continue; }

        buffers[i] = malloc(BUF_SIZE);

        memset(&cb[i], 0, sizeof(struct aiocb));
        cb[i].aio_fildes = fds[i];
        cb[i].aio_buf    = buffers[i];
        cb[i].aio_nbytes = BUF_SIZE;
        cb[i].aio_offset = 0;

        if (aio_read(&cb[i]) == -1) {
            perror("aio_read");
        }
    }

    for (int i = 0; i < count; i++) {
        while (aio_error(&cb[i]) == EINPROGRESS)
            usleep(100);

        int err = aio_error(&cb[i]);
        if (err == 0) {
            ssize_t bytes = aio_return(&cb[i]);
            printf("  [async] %s: %zd байт\n", paths[i], bytes);
        } else {
            fprintf(stderr, "  aio_error: %s\n", strerror(err));
        }

        free(buffers[i]);
        close(fds[i]);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  Час асинхронного читання: %.2f мс\n", elapsed_ms(&t0, &t1));
}

int main(void)
{
    printf("=== Порівняння послідовного та асинхронного читання ===\n\n");

    const char *paths[NUM_FILES] = {
        "/tmp/sp_aio_test_1.bin",
        "/tmp/sp_aio_test_2.bin",
        "/tmp/sp_aio_test_3.bin"
    };

    printf("Створення тестових файлів (%d × %d КБ)...\n", NUM_FILES, BUF_SIZE/1024);
    for (int i = 0; i < NUM_FILES; i++)
        create_test_file(paths[i], BUF_SIZE);

    printf("\n--- Послідовне (блокуюче) читання ---\n");
    read_sequential(paths, NUM_FILES);

    printf("\n--- Асинхронне читання (POSIX AIO) ---\n");
    read_async(paths, NUM_FILES);

    for (int i = 0; i < NUM_FILES; i++)
        unlink(paths[i]);

    printf("\n=== Висновок ===\n");
    printf("Асинхронне читання дозволяє ініціювати операції одночасно,\n"
           "що може зменшити загальний час при роботі з кількома файлами\n"
           "на різних пристроях або при високій латентності I/O.\n");

    return EXIT_SUCCESS;
}
