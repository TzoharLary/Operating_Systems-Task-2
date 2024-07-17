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
#include <stdbool.h>

#define BUFFER_SIZE 1024

struct io_params {
    int input_is_udp;
    int output_is_udp;
    int input_is_unix;
    int output_is_unix;
    int input_is_tcp;
    int output_is_tcp;
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
        printf("Trying to connect to %s (%s)\n", ipstr, ipver);

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
        printf("p is NULL\n");
        fprintf(stderr, "Failed to connect to %s:%d\n", hostname, port);
        exit(EXIT_FAILURE);
    }
    printf("Connected to %s:%d\n", hostname, port);
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

    // create a socket and connect to the server
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

void start_udssd_server(const char *path, int *sockfd, struct io_params *params) {
    struct sockaddr_un servaddr;
    printf("enter to start_udssd_server\n");

    umask(000);

    *sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (*sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("Socket created successfully\n");

    if (access(path, F_OK) != -1) {
        if (unlink(path) < 0) {
            perror("unlink failed");
            close(*sockfd);
            exit(EXIT_FAILURE);
        }
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, path);

    if (bind(*sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        close(*sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Socket bound successfully to %s\n", path);

    // הגדרת הכתובת ואורכה בתוך params
    params->output_is_unix = 1;
    memset(&params->dest_unix_addr, 0, sizeof(params->dest_unix_addr));
    params->dest_unix_addr.sun_family = AF_UNIX;
    strcpy(params->dest_unix_addr.sun_path, path);
    params->dest_unix_addr_len = sizeof(params->dest_unix_addr);
}

void start_udscd_client(const char *path, int *sockfd, struct io_params *params) {
    printf("enter to start_udscd_client\n");
    struct sockaddr_un *addr = &(params->dest_unix_addr);
    *sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (*sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("Client Socket created successfully, sockfd: %d\n", *sockfd);
    
    int sock_type;
    socklen_t optlen = sizeof(sock_type);
    if (getsockopt(*sockfd, SOL_SOCKET, SO_TYPE, &sock_type, &optlen) == -1) {
        perror("getsockopt");
        close(*sockfd);
        exit(EXIT_FAILURE);
    }

    if (sock_type != SOCK_DGRAM) {
        fprintf(stderr, "Socket type is not SOCK_DGRAM\n");
        close(*sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Socket type is SOCK_DGRAM\n");

    params->output_is_unix = 1;
    memset((addr), 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    printf("path: %s\n", path);
    strncpy(addr->sun_path, path, sizeof(addr->sun_path) - 1);
    params->dest_unix_addr_len = sizeof(*addr);
    addr->sun_path[sizeof(addr->sun_path) - 1] = '\0'; // Ensure null-terminated string

    printf("UNIX Domain Datagram client started on path %s\n", path);

    // check if the socket file exists
    if (access(path, F_OK) == -1) {
        printf("Warning: Socket file %s does not exist\n", path);
    }
    // check if the client has read/write access to the socket file
    if (access(path, R_OK | W_OK) == -1) {
        perror("Client does not have read/write access to the socket file");
        exit(EXIT_FAILURE);
    } else {
        printf("Client has read/write access to the socket file %s\n", path);
    }


    // Send an initial message to create the socket file
    const char *init_msg = "first try message for socket creation\n";
    ssize_t sent = sendto(*sockfd, init_msg, strlen(init_msg), 0, (struct sockaddr *)&params->dest_unix_addr, params->dest_unix_addr_len);
    if (sent == -1) {
        perror("Initial sendto failed");
        exit(EXIT_FAILURE);
    } else {
        printf("we in the start_udscd_client function before handle_io and before handle_unix_io\n");
        printf("Sent %zd bytes to %s\n", sent, path);
    }
    printf("end of start_udscd_client\n");
}

void start_udsss_server(const char *path, int *connfd) {
    int sockfd;
    struct sockaddr_un servaddr;
    printf("enter to start_udsss_server\n");
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, path);

    if (bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 5) < 0) {
        perror("listen failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("UNIX Domain Stream server started on path %s\n", path);

    *connfd = accept(sockfd, NULL, NULL);
    if (*connfd < 0) {
        perror("accept failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    close(sockfd);
}

void start_udscs_client(const char *path, int *sockfd) {
    struct sockaddr_un servaddr;
    printf("enter to start_udscc_client\n");
    *sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (*sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, path);

    if (connect(*sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect failed");
        close(*sockfd);
        exit(EXIT_FAILURE);
    }

    printf("UNIX Domain Stream client connected to path %s\n", path);
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

void handle_unix_io(int pipe_fd, int input_fd, int output_fd, struct io_params *params) {
    printf("enter to handle_unix_io\n");
    printf("pipe_fd: %d\n input_fd: %d\n output_fd: %d\n", pipe_fd, input_fd, output_fd);
    char buffer[BUFFER_SIZE];
    struct pollfd fds[2];
    int nfds = 2;
    
    
    // ברגע שהגדרנו לו שהצינור יהיה הפלט, אז כל מה שנכנס לפלט נכנס לצינור בעצם
    // fds[0] for pipe - case when the child process writes to the pipe 
    // that's mean the output is stdout
    fds[0].fd = pipe_fd;
    fds[0].events = POLLIN;


    // זה מזהה הקובץ של הקלט
    // fds[1] for input_fd 
    fds[1].fd = input_fd;
    fds[1].events = POLLIN;
   

    while (1) {
        
        int ret = poll(fds, nfds, -1);

        if (ret == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        // Handle pipe input (output from child process)
        // אנחנו נכנסים לפה, כאשר יש פלט באמת מהתוכנית, והוא נכנס אוטומטית לצינור, ואנחנו רוצים להפנות אותו ללקוח שיפנה לשרת
        // לכן כל מה שקורה פה זה לקחת את הפלט של התוכנית ולהפנות לצינור שיפנה ללקוח
        if (fds[0].revents & POLLIN) {
            printf("enter to fds[0].revents & POLLIN\n");
            ssize_t n = read(fds[0].fd, buffer, BUFFER_SIZE);
            printf("buffer: %s\n", buffer);
            if (n <= 0) {
                printf("enter to if n <= 0\n");
                if (n < 0) perror("read");
                return;
            }
            buffer[n] = '\0';
            printf("Read from pipe: %s\n", buffer);

            // Send to output_fd (could be a Unix domain socket or STDOUT)
            if (params->output_is_unix) {
                printf("Preparing to send to Unix domain socket\n");
                printf("output_fd: %d\n", output_fd);
                printf("buffer: %s\n", buffer);
                printf("length: %zd\n", n);
                printf("dest_unix_addr.sun_path: %s\n", params->dest_unix_addr.sun_path);
                printf("dest_unix_addr_len: %d\n", params->dest_unix_addr_len);

                // בדיקה אם קובץ הסוקט קיים
                if (access(params->dest_unix_addr.sun_path, F_OK) == -1) {
                    printf("Warning: Socket file %s does not exist\n", params->dest_unix_addr.sun_path);
                }     
                printf("we in the handle_unix_io function before sendto\n");

                ssize_t sent = sendto(output_fd, buffer, n, 0, (struct sockaddr *)&params->dest_unix_addr, params->dest_unix_addr_len);
                if (sent == -1) {
                    perror("the sendto of handle_unix_io failed");
                    perror("sent == -1");
                    perror("sendto failed");
                } else {
                    printf("Successfully sent %zd bytes\n", sent);
                }
                printf("Sent %zd bytes to %s\n", sent, params->dest_unix_addr.sun_path);
                // printf("Data sent: %s\n", buffer);
            } 
            // if the output is UDP
            if (params->output_is_udp) {
                ssize_t sent = sendto(output_fd, buffer, n, 0, (struct sockaddr *)&params->dest_addr, params->dest_addr_len);
                if (sent == -1) {
                    perror("sendto");
                    return;
                }
                printf("Sent %zd bytes to %s:%d\n", sent, inet_ntoa(params->dest_addr.sin_addr), ntohs(params->dest_addr.sin_port));
            }
            // if the output is stdout or tcp
            else if (output_fd == STDOUT_FILENO || params->output_is_tcp) {
                printf("enter to else if output_fd == STDOUT_FILENO || params->output_is_tcp\n");
                write(STDOUT_FILENO, buffer, n);
            }
        }

        // Handle socket input (data from Unix domain socket)
        // אנחנו נכנסים לפה כאשר יש קלט והוא לא קלט סטנדרטי, ואנחנו רוצים לקרוא את הקלט ולהפנות אותו לתוכנית
        if (fds[1].revents & POLLIN && input_fd != STDIN_FILENO) {
            
            printf("enter to fds[1].revents & POLLIN\n");
            printf("input_fd: %d\n", input_fd);
            printf("in the fds[1] the buffer is: %s\n", buffer);
            // הפונקציה לוקחת מה שרשום בקלט, ומעתיקה את זה לתוך הבאפר
            // recvfrom is a function that receives a message from a socket and stores it in a buffer
            // in this case, we are reading from the input_fd and storing the message in the buffer
            ssize_t n = recvfrom(fds[1].fd, buffer, BUFFER_SIZE, 0, NULL, NULL);
            if (n == -1) {
                perror("recvfrom");
                return;
            }
            buffer[n] = '\0';
            printf("Read from socket: %s\n", buffer);

            // Send to output_fd (could be a pipe or another socket)
            if (params->output_is_unix) {
                printf("enter to if params->output_is_unix\n");
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
    printf("enter to handle_unix_process\n");
    printf("input_fd: %d\n output_fd1: %d\n command: %s\n", input_fd, output_fd, command);
    printf("params->input_is_unix: %d\n params->output_is_unix: %d\n", params->input_is_unix, params->output_is_unix);
    // create a int array to store the file descriptors for the pipe
    int pipe_out[2];

    // create a pipe with the pipe_out array
    if (pipe(pipe_out) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
    // create a child process
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    // if pid is 0, then it is the child process
    if (pid == 0) {
        // print a message to the console that the child process is entered
        printf("enter to child process\n");
        // if the input_fd is not stdin, then duplicate the input_fd to stdin
        if (input_fd != STDIN_FILENO) {
            // print a message to the console that the input_fd is not stdin
            printf("input_fd is not stdin\n");
            // duplicate the input_fd to stdin that's mean the child process will read from the input_fd
            // instead of the stdin
            dup2(input_fd, STDIN_FILENO);
            // print a message to the console that the input_fd is duplicated to stdin
            // that's mean the child process will read from the input_fd
            printf("input_fd2: %d\n", input_fd);
        }

        printf("child process before close pipe_out[0]\n");
        // close the read end of the pipe
        close(pipe_out[0]);
        printf("child process after close pipe_out[0]\n");
         
          if (output_fd != STDOUT_FILENO) {
            // fcntl is a system call that can perform various operations on file descriptors
            if (fcntl(output_fd, F_GETFD) == -1) {
                perror("fcntl (output_fd)");
                exit(EXIT_FAILURE);
            }
            printf("output_fd is not stdout, output_fd2: %d\n", output_fd);
            dup2(pipe_out[1], STDOUT_FILENO);
        } else {
            dup2(pipe_out[1], STDOUT_FILENO);
        } 
        // printf("before close pipe_out[1]\n");
        // close the write end of the pipe after the child process writes to it
        // we do not need it anymore because we will not write to it again
        close(pipe_out[1]);
        // printf("after close pipe_out[1]\n");
        fprintf(stderr, "Debug: using fprintf and stderr before execute_program\n"); // Debug print
        // execute the command that the user entered
        execute_program(command);
    }
    // Parent process
    else {
        close(pipe_out[1]);
        handle_unix_io(pipe_out[0], input_fd, output_fd, params);
        close(pipe_out[0]);
        wait(NULL);

        // TRY 1:

        // close the write end of the pipe because the parent process will not write to it
        // close(pipe_out[1]);
        // if the output_fd is not stdout or the input_fd is not stdin or the input_fd is a unix domain socket
        // we do it because if the input_fd is a unix domain socket, then we need to read from it and write to the output_fd
        

        // if (output_fd != STDOUT_FILENO || params->input_is_unix || params->output_is_unix) {
        //     printf("enter to first if at parent process\n");
        //     // we call the handle_unix_io for the parent process to read from the pipe and write to the output_fd
        //     handle_unix_io(pipe_out[0], input_fd, output_fd, params);
        //     printf("after handle_unix_io\n");
        // } else {
        //     // if the output_fd is stdout and the input_fd is stdin, then we do not need to read from the pipe
        //     printf("enter to else at parent process\n");
        //     char buffer[BUFFER_SIZE];
        //     ssize_t n;
        //     while ((n = read(pipe_out[0], buffer, BUFFER_SIZE - 1)) > 0) {
        //         buffer[n] = '\0';
        //         printf("Read from child process: %s\n", buffer);
        //         write(STDOUT_FILENO, buffer, n);
        //     }
        // }
        
        
        // printf("parent process before close pipe_out[0]\n");
        // close the read end of the pipe because the parent process will not read from it
        close(pipe_out[0]);
        // wait for the child process to finish
        wait(NULL);
    }
}

void handle_io(int pipe_fd, int socket_fd, int output_fd, struct io_params *params) {
    if (params->input_is_unix || params->output_is_unix) {
        handle_unix_io(pipe_fd, socket_fd, output_fd, params);
        return;
    } 

    printf("enter to handle_io\n");
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


    printf("enter to handle_process\n");
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
        // According to the input instructions, there is no ability to output input if we are a uds customer
        if (strncmp(input_param, "TCPS", 4) == 0) {
            int port = atoi(input_param + 4);
            start_tcp_server(port, &input_fd);
            params.input_is_tcp = 1;
            if (is_bidirectional) {
                output_fd = input_fd;
            }
        } else if (strncmp(input_param, "UDPS", 4) == 0) {
            int port = atoi(input_param + 4);
            input_fd = start_udp_server(port);
            params.input_is_udp = 1;
        } else if (strncmp(input_param, "UDSSD", 5) == 0) {
            start_udssd_server(output_param + 5, &output_fd, &params);   
            params.input_is_unix = 1;
        } else if (strncmp(input_param, "UDSSS", 5) == 0) {
            start_udsss_server(input_param + 5, &input_fd);
            params.input_is_unix = 1;
        }
        
    }

    if (output_param && !is_bidirectional) {
        // According to the output instructions, it is not possible to output if we are a uds server
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
            params.output_is_tcp = 1;
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
            start_udscd_client(output_param + 5, &output_fd, &params);  
        } else if (strncmp(output_param, "UDSCS", 5) == 0) {
            start_udscs_client(output_param + 5, &output_fd);
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

//socat - UNIX-RECVFROM:/tmp/uds_datagram_server,fork
 // Example usage:

    // Example 1: Run a command with input from a UDP server to the standard output (STDOUT)
    // run the following command in the terminal
    //  ./mync -e "./ttt 123456789" -i UDPS4050
    // open another terminal and run the following command
    // ncat -u localhost 4050

    // Example 2: Run a command with input from a UDP server and kill the process after 10 seconds
    // run the following command in the terminal
    // ./mync -e "./ttt 123456789" -i UDPS4050 -t 10
    // open another terminal and run the following command
    // ncat -u localhost 4050

    /* Example 3: Run a command with input from a UDP server and output to a TCP client

    // run the following command in the terminal for listening to the TCP client at port 4455
    // ncat -l 4455 
    // open another terminal and run the following command for running the mync program 
    // with the ttt command and input from the UDP server at port 4050 and output to the TCP client at port 4455
    // ./mync -e "./ttt 123456789" -i UDPS4050 -o TCPClocalhost,4455
    // open another terminal and run the following command for sending data to the UDP server at port 4050
    // ncat -u localhost 4050
    */
    
    /* Example 4: Run a command with input from a UDS server and output to a terminal

    run the following command in the terminal for 
    
    open another terminal and run the following command for running the mync program
    with the ttt command and input from the UDS server at path /tmp/uds_datagram_server and output to the terminal
    ./mync -e "./ttt 123456789" -i UDSSD/tmp/uds_datagram_server
    open another terminal and run the following command for input to the UDS server at path /tmp/uds_datagram_server
    ncat -Uu /tmp/uds_datagram_server
    socat - UNIX-RECVFROM:/tmp/uds_datagram_server,fork
    */
   
    /*     Example 5: Run a command with output to a UDP 

   
   */
   
    // Example usage of the `mync` program with Unix Domain Sockets (UDS):

    /* Example 1: Running `mync` with input from a UDS Datagram server and output to a UDS Datagram client

    # Step 1: Open a terminal and run the following command to start a UDS Datagram server
    ncat -lU /tmp/uds_datagram_server

    # Step 2: Open another terminal and run the following command to run the `mync` program
    # with the `ttt` command, input from the UDS Datagram server, and output to a UDS Datagram client
    ./mync -e "./ttt 123456789" -i UDSSD/tmp/uds_datagram_server -o UDSCD/tmp/uds_datagram_client

    # Step 3: Open a third terminal and run the following command to send data to the UDS Datagram client
    ncat -U /tmp/uds_datagram_client --udp
    */

    /* Example 2: Running `mync` with input from a UDS Stream server and output to a UDS Stream client

    # Step 1: Open a terminal and run the following command to start a UDS Stream server
    ncat -lU /tmp/uds_stream_server

    # Step 2: Open another terminal and run the following command to run the `mync` program
    # with the `ttt` command, input from the UDS Stream server, and output to a UDS Stream client
    ./mync -e "./ttt 123456789" -i UDSSS/tmp/uds_stream_server -o UDSCS/tmp/uds_stream_client

    # Step 3: Open a third terminal and run the following command to send data to the UDS Stream client
    ncat -U /tmp/uds_stream_client

    */

    /* Example 3: Run a command with bidirectional communication using UDS Stream


    ncat -lU /tmp/uds_bidirectional_server

    # Step 2: Open another terminal and run the following command to run the `mync` program
    # with the `ttt` command, input from the UDS Stream server, and output to the same UDS Stream server
    ./mync -e "./ttt 123456789" -b UDSCS/tmp/uds_bidirectional_server

    # Step 3: Open a third terminal and run the following command to send data to the UDS Stream server
    ncat -U /tmp/uds_bidirectional_server

    */

   /* Example 4: Running `mync` with input from a terminal and output to a UDS Datagram server

    # Step 1: Open a terminal and run the following command to start a UDS Datagram server
    ncat -lU /tmp/uds_datagram_server 

    # Step 2: Open another terminal and run the following command to run the `mync` program
    # with the `ttt` command, and output to the UDS Datagram server
    ./mync -e "./ttt 123456789" -o UDSCD/tmp/uds_datagram_server

    */


   // Example usage of the `mync` program with Unix Domain Sockets (UDS) and socat:

    /* Example 1: Running `mync` with input from a UDS Datagram server and output to a UDS Datagram client

    # Step 1: Open a terminal and run the following command to start a UDS Datagram server
    socat -u UNIX-RECV:/tmp/uds_datagram_server STDOUT

    # Step 2: Open another terminal and run the following command to run the `mync` program
    # with the `ttt` command, input from the UDS Datagram server, and output to a UDS Datagram client
    ./mync -e "./ttt 123456789" -i UDSSD/tmp/uds_datagram_server -o UDSCD/tmp/uds_datagram_client

    # Step 3: Open a third terminal and run the following command to send data to the UDS Datagram client
    socat STDIN UNIX-SENDTO:/tmp/uds_datagram_client

    */

   // socat -v UNIX-RECVFROM:/tmp/uds_datagram_server,mode=777 -
