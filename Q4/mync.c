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
#include <signal.h>

#define BUFFER_SIZE 1024

volatile sig_atomic_t timeout_occurred = 0;

void handle_timeout(int signum) {
    timeout_occurred = 1;
}

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

void start_udp_server(int port, int *server_fd) {
    struct sockaddr_in address;

    if ((*server_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(*server_fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("UDP bind failed");
        exit(EXIT_FAILURE);
    }
}

void start_udp_client(const char *hostname, int port, int *client_fd) {
    struct addrinfo hints, *res, *p;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[6];
    snprintf(port_str, sizeof port_str, "%d", port);

    int status;
    if ((status = getaddrinfo(hostname, port_str, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    for (p = res; p != NULL; p = p->ai_next) {
        if ((*client_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("UDP socket creation failed");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "Failed to create UDP socket\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);
}

void handle_io(int input_fd, int output_fd, int is_udp_server, int is_udp_client) {
    char buffer[BUFFER_SIZE];
    struct pollfd fds[2];
    int nfds = 2;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    fds[0].fd = input_fd;
    fds[0].events = POLLIN;
    fds[1].fd = output_fd;
    fds[1].events = POLLIN;

    while (!timeout_occurred) {
        int ret = poll(fds, nfds, -1);
        if (ret == -1) {
            if (errno == EINTR) continue;
            perror("poll");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                ssize_t n;
                if (is_udp_server && fds[i].fd == input_fd) {
                    n = recvfrom(fds[i].fd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);
                } else if (is_udp_client && fds[i].fd == output_fd) {
                    n = sendto(fds[i].fd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, client_addr_len);
                } else {
                    n = read(fds[i].fd, buffer, BUFFER_SIZE);
                }

                if (n <= 0) {
                    if (n < 0) perror("read/recvfrom");
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
    int timeout = 0;

    while ((opt = getopt(argc, argv, "e:i:o:b:t:")) != -1) {
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
            case 't':
                timeout = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-e \"command\"] [-i input] [-o output] [-b both] [-t timeout]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    int input_fd = STDIN_FILENO;
    int output_fd = STDOUT_FILENO;
    int is_udp_server = 0;
    int is_udp_client = 0;

    if (input_param) {
        if (strncmp(input_param, "TCPS", 4) == 0) {
            int port = atoi(input_param + 4);
            start_tcp_server(port, &input_fd);
            if (is_bidirectional) {
                output_fd = input_fd;
            }
        } else if (strncmp(input_param, "UDPS", 4) == 0) {
            int port = atoi(input_param + 4);
            start_udp_server(port, &input_fd);
            is_udp_server = 1;
        }
    }

    if (output_param && !is_bidirectional) {
        if (strncmp(output_param, "TCPC", 4) == 0) {
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
        } else if (strncmp(output_param, "UDPC", 4) == 0) {
            char *output_param_copy = strdup(output_param + 4);
            char *hostname = strtok(output_param_copy, ",");
            char *port_str = strtok(NULL, ",");
            if (hostname == NULL || port_str == NULL) {
                fprintf(stderr, "Invalid UDPC parameters\n");
                free(output_param_copy);
                return EXIT_FAILURE;
            }
            int port = atoi(port_str);
            start_udp_client(hostname, port, &output_fd);
            is_udp_client = 1;
            free(output_param_copy);
        }
    }

    if (timeout > 0) {
        signal(SIGALRM, handle_timeout);
        alarm(timeout);
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
                handle_io(pipe_out[0], output_fd, is_udp_server, is_udp_client);
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
        handle_io(input_fd, output_fd, is_udp_server, is_udp_client);
    }

    // Close file descriptors
    if (input_fd != STDIN_FILENO) close(input_fd);
    if (output_fd != STDOUT_FILENO && output_fd != input_fd) close(output_fd);

    return EXIT_SUCCESS;
}