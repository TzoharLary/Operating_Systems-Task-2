#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>

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

void start_tcp_server(int port, int *client_fd) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

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

    if ((*client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }
    close(server_fd);
}

void start_tcp_client(const char *hostname, int port, int *client_fd) {
    struct addrinfo hints, *res, *p;
    int sockfd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[6];
    snprintf(port_str, sizeof port_str, "%d", port);

    int status;
    if ((status = getaddrinfo(hostname, port_str, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    for (p = res; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("connect");
            continue;
        }

        break; // successfully connected
    }

    if (p == NULL) {
        fprintf(stderr, "Failed to connect to %s:%d\n", hostname, port);
        exit(EXIT_FAILURE);
    }

    *client_fd = sockfd;
    freeaddrinfo(res);
}

void handle_io(int input_fd, int output_fd) {
    char buffer[BUFFER_SIZE];
    struct pollfd fds[2];
    int nfds = 2;

    fds[0].fd = input_fd;
    fds[0].events = POLLIN;
    fds[1].fd = output_fd;
    fds[1].events = POLLIN;

    while (1) {
        int ret = poll(fds, nfds, -1);
        if (ret == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                ssize_t n = read(fds[i].fd, buffer, BUFFER_SIZE);
                if (n <= 0) {
                    if (n < 0) perror("read");
                    return;
                }
                int dest_fd = (fds[i].fd == input_fd) ? output_fd : STDOUT_FILENO;
                if (write(dest_fd, buffer, n) != n) {
                    perror("write");
                    return;
                }
            }
        }
    }
}


int main(int argc, char *argv[]) {
    int opt;
    char *command = NULL;
    char *input_param = NULL;
    char *output_param = NULL;
    int is_bidirectional = 0;

    while ((opt = getopt(argc, argv, "e:i:o:b:")) != -1) {
        switch (opt) {
            case 'e':
                command = optarg;
                break;
            case 'i':
                input_param = optarg;
                break;
            case 'o':
                output_param = optarg;
                break;
            case 'b':
                input_param = output_param = optarg;
                is_bidirectional = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-e \"command\"] [-i input] [-o output] [-b both]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    int input_fd = STDIN_FILENO;
    int output_fd = STDOUT_FILENO;

    if (input_param && strncmp(input_param, "TCPS", 4) == 0) {
        int port = atoi(input_param + 4);
        start_tcp_server(port, &input_fd);
        if (is_bidirectional) {
            output_fd = input_fd;
        }
    }

    if (output_param && strncmp(output_param, "TCPC", 4) == 0 && !is_bidirectional) {
        char *output_param_copy = strdup(output_param + 4);
        char *hostname = strtok(output_param_copy, ",");
        char *port_str = strtok(NULL, ",");
        if (hostname == NULL || port_str == NULL) {
            fprintf(stderr, "Invalid TCPC parameters\n");
            free(output_param_copy);
            return EXIT_FAILURE;
        }
        int port = atoi(port_str);
        start_tcp_client(hostname, port, &output_fd);
        free(output_param_copy);
    }

    if (command) {
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
            if (input_fd != STDIN_FILENO) {
                dup2(input_fd, STDIN_FILENO);
            }
            close(pipe_out[0]);
            if (output_fd != STDOUT_FILENO) {
                dup2(output_fd, STDOUT_FILENO);
            } else {
                dup2(pipe_out[1], STDOUT_FILENO);
            }
            close(pipe_out[1]);
            execute_program(command);
        } else { // Parent process
            close(pipe_out[1]);
            if (output_fd != STDOUT_FILENO) {
                handle_io(pipe_out[0], output_fd);
            } else {
                char buffer[BUFFER_SIZE];
                ssize_t n;
                while ((n = read(pipe_out[0], buffer, BUFFER_SIZE - 1)) > 0) {
                    buffer[n] = '\0';
                    write(STDOUT_FILENO, buffer, n);
                }
            }
            close(pipe_out[0]);
            wait(NULL);
        }
    } else {
        // Handle I/O without executing a command
        handle_io(input_fd, output_fd);
    }

    // Close file descriptors
    if (input_fd != STDIN_FILENO) close(input_fd);
    if (output_fd != STDOUT_FILENO && output_fd != input_fd) close(output_fd);

    return EXIT_SUCCESS;
}






// Example 1: Running `mync` with input from a TCP server and output to a TCP client

// ./mync -e "./ttt 123456789" -i TCPS4050 -o TCPClocalhost,4455

// To run this, first open a new terminal and start a TCP server listening on port 4455:
// nc -l 4455

// After that, run the following command in the original terminal:
// ./mync -e “ttt 123456789” -i TCPS4090 -o TCPClocalhost,4455

// Finally, open another terminal and run:
// telnet localhost 4050



// Example 2: Running `mync` with output to a TCP client

// ./mync -e "ttt 123456789" -o TCPClocalhost,4455
// To run this, first open a new terminal and start a TCP server listening on port 4455:
// nc -l -p 4455
// After that, run the following command in the original terminal:
// ./mync -e "ttt 123456789" -o TCPClocalhost,4455

// Example 3: Running `mync` with both input and output from a TCP server

// ./mync -e "ttt 123456789" -b TCPS4055
// To run this, use the following command in the original terminal:
// ./mync -e "ttt 123456789" -b TCPS4095
// Then, open another terminal and run:
// telnet localhost 4095

// Example 4: Running `mync` with input from a TCP server
// ./mync -e "./ttt 123456789" -i TCPS4050

// To run this, use the following command in the original terminal:
// ./mync -e "./ttt 123456789" -i TCPS4050
// Then, open another terminal and run:
// nc localhost 4050


//./mync -e "./ttt 123456789" -i TCPS4050 
//./mync -e "./ttt 123456789" -i UDPS4050