// Add these includes if not already present
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>

#define ARRAY_SIZE 10

typedef struct {
    int id;
    const char* server_ip;
    int port;
} WriterData;

sem_t rand_sem;

void signal_handler(int signal) {
    printf("Terminating writer clients...\n");
    sem_destroy(&rand_sem);
    exit(0);
}

void* write_process(void* arg) {
    WriterData* args = (WriterData*)arg;
    int id = args->id;
    const char* server_ip = args->server_ip;
    int port = args->port;

    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Socket creation failed\n");
        return NULL;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address\n");
        close(sock);
        return NULL;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "connect() failed\n");
        close(sock);
        return NULL;
    }

    while (1) {
        sleep(1 + rand() % 5);

        sem_wait(&rand_sem);
        int index = rand() % ARRAY_SIZE;
        int new_value = rand() % 40;
        sem_post(&rand_sem);

        char request[1024];
        snprintf(request, sizeof(request), "READ %d", index);

        int request_len = strlen(request);
        if (send(sock, &request_len, sizeof(request_len), 0) != sizeof(request_len)) {
            fprintf(stderr, "Error sending request length\n");
            break;
        }
        if (send(sock, request, request_len, 0) != request_len) {
            fprintf(stderr, "Error sending request\n");
            break;
        }

        memset(buffer, 0, sizeof(buffer));
        int valread = read(sock, buffer, sizeof(buffer) - 1);
        if (valread > 0) {
            buffer[valread] = '\0';
            int old_value;
            sscanf(buffer, "VALUE %d", &old_value);

            snprintf(request, sizeof(request), "WRITE %d %d", index, new_value);

            request_len = strlen(request);
            if (send(sock, &request_len, sizeof(request_len), 0) != sizeof(request_len)) {
                fprintf(stderr, "Error sending request length\n");
                break;
            }
            if (send(sock, request, request_len, 0) != request_len) {
                fprintf(stderr, "Error sending request\n");
                break;
            }

            memset(buffer, 0, sizeof(buffer));
            valread = read(sock, buffer, sizeof(buffer) - 1);
            if (valread > 0) {
                buffer[valread] = '\0';
                if (strcmp(buffer, "UPDATED") == 0) {
                    printf("Writer[%d]: updated DB[%d] from %d to %d\n", id, index, old_value, new_value);
                }
            } else {
                fprintf(stderr, "Read error or server closed connection\n");
                break;
            }
        } else {
            fprintf(stderr, "Read error or server closed connection\n");
            break;
        }
    }

    close(sock);
    printf("Writer[%d] finished.\n", id);
    return NULL;
}

int main(int argc, char const *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <num_writers>\n", argv[0]);
        return -1;
    }

    const char* server_ip = argv[1];
    int port = atoi(argv[2]);
    int K = atoi(argv[3]);
    srand(time(NULL));
    signal(SIGINT, signal_handler);
    if (sem_init(&rand_sem, 0, 1) != 0) {
        fprintf(stderr, "Semaphore initialization failed\n");
        return -1;
    }

    pthread_t *writers = (pthread_t *)malloc(sizeof(pthread_t) * K);
    if (writers == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        sem_destroy(&rand_sem);
        return -1;
    }

    WriterData *writer_data = (WriterData *)malloc(sizeof(WriterData) * K);
    if (writer_data == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        free(writers);
        sem_destroy(&rand_sem);
        return -1;
    }

    for (int i = 0; i < K; ++i) {
        writer_data[i].id = i + 1;
        writer_data[i].server_ip = server_ip;
        writer_data[i].port = port;
        if (pthread_create(&writers[i], NULL, write_process, &writer_data[i]) != 0) {
            fprintf(stderr, "Error creating writer thread\n");
            free(writers);
            free(writer_data);
            sem_destroy(&rand_sem);
            return -1;
        }
    }

    for (int i = 0; i < K; ++i) {
        pthread_join(writers[i], NULL);
    }

    free(writers);
    free(writer_data);
    sem_destroy(&rand_sem);

    return 0;
}
