#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#define BUFFER_SIZE 1024

int main() {
    int pipe_in[2], pipe_out[2];
    char buffer[BUFFER_SIZE];
    ssize_t n;

    if (pipe(pipe_in) == -1 || pipe(pipe_out) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) { // Child process
        close(pipe_in[0]);
        close(pipe_out[1]);

        // Write to pipe_in
        const char *msg = "Hello from child process\n";
        write(pipe_in[1], msg, strlen(msg));
        close(pipe_in[1]); // Close writing end after writing

        // Read from pipe_out
        while ((n = read(pipe_out[0], buffer, BUFFER_SIZE)) > 0) {
            write(STDOUT_FILENO, buffer, n);
        }
        close(pipe_out[0]); // Close reading end after reading

        exit(EXIT_SUCCESS);
    } else { // Parent process
        close(pipe_in[1]);
        close(pipe_out[0]);

        // Read from pipe_in
        while ((n = read(pipe_in[0], buffer, BUFFER_SIZE)) > 0) {
            printf("Parent process read from pipe_in: %.*s\n", (int)n, buffer);
        }
        close(pipe_in[0]); // Close reading end after reading

        // Write to pipe_out
        const char *msg = "Hello from parent process\n";
        write(pipe_out[1], msg, strlen(msg));
        close(pipe_out[1]); // Close writing end after writing

        wait(NULL); // Wait for child to finish
    }

    return EXIT_SUCCESS;
}
