#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/uds_datagram_server"
#define MESSAGE "Hello, Unix Domain Datagram Socket!"

int main() {
    int sockfd;
    struct sockaddr_un servaddr;

    // יצירת הסוקט
    if ((sockfd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("Socket created successfully\n");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, SOCKET_PATH);

    printf("Preparing to send to Unix domain socket\n");
    printf("servaddr.sun_path: %s\n", servaddr.sun_path);

    // שליחת הודעה לסוקט
    if (sendto(sockfd, MESSAGE, strlen(MESSAGE), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("sendto failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Message sent to %s: %s\n", SOCKET_PATH, MESSAGE);

    close(sockfd);
    return 0;
}
