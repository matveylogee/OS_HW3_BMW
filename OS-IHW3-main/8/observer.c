#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#define BUFF_SIZE 1024

int client_socket;

void signal_handler(int signal) {
    printf("Terminating observer...\n");
    close(client_socket);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip-address> <port>\n", argv[0]);
        return -1;
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        return -1;
    }
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0) {
        perror("Invalid address");
        close(client_socket);
        return -1;
    }
    if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("Connection failed");
        close(client_socket);
        return -1;
    }
    printf("Connected to server.\n");
    const char *handshake_message = "OBSERVER";
    if (send(client_socket, handshake_message, strlen(handshake_message), 0) == -1) {
        perror("Handshake message send failed");
        close(client_socket);
        return -1;
    }
    signal(SIGINT, signal_handler);

    char buffer[BUFF_SIZE];
    int bytes_received;
    while ((bytes_received = recv(client_socket, buffer, BUFF_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("From server: %s\n", buffer);
    }
    if (bytes_received == -1) {
        perror("Receive failed");
    }

    close(client_socket);
    printf("Connection closed.\n");

    return 0;
}
