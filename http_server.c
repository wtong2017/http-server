#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>

#define SERVER_PORT (80) // port 80 for http
#define LISTENNQ (10)
#define MAXLINE (4096)
#define MAXTHREAD (10)
#define CHUNKSIZE (100)

struct map {
    char* ext;
    char* type;
};

/* forward declaration */
void* request_func(void *args);

int threads_count = 0;
struct map ext_to_type[] = {
    {"html", "text/html"},
    {"jpg", "image/jpeg"},
    {"pdf", "application/pdf"},
    {"css", "text/css"},
    {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {"png", "image/png"},
};

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
    int bytes_rcv, bytes_wrt, total_bytes_wrt, file_size, res, i;
    int is_compressed = 0;
    int chunked_transfer = 1;
    char *first_line, *http_action, *path, *p, *ext;
    unsigned char *file_buff;
    char tmp[MAXLINE] = {0};
    char content_type[100] = {0};
    char compressed_file_path[100] = {0};
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
    // get the first line of the request
    p = rcv_buff;
    first_line = strsep(&p, "\r\n");
    // printf("%s\n", first_line);
    // get the http action
    http_action = strsep(&first_line, " ");
    // printf("%s\n", http_action);
    if (strncmp(http_action, "GET\0", 4)) {
        printf("%s\n", "Not GET");
        return 0;
    }
    // get the path
    path = strsep(&first_line, " ");
    // printf("%s\n", path);
    if (!strncmp(path, "/\0", 2))
        path = "/index.html";
    // get the extension
    strncpy(tmp, path, sizeof(tmp));
    p = tmp;
    ext = strsep(&p, ".");
    ext = strsep(&p, "");
    // printf("%s\n", ext);
    for (int i = 0; i < sizeof(ext_to_type) / sizeof(ext_to_type[0]); ++i) {
        if (!strncmp(ext_to_type[i].ext, ext, strlen(ext))) {
            strncpy(content_type, ext_to_type[i].type, sizeof(content_type));
        }
    }

    if (!content_type) {
        printf("Do not support file with extension %s\n", ext);
        return 0;
    }

    /* prepare for the send buffer */
    // look for the compressed file
    strncpy(compressed_file_path, path + 1, sizeof(compressed_file_path));
    strcat(compressed_file_path, ".gz");
    file = fopen(compressed_file_path, "r");
    if (!file) {
        printf("%s is not found\n", compressed_file_path);
        // look for the file
        file = fopen(path + 1, "r"); // use relative path: skip '/' in path
    } else {
        is_compressed = 1;
    }
    if (!file) {
        printf("%s is not found\n", path + 1);
        snprintf(wrt_buff, sizeof(wrt_buff) - 1, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: %lu\r\nKeep-Alive: timeout=5, max=100\r\nConnection: Keep-Alive\r\n\r\n<!DOCTYPE html><html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>The requested URL %s was not found on this server.</p></body></html>", 159 + strlen(path), path); // hard code the length
        // printf("%s\n", wrt_buff);
        write(connfd, wrt_buff, strlen(wrt_buff));
    } else {
        // obtain the file size
        fseek(file , 0 , SEEK_END);
        file_size = ftell(file);
        rewind(file);
        // send the response header
        snprintf(wrt_buff, sizeof(wrt_buff) - 1, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nKeep-Alive: timeout=5, max=100\r\nConnection: Keep-Alive\r\n", content_type);
        write(connfd, wrt_buff, strlen(wrt_buff));
        if (chunked_transfer) {
            snprintf(wrt_buff, sizeof(wrt_buff) - 1, "Transfer-Encoding: chunked\r\n");
        } else {
            snprintf(wrt_buff, sizeof(wrt_buff) - 1, "Content-Length: %lu\r\n", sizeof(char)*(file_size));
        }
        write(connfd, wrt_buff, strlen(wrt_buff)); // hard code the lengthContent-Length: %lu\r\n
        if (is_compressed)
            write(connfd, "Content-Encoding: gzip\r\n", 24); // hard code the length
        write(connfd, "\r\n", 2); // hard code the length
        // handle the file reading
        if (chunked_transfer) {
            printf("%s\n", "Chunked transfer");
            file_buff = malloc(sizeof(unsigned char)*(CHUNKSIZE));
            while ((res = fread(file_buff, 1, CHUNKSIZE, file)) == CHUNKSIZE) {
                // send the content of the file in chunk
                snprintf(wrt_buff, sizeof(wrt_buff) - 1, "%04X\r\n", res);
                write(connfd, wrt_buff, strlen(wrt_buff));
                write(connfd, file_buff, res);
                write(connfd, "\r\n", 2); // hard code the length
            }
            if (res != 0) {
                snprintf(wrt_buff, sizeof(wrt_buff) - 1, "%04X\r\n", res);
                write(connfd, wrt_buff, strlen(wrt_buff));
                write(connfd, file_buff, res);
                write(connfd, "\r\n", 2); // hard code the length
            }
            write(connfd, "0\r\n", 3);
            write(connfd, "\r\n", 2); // hard code the length
            free(file_buff);
        }
        else { 
            file_buff = malloc(sizeof(unsigned char)*(file_size));
            res = fread(file_buff, 1, file_size, file);
            if (res != file_size) {
                printf("%s\n", "Reading error");
                return 0;
            }
            // send the content of the file
            // printf("%s\n", file_buff);
            i = 0;
            while (i < file_size) {
               write(connfd, file_buff + i, sizeof(unsigned char));
               i++;
            }
            free(file_buff);
        }
        fclose(file);
    }

    close(connfd);
    threads_count--;
}