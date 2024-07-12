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

int start_udp_server(int port) {
    int sockfd;

    // Create socket
    sockfd = socket(AF_INET6, SOCK_DGRAM, 0);  // SOCK_DGRAM for UDP
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("Socket created with fd: %d\n", sockfd);  // Debugging line

    int opt = 1;

    // Allow the socket to bind to both IPv4 and IPv6 addresses
    if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0) {
        perror("setsockopt IPV6_V6ONLY failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("setsockopt IPV6_V6ONLY succeeded\n");  // Debugging line

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("setsockopt SO_REUSEADDR succeeded\n");  // Debugging line

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
    printf("bind succeeded\n");  // Debugging line

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

void alarm_handler(int sig) {
    fprintf(stderr, "Timeout reached, terminating processes\n");
    exit(EXIT_FAILURE);
}


int is_socket(int fd) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    int result = getsockname(fd, (struct sockaddr *)&addr, &len);
    printf("getsockname result: %d\n", result);
    if (result == -1) {
        perror("getsockname");
    }
    return result == 0;
}

void send_test_message(int fd, struct sockaddr_in *dest_addr, socklen_t dest_addr_len) {
    const char *test_message = "Test message";
    ssize_t sent = sendto(fd, test_message, strlen(test_message), 0, (struct sockaddr *)dest_addr, dest_addr_len);
    if (sent == -1) {
        perror("sendto");
        printf("Failed to send test message\n");
    } else {
        printf("Sent test message: %s\n", test_message);
    }
}

void check_open_fds() {
    printf("Open file descriptors:\n");
    for (int fd = 0; fd < 256; fd++) {
        if (fcntl(fd, F_GETFD) != -1) {
            printf("FD %d is open\n", fd);
        }
    }
}


void handle_io(int pipe_fd, int socket_fd, int output_fd, int input_is_udp, int output_is_udp, struct sockaddr_in *dest_addr, socklen_t dest_addr_len) {
    printf("someone called handle_io\n");
    char buffer[BUFFER_SIZE];
    struct pollfd fds[2];
    int nfds = 2;

    // fds[0] for pipe
    fds[0].fd = pipe_fd;
    fds[0].events = POLLIN;

    // fds[1] for socket
    fds[1].fd = socket_fd;
    fds[1].events = POLLIN;



    // int is_udp = (dest_addr != NULL);  // Check if the connection is UDP
    // printf("Before checking is_socket in handle_io\n");

    
    // int input_is_socket = is_socket(input_fd); // Check if input_fd is a socket
    // printf("After checking is_socket in handle_io\n");
    // printf("input_is_socket is %d\n", input_is_socket);

    // printf("is_udp is %d\n", is_udp);

    printf("input_is_udp is %d\n", input_is_udp);
    printf("output_is_udp is %d\n", output_is_udp);

    // if (!input_is_socket) {
    //     fprintf(stderr, "Error: input_fd is not a socket\n");
    //     return;
    // }

    while (1) {
        printf("Polling for events...\n");
        int ret = poll(fds, nfds, -1);
        printf("After poll, ret = %d\n", ret);

        if (ret == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        // Handle pipe input (output from child process)
        if (fds[0].revents & POLLIN) {
            printf("fds[0].revents & POLLIN\n");
            ssize_t n = read(fds[0].fd, buffer, BUFFER_SIZE);
            if (n <= 0) {
                if (n < 0) perror("read");
                return;
            }
            buffer[n] = '\0';
            printf("on fds[0].revents & POLLIN condition, Received from pipe: %s\n", buffer);

            // Send to output_fd (could be a socket or STDOUT)
            if (output_is_udp) {
                ssize_t sent = sendto(output_fd, buffer, n, 0, (struct sockaddr *)dest_addr, dest_addr_len);
                if (sent == -1) {
                    perror("sendto");
                    return;
                }
                printf("Sent %zd bytes to %s:%d\n", sent, inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port));
            } else {
                ssize_t written = write(output_fd, buffer, n);
                if (written != n) {
                    perror("write");
                    return;
                }
                printf("on fds[0].revents & POLLIN condition, Written: %s\n", buffer);
            }
        }

        // Handle socket input (data from network)
        if (fds[1].revents & POLLIN) {
            printf("fds[1].revents & POLLIN\n");
            ssize_t n;
            if (input_is_udp) {
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
            printf("Received from socket: %s\n", buffer);

            // Send to output_fd (could be a pipe or another socket)
            if (output_is_udp) {
                ssize_t sent = sendto(output_fd, buffer, n, 0, (struct sockaddr *)dest_addr, dest_addr_len);
                if (sent == -1) {
                    perror("sendto");
                    return;
                }
                printf("Sent %zd bytes to %s:%d\n", sent, inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port));
            } else {
                ssize_t written = write(output_fd, buffer, n);
                if (written != n) {
                    perror("write");
                    return;
                }
                printf("Written: %s\n", buffer);
            }
   
        }
    }
    printf("Exiting handle_io\n");

}

void handle_process(char *command, int input_fd, int output_fd, int input_is_udp, int output_is_udp, struct sockaddr_in *dest_addr, socklen_t dest_addr_len) {
       
        int pipe_out[2];
        if (pipe(pipe_out) == -1) {
            perror("pipe");
        exit(EXIT_FAILURE);
        }
        printf("input_fd: %d\n", input_fd);
        printf("output_fd: %d\n", output_fd);

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
        exit(EXIT_FAILURE);
        }

        if (pid == 0) {
             // Child process
             printf("child process started\n");
            if (input_fd != STDIN_FILENO) {
                // we say to the child process to read the standard input
                // from the input_fd instead the terminal
                dup2(input_fd, STDIN_FILENO);
            }
            // close pipe_out[0] that means close the read end of the pipe
            close(pipe_out[0]);
          
            // if (input_is_udp)
            // {
            //     close(input_fd);
            // }
            

            if (output_fd != STDOUT_FILENO) {
                // we say to the child process to write the standard output
                // to the output_fd instead the terminal 
                dup2(output_fd, STDOUT_FILENO);
            } 
            else {
                // we say to the child process to write the standard output
                // to the pipe instead the terminal
                dup2(pipe_out[1], STDOUT_FILENO);
            }
            close(pipe_out[1]);
            execute_program(command);
        } 
        else { // Parent process
            printf("parent process started\n");
            close(pipe_out[1]);
            if (output_fd != STDOUT_FILENO || (input_is_udp || output_is_udp)) {
                // If output_fd is not STDOUT or if the connection is UDP, handle I/O
                handle_io(pipe_out[0], input_fd, output_fd, input_is_udp, output_is_udp, dest_addr, dest_addr_len);
                // handle_io(input_fd, output_fd, input_is_udp, output_is_udp, dest_addr, dest_addr_len);

            } 
            else { 
                // If output_fd is STDOUT and dest_addr is NULL, write directly to STDOUT
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
    int input_is_udp = 0; // Check if input is a UDP connection
    int output_is_udp = 0; // Check if output is a UDP connection

    struct sockaddr_in dest_addr;
    socklen_t dest_addr_len = sizeof(dest_addr);


    printf("dest_addr is %p\n", &dest_addr);
    printf("Starting main function...\n");

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
            input_is_udp = 1;
            // if (!output_param) {
            //     output_fd = input_fd;
            // }
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
            start_udp_client(hostname, port, &output_fd, &dest_addr);
            output_is_udp = 1;
            free(output_param_copy);
        }
    }

    if (command) {
        handle_process(command, input_fd, output_fd, input_is_udp, output_is_udp, &dest_addr, dest_addr_len);
    } else {
        handle_io(input_fd, input_fd, output_fd, input_is_udp, output_is_udp, &dest_addr, dest_addr_len);
    }


    // Close file descriptors
    if (input_fd != STDIN_FILENO) close(input_fd);
    if (output_fd != STDOUT_FILENO && output_fd != input_fd) close(output_fd);

    return EXIT_SUCCESS;
}
