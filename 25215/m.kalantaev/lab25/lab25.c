#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int write_all(int fd, const unsigned char *data, size_t size)
{
    while (size > 0) {
        ssize_t written = write(fd, data, size);

        if (written == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }

        data += written;
        size -= (size_t)written;
    }
    return 0;
}

static int copy_text(int input, int output, bool uppercase)
{
    unsigned char buffer[4096];

    for (;;) {
        ssize_t count = read(input, buffer, sizeof buffer);

        if (count == 0) {
            return EXIT_SUCCESS;
        }
        if (count == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("read");
            return EXIT_FAILURE;
        }

        if (uppercase) {
            for (ssize_t i = 0; i < count; ++i) {
                buffer[i] = (unsigned char)toupper(buffer[i]);
            }
        }
        if (write_all(output, buffer, (size_t)count) == -1) {
            perror("write");
            return EXIT_FAILURE;
        }
    }
}

int main(void)
{
    int ends[2];
    int status;
    int result;
    pid_t child;

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return EXIT_FAILURE;
    }
    if (pipe(ends) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }
    child = fork();
    if (child == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (child == 0) {
        if (close(ends[1]) == -1) {
            perror("close pipe write end in child");
            _exit(EXIT_FAILURE);
        }
        _exit(copy_text(ends[0], STDOUT_FILENO, true));
    }

    if (close(ends[0]) == -1) {
        perror("close pipe read end in parent");
        return EXIT_FAILURE;
    }
    result = copy_text(STDIN_FILENO, ends[1], false);

    /* Let the child see EOF before waiting for it. */
    if (close(ends[1]) == -1) {
        perror("close pipe write end in parent");
        return EXIT_FAILURE;
    }
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            perror("waitpid");
            return EXIT_FAILURE;
        }
    }

    if (WIFSIGNALED(status)) {
        fprintf(stderr, "Child terminated by signal %d\n", WTERMSIG(status));
    }
    if (result == EXIT_SUCCESS && WIFEXITED(status) &&
        WEXITSTATUS(status) == EXIT_SUCCESS) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
