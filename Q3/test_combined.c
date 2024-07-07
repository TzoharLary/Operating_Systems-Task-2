#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

void handle_tcp_server_input(int port, int pipe_fd) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    printf("Accepted connection on port %d\n", port);

    while (1) {
        ssize_t n = read(new_socket, buffer, BUFFER_SIZE);
        if (n <= 0) {
            if (n == 0) {
                printf("Connection closed by client\n");
            } else {
                perror("read");
            }
            break;
        }
        printf("Read %zd bytes from TCP connection\n", n);
        if (write(pipe_fd, buffer, n) != n) {
            perror("write to pipe");
            break;
        }
    }

    close(new_socket);
    close(server_fd);
    close(pipe_fd); // סגירת הפייפ לאחר סיום השימוש
}

int main() {
    int pipe_fd[2];
    char buffer[BUFFER_SIZE];
    ssize_t n;

    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) { // Child process
        close(pipe_fd[0]);

        // Handle TCP server input and write to pipe
        handle_tcp_server_input(4050, pipe_fd[1]);

        close(pipe_fd[1]); // Close writing end after use
        exit(EXIT_SUCCESS);
    } else { // Parent process
        close(pipe_fd[1]);

        // Read from pipe and write to stdout
        while ((n = read(pipe_fd[0], buffer, BUFFER_SIZE)) > 0) {
            printf("Parent process read from pipe: %.*s\n", (int)n, buffer);
        }

        if (n == -1) {
            perror("read");
        }

        close(pipe_fd[0]); // Close reading end after use
        wait(NULL); // Wait for child to finish
    }

    return EXIT_SUCCESS;
}
