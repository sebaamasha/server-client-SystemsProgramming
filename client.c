#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 5555
#define THREADS 5
#define BUF_SIZE 4096

// receive exactly len bytes
int recv_all(int fd, void *buf, int len) {
    int got = 0;
    while (got < len) {
        int r = recv(fd, (char*)buf + got, len - got, 0);
        if (r == 0) return 0;          // connection closed
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

// arguments passed to each client thread
typedef struct {
    int id;
    const char *msg;
} args_t;

// function executed by each client thread
void *client_thread(void *arg) {
    args_t *a = (args_t*)arg;

    // create socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return NULL; }

    // server address
    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);

    // connect to server
    if (connect(fd, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("connect");
        close(fd);
        return NULL;
    }

    // prepare message
    int len = (int)strlen(a->msg);
    if (len > BUF_SIZE) len = BUF_SIZE;

    uint32_t net_len = htonl((uint32_t)len);

    // send length and message
    if (send_all(fd, &net_len, sizeof(net_len)) < 0) { perror("send len"); close(fd); return NULL; }
    if (send_all(fd, a->msg, len) < 0) { perror("send msg"); close(fd); return NULL; }

    // receive response length
    uint32_t resp_net_len;
    int r = recv_all(fd, &resp_net_len, sizeof(resp_net_len));
    if (r <= 0) { printf("[client %d] no response len\n", a->id); close(fd); return NULL; }

    int resp_len = (int)ntohl(resp_net_len);
    if (resp_len < 0 || resp_len > BUF_SIZE) { 
        printf("[client %d] bad resp len\n", a->id); 
        close(fd); 
        return NULL; 
    }

    // receive response data
    char buf[BUF_SIZE + 1];
    r = recv_all(fd, buf, resp_len);
    if (r <= 0) { printf("[client %d] no response data\n", a->id); close(fd); return NULL; }

    buf[resp_len] = '\0';
    printf("[client %d] got: %s\n", a->id, buf);

    close(fd);
    return NULL;
}

int main() {
    pthread_t t[THREADS];
    args_t args[THREADS];

    const char *msgs[THREADS] = {
        "hello",
        "thread test",
        "abcDef",
        "systems programming",
        "saba"
    };

    // create client threads
    for (int i = 0; i < THREADS; i++) {
        args[i].id = i;
        args[i].msg = msgs[i];
        if (pthread_create(&t[i], NULL, client_thread, &args[i]) != 0) {
            perror("pthread_create");
        }
    }

    // wait for all threads
    for (int i = 0; i < THREADS; i++) {
        pthread_join(t[i], NULL);
    }

    return 0;
}
