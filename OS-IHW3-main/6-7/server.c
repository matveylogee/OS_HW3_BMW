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
#define MAX_CLIENTS 5

int db[ARRAY_SIZE];
sem_t db_sem;
sem_t writer_sem;
int server_fd;
int monitor_clients[MAX_CLIENTS] = {-1};
sem_t observer_sem;

void init_db() {
    for (int i = 1; i < ARRAY_SIZE + 1; ++i) {
        db[i - 1] = i;
    }
}

void notify_observers(const char *message) {
    sem_wait(&observer_sem);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (monitor_clients[i] != -1) {
            send(monitor_clients[i], message, strlen(message), 0);
        }
    }
    sem_post(&observer_sem);
}

void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[1024] = {0};
    while (1) {
        int msg_len;
        int len_read = read(client_socket, &msg_len, sizeof(msg_len));
        if (len_read <= 0) {
            break;
        }

        if (msg_len > 0 && msg_len < sizeof(buffer)) {
            int result = 0;
            while (result < msg_len) {
                int part = read(client_socket, buffer + result, msg_len - result);
                if (part <= 0) {
                    break;
                }
                result += part;
            }
            buffer[msg_len] = '\0';

            char *request = strdup(buffer);
            if (strncmp(request, "READ", 4) == 0) {
                int index = atoi(request + 5);
                int value;

                sem_wait(&db_sem);
                value = db[index];
                sem_post(&db_sem);

                char response[1024];
                snprintf(response, sizeof(response), "VALUE %d", value);
                send(client_socket, response, strlen(response), 0);

                snprintf(response, sizeof(response), "read value %d from index  %d", value, index);
                notify_observers(response);
            } else if (strncmp(request, "WRITE", 5) == 0) {
                int index, new_value;
                sscanf(request + 6, "%d %d", &index, &new_value);

                sem_wait(&writer_sem);
                sem_wait(&db_sem);
                int old_value = db[index];
                db[index] = new_value;
                sem_post(&db_sem);
                sem_post(&writer_sem);

                char response[1024];
                snprintf(response, sizeof(response), "UPDATED FROM %d TO %d", old_value, new_value);
                send(client_socket, response, strlen(response), 0);

                char response_log[1024];
                snprintf(response_log, sizeof(response_log), "DB[%d] updated to %d (old value %d)", index, new_value, old_value);
                notify_observers(response_log);
            }
            free(request);
        } else {
            fprintf(stderr, "Invalid message length\n");
            break;
        }
    }
    close(client_socket);
    printf("Client disconnected.\n");
    char notification[1024];
    snprintf(notification, sizeof(notification), "Client disconnected");
    notify_observers(notification);

    return NULL;
}

void signal_handler(int signal) {
    printf("Caught signal %d, terminating server...\n", signal);
    close(server_fd);
    sem_destroy(&db_sem);
    sem_destroy(&writer_sem);
    sem_destroy(&observer_sem);
    exit(0);
}

int main(int argc, char const *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP> <PORT>\n", argv[0]);
        return -1;
    }

    const char *SERVER_IP = argv[1];
    int PORT = atoi(argv[2]);

    init_db();

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
    address.sin_addr.s_addr = inet_addr(SERVER_IP);
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind() failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen() failed");
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, signal_handler);

    printf("Server listening on <ip:port> %s:%d\n", SERVER_IP, PORT);

    if (sem_init(&db_sem, 0, 1) != 0) {
        perror("sem_init() db failed");
        exit(EXIT_FAILURE);
    }

    if (sem_init(&writer_sem, 0, 1) != 0) {
        perror("sem_init() writer failed");
        sem_destroy(&db_sem);
        exit(EXIT_FAILURE);
    }

    if (sem_init(&observer_sem, 0, 1) != 0) {
        perror("sem_init() observer failed");
        sem_destroy(&db_sem);
        sem_destroy(&writer_sem);
        exit(EXIT_FAILURE);
    }

    while (1) {
        int *client_socket_ptr = (int *)malloc(sizeof(int));
        if (!client_socket_ptr) {
            perror("malloc failed");
            sem_destroy(&db_sem);
            sem_destroy(&writer_sem);
            sem_destroy(&observer_sem);
            exit(EXIT_FAILURE);
        }

        if ((*client_socket_ptr = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("accept() failed");
            free(client_socket_ptr);
            sem_destroy(&db_sem);
            sem_destroy(&writer_sem);
            sem_destroy(&observer_sem);
            exit(EXIT_FAILURE);
        }
        printf("Client connected successfully.\n");
        char notification[1024];
        snprintf(notification, sizeof(notification), "Client connected");
        notify_observers(notification);

        char handshake_message[50];
        int bytes_received = recv(*client_socket_ptr, handshake_message, sizeof(handshake_message), 0);
        if (bytes_received > 0) {
            handshake_message[bytes_received] = '\0';
            if (strcmp(handshake_message, "OBSERVER") == 0) {
                sem_wait(&observer_sem);
                for (int i = 0; i < MAX_CLIENTS; ++i) {
                    if (monitor_clients[i] == -1) {
                        monitor_clients[i] = *client_socket_ptr;
                        break;
                    }
                }
                sem_post(&observer_sem);
            } else {
                pthread_t client_thread;
                if (pthread_create(&client_thread, NULL, handle_client, client_socket_ptr) != 0) {
                    perror("pthread_create failed");
                    free(client_socket_ptr);
                    sem_destroy(&db_sem);
                    sem_destroy(&writer_sem);
                    sem_destroy(&observer_sem);
                    exit(EXIT_FAILURE);
                }
                pthread_detach(client_thread);
            }
        } else {
            perror("Error receiving handshake message");
            close(*client_socket_ptr);
            free(client_socket_ptr);
        }
    }

    close(server_fd);
    sem_destroy(&db_sem);
    sem_destroy(&writer_sem);
    sem_destroy(&observer_sem);
    return 0;
}
