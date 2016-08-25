#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <netdb.h>

#include "clogger/clogger.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"

static volatile int running = 1;

void int_handler(int dummy) {
    running = 0;
}

void print_usage() {
    printf("usage: dtcp host port\n");
}

void *send_thread(void *arg);

void *recv_thread(void *arg);

int make_connection(const char *host, const char *post);

int main(int argc, char *argv[]) {
    const char *tag = "main";

    if (argc != 3) {
        printf("unknown arguments\n");
        print_usage();
        exit(1);
    }

    log_i(tag, "Starting...");

    signal(SIGINT, int_handler);

    char *serv_host = argv[1];
    char *serv_port = argv[2];

    int sock = make_connection(serv_host, serv_port);
    if (sock < 0) {
        log_e(tag, "Failed to connect to server.");
        exit(1);
    }

    pthread_t sendthr;
    pthread_create(&sendthr, NULL, send_thread, &sock);
    pthread_t recvthr;
    pthread_create(&recvthr, NULL, recv_thread, &sock);

    while (running);

    log_i(tag, "Stopping...");
    pthread_cancel(sendthr);
    close(sock);

    return 0;
}

void *send_thread(void *arg) {
    char *tag = "send_thread";

    int sock = *((int *) arg);

    char *line = NULL;
    size_t line_len = 0;
    size_t str_len = 0;
    ssize_t sent_count = 0;
    for (;;) {
        pthread_testcancel();
        getline(&line, &line_len, stdin);
        str_len = strlen(line);
        sent_count = send(sock, line, str_len, 0);
        if (sent_count < 0 || sent_count != str_len) {
            log_e(tag, "Failed to send.");
        }
    }

    return NULL;
}

void *recv_thread(void *arg) {
    char *tag = "recv_thread";

    int sock = *((int *) arg);
    setvbuf(stdout, NULL, _IONBF, 0);

    char buffer[BUFSIZ];
    ssize_t recv_count = 0;

    for (;;) {
        recv_count = recv(sock, buffer, BUFSIZ - 1, 0);
        if (recv_count > 0) {
            buffer[recv_count] = '\0';
            fputs(buffer, stdout);
        }
    }

    return NULL;
}

int make_connection(const char *host, const char *post) {
    char *tag = "make_connection";

    log_i(tag, "Looking up host: %s...", host);
    struct addrinfo addr_criteria;
    memset(&addr_criteria, 0, sizeof(addr_criteria));
    addr_criteria.ai_family = AF_UNSPEC;
    addr_criteria.ai_socktype = SOCK_STREAM;
    addr_criteria.ai_protocol = IPPROTO_TCP;
    struct addrinfo *addr_list = NULL;
    if (getaddrinfo(host, post, &addr_criteria, &addr_list) || addr_list == NULL) {
        log_e(tag, "Failed to look up host.");
        exit(1);
    }
    log_i(tag, "Succeeded to look up host.");

    int sock = -1;

    char *addr_str = malloc(INET6_ADDRSTRLEN);
    for (struct addrinfo *addr_info = addr_list; addr_info != NULL; addr_info = addr_info->ai_next) {
        switch (addr_info->ai_addr->sa_family) {
            case AF_INET:
                inet_ntop(AF_INET, &((struct sockaddr_in *) addr_info->ai_addr)->sin_addr, addr_str, INET_ADDRSTRLEN);
                break;
            case AF_INET6:
                inet_ntop(AF_INET6, &((struct sockaddr_in6 *) addr_info->ai_addr)->sin6_addr, addr_str,
                          INET6_ADDRSTRLEN);
                break;
            default:
                break;
        }
        log_i(tag, "Trying %s...", addr_str);

        sock = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
        if (sock < 0) {
            log_e(tag, "Failed to obtain socket.");
            exit(1);
        }
        log_i(tag, "Obtained socket: %d.", sock);

        log_i(tag, "Connecting to server: %s:%s...", addr_str, post);
        if (connect(sock, addr_info->ai_addr, addr_info->ai_addrlen) == 0) {
            log_i(tag, "Connected.");
            break;
        }

        close(sock);
        sock = -1;
    }
    free(addr_str);

    freeaddrinfo(addr_list);
    return sock;
}

#pragma clang diagnostic pop
