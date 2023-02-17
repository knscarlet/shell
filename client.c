#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
struct cmd_struct {
    uint16_t cmd_str_len;
    char cmd_str[8000];
} __attribute__((__packed__));
int main() {
    struct sockaddr_in addr;
    char cmd_str[8000];
    char buffer[8000];
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(socket_fd < 0) {
        perror("socket");
        return -1;
    }
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    addr.sin_port = htons(1234);
    if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("connect");
        return -1;
    }
    while (true) {
        int size_2;
        int size_3;
        struct cmd_struct cmd;
        uint16_t cmd_str_len;
        printf("shell > ");
        fflush(stdout);
        int size = read(0, cmd_str, sizeof(cmd_str) - 1);
        if (size <= 0) {
            if (size == 0) {
                puts("stdin EOF");
            } else {
                perror("read");
            }
            break;
        }
        if (cmd_str[size - 1] == '\n') {
            cmd_str[size - 1] = '\0';
            size--;
        } else {
            cmd_str[size] = '\0';
        }
        cmd.cmd_str_len = htons((uint16_t)size);
        memcpy(&cmd.cmd_str, cmd_str, sizeof(cmd.cmd_str));
        size_2 = send(socket_fd, &cmd, sizeof(cmd.cmd_str_len) + size + 1, MSG_WAITALL);
        if (size_2 <= 0) {
            if (size_2 == 0) {
                puts("server disconnect");
            } else {
                perror("send");
            }
            break;
        }
        size_3 = 0;
        cmd_str_len = 0;
        while (size_3 < 2 || size_3 < cmd_str_len + (int)sizeof(uint16_t)) {
            int size_4 = recv(socket_fd, buffer + size_3, sizeof(buffer) - 1 - size_3, 0);
            if (size_4 <= 0) {
                if (size_4 == 0) {
                    puts("server disconnect");
                } else {
                    perror("recv");
                }
                goto exit;
            }
            size_3 += size_4;
            if (size_3 >= (int)sizeof(uint16_t)) {
                if (cmd_str_len == 0) {
                    memcpy(&cmd_str_len, buffer, sizeof(cmd_str_len));
                    cmd_str_len = ntohs(cmd_str_len);
                    // printf("cmd_str_len = %u\n", cmd_str_len);
                    if (cmd_str_len == 0) {
                        goto exit;
                    }
                }
            }
        }
        buffer[size_3] = '\0';
        puts(buffer + sizeof(uint16_t));
    }
exit:
    close(socket_fd);
    return 0;
}


