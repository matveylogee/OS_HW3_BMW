#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#define ARRAY_SIZE 10

typedef struct {
    int id;
    const char* server_ip;
    int port;
} ReaderData;

sem_t rand_sem;

int fib(int n) {
    if (n == 0) return 0;
    if (n == 1) return 1;

    int prev = 0;
    int curr = 1;
    int next;

    for (int i = 2; i <= n; i++) {
        next = prev + curr;
        prev = curr;
        curr = next;
    }

    return curr;
}

void signal_handler(int signal) {
    printf("Caught signal %d, terminating reader clients...\n", signal);
    sem_destroy(&rand_sem);
    exit(0);
}

void *reader_task(void *arg) {
    ReaderData *reader_data = (ReaderData *)arg;
    int id = reader_data->id;
    const char* server_ip = reader_data->server_ip;
    int port = reader_data->port;

    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Socket creation error\n");
        return NULL;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address / Address not supported\n");
        close(sock);
        return NULL;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "Connection failed\n");
        close(sock);
        return NULL;
    }

    const char *handshake_message = "READER";
    if (send(sock, handshake_message, strlen(handshake_message), 0) == -1) {
        perror("Handshake message send failed");
        close(sock);
        return NULL;
    }

    while (1) {
        int sleep_time = 1000 + rand() % 5000;
        usleep(sleep_time * 1000);

        sem_wait(&rand_sem);
        int index = rand() % ARRAY_SIZE;
        sem_post(&rand_sem);

        char request[1024];
        sprintf(request, "READ %d", index);

        int msg_len = strlen(request);
        if (send(sock, &msg_len, sizeof(msg_len), 0) != sizeof(msg_len)) {
            fprintf(stderr, "Reader %d failed to send message length\n", id);
            break;
        }

        if (send(sock, request, msg_len, 0) != msg_len) {
            fprintf(stderr, "Reader %d failed to send message\n", id);
            break;
        }

        memset(buffer, 0, sizeof(buffer));
        int bytes_received = read(sock, buffer, sizeof(buffer) - 1);

        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            if (strstr(buffer, "VALUE") == buffer) {
                int value = atoi(buffer + 6);
                int fib_value = fib(value);
                printf("Reader[%d]: DB[%d] = %d, Fibonacci = %d\n", id, index, value, fib_value);
            } else {
                printf("Reader %d received: %s\n", id, buffer);
            }
        } else {
            fprintf(stderr, "Reader %d read error\n", id);
            break;
        }
    }

    close(sock);
    printf("Reader %d finished.\n", id);
    return NULL;
}

int main(int argc, char const *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <num_readers>\n", argv[0]);
        return -1;
    }

    const char* server_ip = argv[1];
    int port = atoi(argv[2]);
    int N = atoi(argv[3]);

    srand(time(NULL));

    signal(SIGINT, signal_handler);

    if (sem_init(&rand_sem, 0, 1) != 0) {
        perror("Semaphore initialization failed");
        return -1;
    }

    pthread_t readers[N];
    ReaderData *reader_data = malloc(sizeof(ReaderData) * N);
    if (reader_data == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        sem_destroy(&rand_sem);
        return -1;
    }

    for (int i = 0; i < N; ++i) {
        reader_data[i].id = i + 1;
        reader_data[i].server_ip = server_ip;
        reader_data[i].port = port;
        if (pthread_create(&readers[i], NULL, reader_task, &reader_data[i]) != 0) {
            fprintf(stderr, "Error creating reader thread\n");
            free(reader_data);
            sem_destroy(&rand_sem);
            return -1;
        }
    }

    for (int i = 0; i < N; ++i) {
        pthread_join(readers[i], NULL);
    }

    free(reader_data);
    sem_destroy(&rand_sem);

    return 0;
}
