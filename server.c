#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 5555
#define BUF_SIZE 4096

int connected_clients = 0;
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

// receive exactly len bytes
int recv_all(int fd, void *buf, int len) {
    int got = 0;
    while (got < len) {
        int r = recv(fd, (char*)buf + got, len - got, 0);
        if (r == 0) return 0;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += r;
    }
    return 1;
}

// send exactly len bytes
int send_all(int fd, const void *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int s = send(fd, (char*)buf + sent, len - sent, 0);
        if (s < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += s;
    }
    return 1;
}

// function executed by each server thread
void *handle_client(void *arg) {
    int cfd = *(int*)arg;
    free(arg);

    // update clients counter (critical section)
    pthread_mutex_lock(&mtx);
    connected_clients++;
    printf("[server] client connected, now=%d\n", connected_clients);
    pthread_mutex_unlock(&mtx);

    char buffer[BUF_SIZE];

    while (1) {
        // receive message length
        uint32_t net_len;
        int r = recv_all(cfd, &net_len, sizeof(net_len));
        if (r == 0) break;
        if (r < 0) { perror("recv length"); break; }

        int len = (int)ntohl(net_len);
        if (len < 0 || len > BUF_SIZE) {
            printf("[server] bad length=%d\n", len);
            break;
        }

        // receive message data
        r = recv_all(cfd, buffer, len);
        if (r == 0) break;
        if (r < 0) { perror("recv payload"); break; }

        // convert to uppercase
        for (int i = 0; i < len; i++) {
            buffer[i] = (char)toupper((unsigned char)buffer[i]);
        }

        // send back length and data
        if (send_all(cfd, &net_len, sizeof(net_len)) < 0) { perror("send length"); break; }
        if (send_all(cfd, buffer, len) < 0) { perror("send payload"); break; }
    }

    close(cfd);

    // update clients counter (critical section)
    pthread_mutex_lock(&mtx);
    connected_clients--;
    printf("[server] client disconnected, now=%d\n", connected_clients);
    pthread_mutex_unlock(&mtx);

    return NULL;
}

int main() {
    // create listening socket
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // bind socket
    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sfd);
        return 1;
    }

    // start listening
    if (listen(sfd, 10) < 0) {
        perror("listen");
        close(sfd);
        return 1;
    }

    printf("[server] listening on 127.0.0.1:%d\n", PORT);

    while (1) {
        // accept new client
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        int *p = malloc(sizeof(int));
        if (!p) { close(cfd); continue; }
        *p = cfd;

        pthread_t t;
        if (pthread_create(&t, NULL, handle_client, p) != 0) {
            perror("pthread_create");
            close(cfd);
            free(p);
            continue;
        }

        pthread_detach(t);
    }

    close(sfd);
    return 0;
}
