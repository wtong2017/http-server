#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT (80) // port 80 for http
#define LISTENNQ (5)
#define MAXLINE (4096)
#define MAXTHREAD (5)

void* request_func(void *args);

int threads_count = 0;
int main(int argc, char **argv) {
    int listenfd, connfd;

    struct sockaddr_in servaddr, cliaddr;
    socklen_t len = sizeof(struct sockaddr_in);

    char ip_str[INET_ADDRSTRLEN] = {0};

    pthread_t threads[MAXTHREAD];

    /* initialize server socket */
    listenfd = socket(AF_INET, SOCK_STREAM, 0); /* SOCK_STREAM : TCP */
    if (listenfd < 0) {
        printf("Error: init socket\n");
        return 0;
    }

    /* initialize server address (IP:port) */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY; /* IP address: 0.0.0.0 */
    servaddr.sin_port = htons(SERVER_PORT); /* port number */

    /* bind the socket to the server address */
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(struct sockaddr)) < 0) {
        printf("Error: bind\n");
        return 0;
    }

    if (listen(listenfd, LISTENNQ) < 0) {
        printf("Error: listen\n");
        return 0;
    }

    /* keep processing incoming requests */
    while (1) {
        /* accept an incoming connection from the remote side */
        connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &len);
        if (connfd < 0) {
            printf("Error: accept\n");
            return 0;
        }

        /* print client (remote side) address (IP : port) */
        inet_ntop(AF_INET, &(cliaddr.sin_addr), ip_str, INET_ADDRSTRLEN);
        printf("Incoming connection from %s : %hu with fd: %d\n", ip_str, ntohs(cliaddr.sin_port), connfd);

        /* create dedicate thread to process the request */
        if (pthread_create(&threads[threads_count], NULL, request_func, (void *)connfd) != 0) {
            printf("Error when creating thread %d\n", threads_count);
            return 0;
        }

        if (++threads_count >= MAXTHREAD) {
            break;
        }
    }
    printf("Max thread number reached, wait for all threads to finish and exit...\n");
    for (int i = 0; i < MAXTHREAD; ++i) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}

void* request_func(void *args) {
    /* get the connection fd */
    int connfd = (int)args;
    char rcv_buff[MAXLINE] = {0};
    char wrt_buff[MAXLINE] = {0};
    char http_response[MAXLINE] = {0};
    int bytes_rcv, index, bytes_wrt, total_bytes_wrt;
    char *first_line, *http_action, *path, *p;
    FILE *file;

    /* read the response */
    bytes_rcv = 0;
    while (1) {
        bytes_rcv += recv(connfd, rcv_buff + bytes_rcv, sizeof(rcv_buff) - bytes_rcv - 1, 0);
        if (bytes_rcv && rcv_buff[bytes_rcv - 1] == '\n')
            break;
    }
    // printf("%s\n", rcv_buff);

    /* parse the request */
    p = rcv_buff;
    first_line = strsep(&p, "\r\n");
    // printf("%s\n", first_line);
    http_action = strsep(&first_line, " ");
    // printf("%s\n", http_action);
    if (strncmp(http_action, "GET\0", 4)) {
        printf("%s\n", "Not GET");
        return;
    }
    path = strsep(&first_line, " ");
    // printf("%s\n", path);
    if (strncmp(path, "/\0", 2) == 0)
        path = "/index.html";

    /* prepare for the send buffer */
    file = fopen(path+1, "r"); // use relative path: skip '/' in path
    if (!file) {
        snprintf(http_response, sizeof(http_response) - 1, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: %lu\r\nConnection: Keep-Alive\r\n\r\n<!DOCTYPE html><html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>The requested URL %s was not found on this server.</p></body></html>", 159 + strlen(path), path);
    } else {
        index = 0;
        // TODO: handle out of wrt_buff
        while ((wrt_buff[index] = fgetc(file)) != EOF && index < MAXLINE-1) {
            ++index;
        }
        wrt_buff[index] = '\0';
        fclose(file);
        // printf("%s\n", wrt_buff);
        // TODO: handle other type of documents e.g. pdf, jpg
        snprintf(http_response, sizeof(http_response) - 1, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %lu\r\nConnection: Keep-Alive\r\n\r\n%s", strlen(wrt_buff), wrt_buff);
        // printf("%s\n", http_response);
    }

    /* write the buffer to the connection */
    bytes_wrt = 0;
    total_bytes_wrt = strlen(http_response);
    while (bytes_wrt < total_bytes_wrt) {
        bytes_wrt += write(connfd, http_response + bytes_wrt, total_bytes_wrt - bytes_wrt);
    }

    close(connfd);
    threads_count--;
}