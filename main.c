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
#pragma ide diagnostic ignored "missing_default_case"
#pragma clang diagnostic ignored "-Wmissing-noreturn"

#define MODE_CLIENT 0
#define MODE_SERVER 1

static volatile int running = 1;

void int_handler(int dummy) {
    running = 0;
}

void print_usage() {
    printf("usage: dtcp [-c|-s] [-h host] [-p port]\n");
}

void *send_thread(void *arg);

void *recv_thread(void *arg);

int make_connection(const char *host, const char *port);

int listen_for_client(const char *host, const char *port);

int main(int argc, char *argv[]) {
    const char *tag = "main";

    int mode = MODE_CLIENT;
    char *host = "127.0.0.1";
    char *port = "2333";

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    } else if (argc > 6) {
        printf("too many arguments\n");
        print_usage();
        exit(1);
    }

    for (int i = 1; i < argc; i++) {
        if (strlen(argv[i]) == 2 && argv[i][0] == '-') {
            char option = argv[i][1];
            switch (option) {
                case 'c':
                    mode = MODE_CLIENT;
                    break;
                case 's':
                    mode = MODE_SERVER;
                    break;
                case 'h':
                    host = argv[++i];
                    break;
                case 'p':
                    port = argv[++i];
                    break;
                default:
                    printf("unknown arguments\n");
                    print_usage();
                    exit(1);
            }
        } else {
            printf("unknown arguments\n");
            print_usage();
            exit(1);
        }
    }

    log_i(tag, "Starting...");

    signal(SIGINT, int_handler);

    int sock = -1;
    switch (mode) {
        case MODE_CLIENT:
            sock = make_connection(host, port);
            break;
        case MODE_SERVER:
            sock = listen_for_client(host, port);
            break;
    }

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

struct addrinfo *resolve_host(const char *host, const char *port) {
    char *tag = "resolve_host";

    log_i(tag, "Resolving host: %s...", host);
    struct addrinfo addr_criteria;
    memset(&addr_criteria, 0, sizeof(addr_criteria));
    addr_criteria.ai_family = AF_UNSPEC;
    addr_criteria.ai_socktype = SOCK_STREAM;
    addr_criteria.ai_protocol = IPPROTO_TCP;
    struct addrinfo *addr_list = NULL;
    if (getaddrinfo(host, port, &addr_criteria, &addr_list) || addr_list == NULL) {
        log_e(tag, "Failed to resolve host.");
        exit(1);
    }
    log_i(tag, "Succeeded to resolve host.");

    return addr_list;
}

void sockaddr2ip(struct sockaddr *addr, char *ip_str) {
    switch (addr->sa_family) {
        case AF_INET:
            inet_ntop(AF_INET, &((struct sockaddr_in *) addr)->sin_addr, ip_str, INET_ADDRSTRLEN);
            break;
        case AF_INET6:
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *) addr)->sin6_addr, ip_str, INET6_ADDRSTRLEN);
            break;
        default:
            break;
    }
}

int make_connection(const char *host, const char *port) {
    char *tag = "make_connection";

    struct addrinfo *addr_list = resolve_host(host, port);
    int sock = -1;

    char *addr_str = malloc(INET6_ADDRSTRLEN);
    for (struct addrinfo *addr_info = addr_list; addr_info != NULL; addr_info = addr_info->ai_next) {
        sockaddr2ip(addr_info->ai_addr, addr_str);
        log_i(tag, "Trying %s...", addr_str);

        sock = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
        if (sock < 0) {
            log_e(tag, "Failed to obtain socket.");
            exit(1);
        }
        log_i(tag, "Obtained socket: %d.", sock);

        log_i(tag, "Connecting to server: %s:%s...", addr_str, port);
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

int listen_for_client(const char *host, const char *port) {
    char *tag = "listen_for_client";

    struct addrinfo *addr_info = resolve_host(host, port);
    struct sockaddr *addr = addr_info->ai_addr;

    int server_sock;
    if ((server_sock = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol)) < 0) {
        log_e(tag, "Failed to obtain server socket.");
        exit(1);
    }
    log_i(tag, "Obtained server socket: %d.", server_sock);

    char *addr_str = malloc(INET6_ADDRSTRLEN);
    sockaddr2ip(addr, addr_str);

    log_i(tag, "Binding to ip and port: %s:%s...", addr_str, port);
    if (bind(server_sock, addr, sizeof(*addr)) < 0) {
        log_e(tag, "Failed to bind server socket.");
        exit(1);
    }
    log_i(tag, "Succeeded to bind socket.");

    if (listen(server_sock, 1) < 0) {
        log_e(tag, "Failed to start listening.");
        exit(1);
    }
    log_i(tag, "Start listening...");

    struct sockaddr clnt_addr;
    socklen_t clnt_addr_len = sizeof(clnt_addr);
    int client_sock = accept(server_sock, &clnt_addr, &clnt_addr_len);
    if (client_sock < 0) {
        log_e(tag, "Failed to accept client.");
        exit(1);
    }
    sockaddr2ip(&clnt_addr, addr_str);
    log_i(tag, "Succeeded to accept client: %s, socket: %d.", addr_str, client_sock);
    free(addr_str);
    return client_sock;
}

#pragma clang diagnostic pop
