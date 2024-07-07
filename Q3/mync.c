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
    close(server_fd);  // סגירת server_fd לאחר קבלת client_fd
}



void start_tcp_client(const char *hostname, int port, int *client_fd) {
    struct addrinfo hints, *res, *p;
    int sockfd;

    printf("start_tcp_client: hostname=%s, port=%d\n", hostname, port);  // הודעת debug נוספת

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

        printf("start_tcp_client: trying to connect to %s\n", hostname);  // הודעת debug נוספת

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

    printf("TCP client connected to %s:%d, client_fd=%d\n", hostname, port, *client_fd);  // הודעת debug נוספת
}


void copy_pipe_to_tcp(int pipe_fd, int tcp_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t n;
    printf("Entering copy_pipe_to_tcp function\n");

    while ((n = read(pipe_fd, buffer, BUFFER_SIZE)) > 0) {
        printf("copy_pipe_to_tcp: read %ld bytes from pipe\n", n);
        if (write(tcp_fd, buffer, n) != n) {
            perror("write to tcp");
            break;
        }
        printf("copy_pipe_to_tcp: wrote %ld bytes to tcp\n", n);
    }

    if (n == -1) {
        perror("read from pipe");
    }
    printf("copy_pipe_to_tcp: finished copying data\n");
}

int main(int argc, char *argv[]) {
    int opt;
    char *command = NULL;
    char *input_param = NULL;
    char *output_param = NULL;

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
                break;
            default:
                fprintf(stderr, "Usage: %s -e \"command\" [-i input] [-o output] [-b both]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (!command) {
        fprintf(stderr, "Command is required\n");
        return EXIT_FAILURE;
    }

    int input_fd = -1, output_fd = -1;

    // Debugging the value of output_param
    printf("Initial output_param: %s\n", output_param);

    if (input_param && strncmp(input_param, "TCPS", 4) == 0) {
        int port = atoi(input_param + 4);
        printf("Starting TCP server on port %d\n", port);
        start_tcp_server(port, &input_fd);
        printf("TCP server started on port %d, client connected\n", port);
    }

    if (output_param) {
        printf("Checking output_param value: %s\n", output_param);
        if (strncmp(output_param, "TCPC", 4) == 0) {
            printf("Output parameter matches 'TCPC': %s\n", output_param);  // הודעת debug נוספת
            char *output_param_copy = strdup(output_param + 4);
            if (!output_param_copy) {
                fprintf(stderr, "Memory allocation failed\n");
                return EXIT_FAILURE;
            }

            printf("Output parameter copy: %s\n", output_param_copy); // הודעת debug נוספת

            char *hostname = strtok(output_param_copy, ",");
            char *port_str = strtok(NULL, ",");
            if (hostname == NULL || port_str == NULL) {
                fprintf(stderr, "Invalid TCPC parameters\n");
                free(output_param_copy);
                return EXIT_FAILURE;
            }

            printf("Parsed hostname: %s, port_str: %s\n", hostname, port_str); // הודעת debug נוספת

            int port = atoi(port_str);
            if (port == 0) {
                fprintf(stderr, "Invalid port\n");
                free(output_param_copy);
                return EXIT_FAILURE;
            }

            printf("Connecting to TCP client at %s:%d\n", hostname, port);
            start_tcp_client(hostname, port, &output_fd);
            printf("start_tcp_client called\n");  // הודעת debug נוספת
            free(output_param_copy);
            printf("Connected to TCP client at %s:%d, output_fd=%d\n", hostname, port, output_fd);
        } else {
            printf("Output parameter does not match 'TCPC': %s\n", output_param);  // הודעת debug נוספת
        }
    } else {
        printf("Output parameter is NULL\n");  // הודעת debug נוספת
    }

    printf("Output parameter after parsing: %s\n", output_param);  // הודעת debug נוספת

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
        printf("Child: process started\n");
        if (input_fd != -1) {
            printf("Child: redirecting stdin from TCP server\n");
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }

        // Redirect stdout
        close(pipe_out[0]);
        if (output_fd != -1) {
            printf("Child: redirecting stdout to TCP client\n");
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        } else {
            printf("Child: redirecting stdout to pipe\n");
            dup2(pipe_out[1], STDOUT_FILENO);
        }
        close(pipe_out[1]);

        // Execute the command
        printf("Child: executing command\n");
        execute_program(command);
        printf("Child: command execution finished\n"); // Should not reach here
    } else { // Parent process
        printf("Parent: process started\n");
        close(pipe_out[1]);

        if (output_fd != -1) {
            printf("Parent: output_fd is set to %d, preparing to copy data from pipe to tcp\n", output_fd);
            copy_pipe_to_tcp(pipe_out[0], output_fd);
            printf("Parent: finished copying data from pipe to tcp\n");
            close(output_fd);
            printf("Parent: closed output_fd\n");
        } else {
            printf("Parent: output_fd is not set, reading from pipe\n");
            char buffer[BUFFER_SIZE];
            ssize_t n;

            while ((n = read(pipe_out[0], buffer, BUFFER_SIZE - 1)) > 0) {
                buffer[n] = '\0';
                write(STDOUT_FILENO, buffer, n);
            }

            if (n == -1) {
                perror("read");
            }
        }

        close(pipe_out[0]);
        printf("Parent: closed pipe_out[0]\n");

        // Wait for child to finish
        printf("Parent: waiting for child to finish\n");
        wait(NULL);
        printf("Parent: child process finished\n");
    }

    return EXIT_SUCCESS;
}





// Example 1: Running `mync` with input from a TCP server and output to a TCP client

// ./mync -e “ttt 123456789” -i TCPS4090 -o TCPClocalhost,4455
// To run this, first open a new terminal and start a TCP server listening on port 4455:
// nc -l -p 4455
// After that, run the following command in the original terminal:
// ./mync -e “ttt 123456789” -i TCPS4090 -o TCPClocalhost,4455
// Finally, open another terminal and run:
// telnet localhost 4090

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

// ./mync -e "ttt 123456789" -i TCPS4092
// To run this, use the following command in the original terminal:
// ./mync -e "ttt 123456789" -i TCPS4092
// Then, open another terminal and run:
// telnet localhost 4092
