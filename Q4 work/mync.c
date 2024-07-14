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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>

#define BUFFER_SIZE 1024

struct io_params {
    int input_is_udp;
    int output_is_udp;
    int input_is_unix;
    int output_is_unix;
    struct sockaddr_in dest_addr;
    socklen_t dest_addr_len;
    struct sockaddr_un dest_unix_addr;
    socklen_t dest_unix_addr_len;
};


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

        char ipstr[INET6_ADDRSTRLEN];
        void *addr;
        char *ipver;

        if (p->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
        } 
        else { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = "IPv6";
        }

        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        // printf("Trying to connect to %s (%s)\n", ipstr, ipver);

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            if (errno == ECONNREFUSED) {
                    if (p->ai_family == AF_INET6) {
                        fprintf(stderr, "Failed to connect to %s (%s): because no server is listening at the specified address and port, or the server is only listening on IPv4 (Connection refused)\n", ipstr, ipver);
                    } 
                    else {
                        fprintf(stderr, "Failed to connect to %s (%s): because no server is listening at the specified address and port (Connection refused)\n", ipstr, ipver);
                    }
            } 
            else if (errno == ETIMEDOUT) {
                fprintf(stderr, "Failed to connect to %s (%s): because the connection timed out\n", ipstr, ipver);
            } 
            else {
                fprintf(stderr, "Failed to connect to %s (%s): because %s\n", ipstr, ipver, strerror(errno));
            }
            close(sockfd);
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

int start_udp_server(int port) {
    int sockfd;

    // Create socket
    sockfd = socket(AF_INET6, SOCK_DGRAM, 0);  // SOCK_DGRAM for UDP
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    // Allow the socket to bind to both IPv4 and IPv6 addresses
    if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0) {
        perror("setsockopt IPV6_V6ONLY failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in6 server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    
    // Fill server information
    server_addr.sin6_family = AF_INET6; // IPv6
    server_addr.sin6_addr = in6addr_any; // Accept connections on any IP address (both IPv4 and IPv6)
    server_addr.sin6_port = htons(port);

    // Bind the socket with the server address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("UDP server started on port %d\n", port);
    return sockfd;
}

void start_udp_client(const char *hostname, int port, int *client_fd, struct sockaddr_in *dest_addr) {
    struct addrinfo hints, *res, *p;
    int sockfd;

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
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }

        // Check if the socket is of type SOCK_DGRAM
        int sock_type;
        socklen_t len = sizeof(sock_type);
        if (getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &sock_type, &len) == -1) {
            perror("getsockopt");
            close(sockfd);
            continue;
        }

        if (sock_type != SOCK_DGRAM) {
            fprintf(stderr, "Socket is not of type SOCK_DGRAM\n");
            close(sockfd);
            continue;
        }

        break; // successfully created socket
    }

    if (p == NULL) {
        fprintf(stderr, "Failed to create socket to %s:%d\n", hostname, port);
        exit(EXIT_FAILURE);
    }

    // Copy the destination address
    memset(dest_addr, 0, sizeof(struct sockaddr_in));
    dest_addr->sin_family = AF_INET;
    dest_addr->sin_port = htons(port);
    memcpy(&(dest_addr->sin_addr), &(((struct sockaddr_in *)p->ai_addr)->sin_addr), sizeof(struct in_addr));

    printf("UDP client will send to %s:%d\n", inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port));

    *client_fd = sockfd;
    freeaddrinfo(res);
}

void start_udssd_server(const char *path) {
    int sockfd;
    struct sockaddr_un servaddr;

    sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, path);

    bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

    // קבלת הודעות (Datagram)
    while (1) {
        char buffer[1024];
        recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
        printf("Received: %s\n", buffer);
    }

    close(sockfd);
}

void start_udsdc_client(const char *path) {
    int sockfd;
    struct sockaddr_un servaddr;

    sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, path);

    // שליחת הודעות (Datagram)
    while (1) {
        char buffer[1024];
        fgets(buffer, sizeof(buffer), stdin);
        sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
    }

    close(sockfd);
}

void start_udsss_server(const char *path) {
    int sockfd, connfd;
    struct sockaddr_un servaddr;

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, path);

    bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    listen(sockfd, 5);

    while (1) {
        connfd = accept(sockfd, NULL, NULL);
        // קבלת הודעות (Stream)
        while (1) {
            char buffer[1024];
            int n = read(connfd, buffer, sizeof(buffer));
            if (n <= 0) break;
            printf("Received: %s\n", buffer);
        }
        close(connfd);
    }

    close(sockfd);
}

void start_udscc_client(const char *path) {
    int sockfd;
    struct sockaddr_un servaddr;

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, path);

    connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

    // שליחת הודעות (Stream)
    while (1) {
        char buffer[1024];
        fgets(buffer, sizeof(buffer), stdin);
        write(sockfd, buffer, strlen(buffer));
    }

    close(sockfd);
}

void alarm_handler(int sig) {
    fprintf(stderr, "Timeout reached, terminating processes\n");
    exit(EXIT_FAILURE);
}


int is_socket(int fd) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    int result = getsockname(fd, (struct sockaddr *)&addr, &len);
    if (result == -1) {
        perror("getsockname");
    }
    return result == 0;
}


void handle_unix_io(int pipe_fd, int socket_fd, int output_fd, struct io_params *params) {
    char buffer[BUFFER_SIZE];
    struct pollfd fds[2];
    int nfds = 2;

    // fds[0] for pipe
    fds[0].fd = pipe_fd;
    fds[0].events = POLLIN;

    // fds[1] for socket
    fds[1].fd = socket_fd;
    fds[1].events = POLLIN;

    while (1) {
        int ret = poll(fds, nfds, -1);

        if (ret == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        // Handle pipe input (output from child process)
        if (fds[0].revents & POLLIN) {
            ssize_t n = read(fds[0].fd, buffer, BUFFER_SIZE);
            if (n <= 0) {
                if (n < 0) perror("read");
                return;
            }
            buffer[n] = '\0';

            // Send to output_fd (could be a Unix domain socket or STDOUT)
            if (params->output_is_unix) {
                ssize_t sent = sendto(output_fd, buffer, n, 0, (struct sockaddr *)&params->dest_unix_addr, params->dest_unix_addr_len);
                if (sent == -1) {
                    perror("sendto");
                    return;
                }
                printf("Sent %zd bytes to %s\n", sent, params->dest_unix_addr.sun_path);
            } else {
                ssize_t written = write(output_fd, buffer, n);
                if (written != n) {
                    perror("write");
                    return;
                }
            }
        }

        // Handle socket input (data from Unix domain socket)
        if (fds[1].revents & POLLIN) {
            ssize_t n = recvfrom(fds[1].fd, buffer, BUFFER_SIZE, 0, NULL, NULL);
            if (n == -1) {
                perror("recvfrom");
                return;
            }
            buffer[n] = '\0';

            // Send to output_fd (could be a pipe or another socket)
            if (params->output_is_unix) {
                ssize_t sent = sendto(output_fd, buffer, n, 0, (struct sockaddr *)&params->dest_unix_addr, params->dest_unix_addr_len);
                if (sent == -1) {
                    perror("sendto");
                    return;
                }
                printf("Sent %zd bytes to %s\n", sent, params->dest_unix_addr.sun_path);
            } else {
                ssize_t written = write(output_fd, buffer, n);
                if (written != n) {
                    perror("write");
                    return;
                }
            }
        }
    }
}

void handle_unix_process(char *command, int input_fd, int output_fd, struct io_params *params) {
    int pipe_out[2];
    if (pipe(pipe_out) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // Child process
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
    } else {
        // Parent process
        close(pipe_out[1]);
        if (output_fd != STDOUT_FILENO || params->input_is_unix || params->output_is_unix) {
            handle_unix_io(pipe_out[0], input_fd, output_fd, params);
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
}


void handle_io(int pipe_fd, int socket_fd, int output_fd, struct io_params *params) {
    if (params->input_is_unix || params->output_is_unix) {
        handle_unix_io(pipe_fd, socket_fd, output_fd, params);
        return;
    } 

    char buffer[BUFFER_SIZE];
    struct pollfd fds[2];
    int nfds = 2;

    // fds[0] for pipe
    fds[0].fd = pipe_fd;
    fds[0].events = POLLIN;

    // fds[1] for socket
    fds[1].fd = socket_fd;
    fds[1].events = POLLIN;

    while (1) {
        int ret = poll(fds, nfds, -1);

        if (ret == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        // Handle pipe input (output from child process)
        if (fds[0].revents & POLLIN) {
            ssize_t n = read(fds[0].fd, buffer, BUFFER_SIZE);
            if (n <= 0) {
                if (n < 0) perror("read");
                return;
            }
            buffer[n] = '\0';

            // Send to output_fd (could be a socket or STDOUT)
            if (params->output_is_udp) {
                ssize_t sent = sendto(output_fd, buffer, n, 0, (struct sockaddr *)&params->dest_addr, params->dest_addr_len);
                if (sent == -1) {
                    perror("sendto");
                    return;
                }
                printf("Sent %zd bytes to %s:%d\n", sent, inet_ntoa(params->dest_addr.sin_addr), ntohs(params->dest_addr.sin_port));
            } else {
                ssize_t written = write(output_fd, buffer, n);
                if (written != n) {
                    perror("write");
                    return;
                }
            }
        }

        // Handle socket input (data from network)
        if (fds[1].revents & POLLIN) {
            ssize_t n;
            if (params->input_is_udp) {
                n = recvfrom(fds[1].fd, buffer, BUFFER_SIZE, 0, NULL, NULL);
                if (n == -1) {
                    perror("recvfrom");
                    return;
                }
            } else {
                n = read(fds[1].fd, buffer, BUFFER_SIZE);
                if (n <= 0) {
                    if (n < 0) perror("read");
                    return;
                }
            }
            buffer[n] = '\0';

            // Send to output_fd (could be a pipe or another socket)
            if (params->output_is_udp) {
                ssize_t sent = sendto(output_fd, buffer, n, 0, (struct sockaddr *)&params->dest_addr, params->dest_addr_len);
                if (sent == -1) {
                    perror("sendto");
                    return;
                }
                printf("Sent %zd bytes to %s:%d\n", sent, inet_ntoa(params->dest_addr.sin_addr), ntohs(params->dest_addr.sin_port));
            } else {
                ssize_t written = write(output_fd, buffer, n);
                if (written != n) {
                    perror("write");
                    return;
                }
            }
        }
    }

}

void handle_process(char *command, int input_fd, int output_fd, struct io_params *params) {
    if (params->input_is_unix || params->output_is_unix) {
        handle_unix_process(command, input_fd, output_fd, params);
        return;
    }

    int pipe_out[2];
    if (pipe(pipe_out) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // Child process
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
    } else {
        // Parent process
        close(pipe_out[1]);
        if (output_fd != STDOUT_FILENO || params->input_is_udp || params->output_is_udp) {
            handle_io(pipe_out[0], input_fd, output_fd, params);
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
}

int main(int argc, char *argv[]) {
    int opt;
    char *command = NULL;
    char *input_param = NULL;
    char *output_param = NULL;
    int is_bidirectional = 0;
    int timeout = 0;

    struct io_params params = {0}; // Initialize the params struct

    // Parse command line arguments using getopt 
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

    // Set up alarm signal handler if timeout is specified
    if (timeout > 0) {
        signal(SIGALRM, alarm_handler);
        alarm(timeout);
    }

    // File descriptors for input and output
    int input_fd = STDIN_FILENO;
    int output_fd = STDOUT_FILENO;

    // Start server or client based on input and output parameters
    if (input_param) {
        if (strncmp(input_param, "TCPS", 4) == 0) {
            int port = atoi(input_param + 4);
            start_tcp_server(port, &input_fd);
            if (is_bidirectional) {
                output_fd = input_fd;
            }
        } else if (strncmp(input_param, "UDPS", 4) == 0) {
            int port = atoi(input_param + 4);
            input_fd = start_udp_server(port);
            params.input_is_udp = 1;
        } else if (strncmp(input_param, "UDSSD", 5) == 0) {
            start_udssd_server(input_param + 5);
            params.input_is_unix = 1;
        } else if (strncmp(input_param, "UDSSS", 5) == 0) {
            start_udsss_server(input_param + 5);
            params.input_is_unix = 1;
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
            start_udp_client(hostname, port, &output_fd, &params.dest_addr);
            params.output_is_udp = 1;
            free(output_param_copy);
        } else if (strncmp(output_param, "UDSCD", 5) == 0) {
            start_udsdc_client(output_param + 5);
            params.output_is_unix = 1;
        } else if (strncmp(output_param, "UDSCS", 5) == 0) {
            start_udscc_client(output_param + 5);
            params.output_is_unix = 1;
        }
    }

    if (command) {
        handle_process(command, input_fd, output_fd, &params);
    } else {
        handle_io(input_fd, input_fd, output_fd, &params);
    }

    // Close file descriptors
    if (input_fd != STDIN_FILENO) close(input_fd);
    if (output_fd != STDOUT_FILENO && output_fd != input_fd) close(output_fd);

    return EXIT_SUCCESS;
}


 // Example usage:

    // Example 1: Run a command with input from a UDp server to the standard output (STDOUT)
    // run the following command in the terminal
    //  ./mync -e "./ttt 123456789" -i UDPS4050
    // open another terminal and run the following command
    // nc -u localhost 4050

    // Example 2: Run a command with input from a UDP server and kill the process after 10 seconds
    // run the following command in the terminal
    // ./mync -e "./ttt 123456789" -i UDPS4050 -t 10
    // open another terminal and run the following command
    // nc -u localhost 4050

    // Example 3: Run a command with input from a UDP server and output to a TCP client
    // run the following command in the terminal for listening to the TCP client at port 4455
    // nc -l 4455 
    // open another terminal and run the following command for running the mync program 
    // with the ttt command and input from the UDP server at port 4050 and output to the TCP client at port 4455
    // ./mync -e "./ttt 123456789" -i UDPS4050 -o TCPClocalhost,4455
    // open another terminal and run the following command for sending data to the UDP server at port 4050
    // nc -u localhost 4050
    //ל0