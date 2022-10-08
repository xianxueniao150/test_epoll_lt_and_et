#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_INPUT_CHAR 256
#define MAX_READ 256
#define WRITE_NUM 8

struct WRITE_BUFFER {
    int size;
    char *ptr;
    char *buf;
};

static struct WRITE_BUFFER *wb;

static void *new_buffer(int size) {
    wb = (struct WRITE_BUFFER *)malloc(sizeof(struct WRITE_BUFFER));
    wb->size = size;
    wb->buf = (char *)malloc(sizeof(char) * size);
    wb->ptr = wb->buf;
}

static void free_buffer() {
    free(wb->buf);
    free(wb);
    wb = NULL;
}

static int try_connect(const char *host, int port) {
    struct addrinfo ai_hints;
    struct addrinfo *ai_list = NULL;

    char portstr[16];
    sprintf(portstr, "%d", port);
    memset(&ai_hints, 0, sizeof(ai_hints));

    ai_hints.ai_family = AF_INET;
    ai_hints.ai_socktype = SOCK_STREAM;
    ai_hints.ai_protocol = IPPROTO_TCP;

    int status = getaddrinfo(host, portstr, &ai_hints, &ai_list);
    if (status != 0) {
        freeaddrinfo(ai_list);
        printf("getaddrinfo fail");
        return -1;
    }

    int fd = socket(ai_list->ai_family, ai_list->ai_socktype, ai_list->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(ai_list);
        return -1;
    }
    status = connect(fd, ai_list->ai_addr, ai_list->ai_addrlen);
    if (status != 0) {
        freeaddrinfo(ai_list);
        close(fd);
        printf("connect fail");
        return -1;
    }

    freeaddrinfo(ai_list);
    printf("connect to server success \n");
    return fd;
}

static void input(int epfd, int fd, int event) {
    printf("begin input\n");
    char buf[MAX_INPUT_CHAR];
    int idx = 0;
    for (;;) {
        int c = getchar();
        if (c == '\n' || idx >= MAX_INPUT_CHAR - 1) {
            break;
        }
        buf[idx] = c;
        idx++;
    }
    buf[idx] = '\0';

    if (idx > 0) {
        wb = new_buffer(idx);
        struct epoll_event ee;
        ee.events = event;
        ee.data.fd = fd;
        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ee);
        memcpy(wb->buf, buf, sizeof(char) * idx);
    }
    printf("%s\n", buf);
    printf("end input\n");
}

static void output(int epfd, int fd, int event) {
    printf("begin to output\n");
    if (wb) {
        int wsize = wb->size - (wb->ptr - wb->buf);
        if (wsize > WRITE_NUM) {
            wsize = WRITE_NUM;
        }
        int n = write(fd, wb->ptr, wsize);
        if (n <= 0) {
            abort();
        }

        printf("wsize:%d n:%d\n", wsize, n);
        printf("<<<<");
        char *ptr = (char *)wb->ptr;
        for (int i = 0; i < n; i++) {
            printf("%c", ptr[i]);
        }
        printf("\n");

        wb->ptr += wsize;
        if (wb->ptr >= wb->buf + wb->size) { //这个buff写完了
            free_buffer();
            struct epoll_event ee;
            ee.events = event;
            ee.data.fd = fd;
            // epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ee);
        }
    }
    printf("end to output\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("please input mode, lt(1) or et(2)\n");
        return -1;
    }

    int ep_event = 0;
    if (strcmp(argv[1], "lt") == 0) {
        printf("client epoll set lt\n");
        ep_event = 0;
    } else if (strcmp(argv[1], "et") == 0) {
        printf("client epoll set et\n");
        ep_event = EPOLLET;
    } else {
        printf("unknow mode %s please input lt or et\n", argv[1]);
        return -1;
    }

    int epfd = epoll_create(1024);
    if (epfd == -1) {
        printf("can not create epoll error:%d\n", errno);
        return -1;
    }

    int fd = try_connect("127.0.0.1", 8002);
    if (fd < 0) {
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);

    int socket_event = EPOLLIN | ep_event;
    struct epoll_event e;
    e.events = socket_event;
    e.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &e);

    input(epfd, fd, socket_event | EPOLLOUT);

    for (;;) {

        printf("before epoll_wait\n");
        struct epoll_event ev[1];
        int n = epoll_wait(epfd, ev, 1, -1);
        if (n == -1) {
            printf("epoll_wait error %d", errno);
            break;
        }
        printf("after epoll_wait\n");

        for (int i = 0; i < n; i++) {
            struct epoll_event *e = &ev[i];
            int flag = e->events;
            int r = (flag & EPOLLIN) != 0;
            int w = (flag & EPOLLOUT) != 0;

            if (r) {
                printf("begin to read fd:%d\n", fd);
                char read_buf[MAX_READ] = {0};
                int rn = read(fd, read_buf, MAX_READ);
                if (rn <= 0) {
                    goto _EXIT;
                }

                printf(">>>>");
                for (int j = 0; j < rn; j++) {
                    printf("%c", read_buf[j]);
                }
                printf("\n");
            }

            if (w) {
                output(epfd, fd, socket_event);
            }
        }
    }

_EXIT:
    close(fd);
    close(epfd);

    return 0;
}
