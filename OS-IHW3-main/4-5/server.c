#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>

#define ARRAY_SIZE 10

int database[ARRAY_SIZE];
sem_t db_sem;
sem_t writer_sem;
int server_fd;

void init_db() {
    for (int i = 1; i < ARRAY_SIZE + 1; ++i) {
        database[i - 1] = i;
    }
}

void *handle_client(void *arg) {
    int client_socket = *(int *) arg;
    free(arg);

    char buffer[1024] = {0};
    while (1) {
        int msg_len;
        int len_read = read(client_socket, &msg_len, sizeof(msg_len));
        if (len_read <= 0) {
            break;
        }

        if (msg_len > 0 && msg_len < sizeof(buffer)) {
            int total_read = 0;
            while (total_read < msg_len) {
                int valread = read(client_socket, buffer + total_read, msg_len - total_read);
                if (valread <= 0) {
                    break;
                }
                total_read += valread;
            }
            buffer[msg_len] = '\0';

            char *request = strdup(buffer);
            if (strncmp(request, "READ", 4) == 0) {
                int index = atoi(request + 5);
                int value;

                sem_wait(&db_sem);
                value = database[index];
                sem_post(&db_sem);

                char response[1024];
                snprintf(response, sizeof(response), "VALUE %d", value);
                send(client_socket, response, strlen(response), 0);
            } else if (strncmp(request, "WRITE", 5) == 0) {
                int index, new_value;
                sscanf(request + 6, "%d %d", &index, &new_value);

                sem_wait(&writer_sem);
                sem_wait(&db_sem);
                database[index] = new_value;
                sem_post(&db_sem);
                sem_post(&writer_sem);

                char *response = "UPDATED";
                send(client_socket, response, strlen(response), 0);
            }
            free(request);
        } else {
            fprintf(stderr, "Invalid message length\n");
            break;
        }
    }
    close(client_socket);
    printf("Client disconnected.\n");
    return NULL;
}

void signal_handler(int signal) {
    printf("Caught signal %d, terminating server...\n", signal);
    close(server_fd);
    exit(0);
}

int main(int argc, char const *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip-address> <port>\n", argv[0]);
        return -1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    init_db();

    sem_init(&db_sem, 0, 1);
    sem_init(&writer_sem, 0, 1);

    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(server_ip);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        perror("bind() failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        perror("listen() failed");
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, signal_handler);

    printf("Server listening on <ip:port> %s:%d\n", server_ip, port);

    while (1) {
        int *client_socket_ptr = (int *) malloc(sizeof(int));
        if (!client_socket_ptr) {
            perror("malloc failed");
            exit(EXIT_FAILURE);
        }
        if ((*client_socket_ptr = accept(server_fd, (struct sockaddr *) &address, (socklen_t * ) & addrlen)) < 0) {
            perror("accept() failed");
            free(client_socket_ptr);
            exit(EXIT_FAILURE);
        }
        printf("Client connected successfully.\n");
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, client_socket_ptr) != 0) {
            perror("pthread_create failed");
            free(client_socket_ptr);
            exit(EXIT_FAILURE);
        }
        pthread_detach(client_thread);
    }

    close(server_fd);
    return 0;
}
