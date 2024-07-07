#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#define BUFFER_SIZE 1024

void execute_program(char *command) {
    char *argv[10]; // Adjust size as needed
    char *token;
    int i = 0;

    // Split the command into program and its arguments
    token = strtok(command, " ");
    while (token != NULL && i < 9) {
        argv[i++] = token;
        token = strtok(NULL, " ");
    }
    argv[i] = NULL;

    if (execvp(argv[0], argv) == -1) {
        perror("execvp");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3 || strcmp(argv[1], "-e") != 0) {
        fprintf(stderr, "Usage: %s -e \"command\"\n", argv[0]);
        return EXIT_FAILURE;
    }

    int pipe_out[2];

    if (pipe(pipe_out) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) { // Child process
        // Redirect stdout
        close(pipe_out[0]);
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_out[1]);

        // Execute the command
        execute_program(argv[2]);
    } else { // Parent process
        close(pipe_out[1]);

        char buffer[BUFFER_SIZE];
        ssize_t n;

        // Read from child's stdout and write to stdout
        while ((n = read(pipe_out[0], buffer, BUFFER_SIZE - 1)) > 0) {
            buffer[n] = '\0';
            write(STDOUT_FILENO, buffer, n);
        }

        if (n == -1) {
            perror("read");
        }

        close(pipe_out[0]);

        // Wait for child to finish
        wait(NULL);
    }

    return EXIT_SUCCESS;
}
