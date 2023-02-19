#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/wait.h>
struct cmd_struct {
    int client_fd;
    _Atomic(bool) *used;
    char *cmd_str;
    #define CMD_THREADS 10
};
pthread_cond_t cmd_stop_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t cmd_stop_mutex = PTHREAD_MUTEX_INITIALIZER;
bool cmd_stop;
void *cmd_thread_function(void *cmd_struct_pointer);
bool cmd_help(struct cmd_struct *cmd);
bool cmd_ls(struct cmd_struct *cmd);
bool cmd_sh(struct cmd_struct *cmd);
void *cmd_accept_function(void *socket_fd_pointer);
static int system_fd(const char *cmd);
int main() {
    int reuse = 1;
    pthread_t accept_thread;    
    int x;
    struct sockaddr_in addr;
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(socket_fd < 0) {
        perror("socket");
        return -1;
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) {
        perror("setsockopt");
        close(socket_fd);
        return -1;
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse))) {
        perror("setsockopt");
        close(socket_fd);
        return -1;
    }
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    addr.sin_port = htons(1234);
    if(bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(socket_fd);
        return -1;
    }
    if(listen(socket_fd, 30) < 0) {
        perror("listen");
        close(socket_fd);
        return -1;
    }
    x = pthread_create(&accept_thread, NULL, &cmd_accept_function, &socket_fd);
    if (x != 0) {
        errno = x;
        perror("pthread_create");
        close(socket_fd);
        return -1;
    }
    pthread_detach(accept_thread);
    puts("server started");
    pthread_mutex_lock(&cmd_stop_mutex);
    while (cmd_stop != true){
        pthread_cond_wait(&cmd_stop_cond, &cmd_stop_mutex);
    }
    pthread_mutex_unlock(&cmd_stop_mutex);
    close(socket_fd);
    puts("server stopped");
    return 0;
}

void *cmd_thread_function(void *cmd_struct_pointer) {
    char cmd_buffer[8000];
    struct cmd_struct *cmd = cmd_struct_pointer;
    bool exit_cmd = false;
    cmd->cmd_str = cmd_buffer + sizeof(uint16_t);

    while(exit_cmd != true) {
        uint16_t cmd_str_len;
        char *ptr_buffer = cmd_buffer;
        int size;
        size_t recv_length = sizeof(cmd_buffer) - 1;
        size_t total_size = 0;

        memset(cmd_buffer, 0, sizeof(cmd_buffer));
        while (true) {
            if (recv_length == 0) {
                break;
            }
            size = recv(cmd->client_fd, ptr_buffer, recv_length, 0);
            if (size <= 0) {
                if (size == 0) {
                    puts("client closed connection");
                } else {
                    perror("recv client error");
                }
                exit_cmd = true;
                break;
            }
            if (total_size == 0) {
                memcpy(&cmd_str_len, ptr_buffer, sizeof(cmd_str_len));
                cmd_str_len = ntohs(cmd_str_len);
                if (total_size - sizeof(uint16_t) >= recv_length) {
                    break;
                }
                recv_length = cmd_str_len;
                ptr_buffer += size;
            } else {
                total_size += size;
                recv_length -= size;
                ptr_buffer += size;
            }
        }

        if(exit_cmd) {
            continue;
        }
        ptr_buffer = cmd_buffer + sizeof(uint16_t);
        puts(ptr_buffer);
        if (!strcmp(ptr_buffer, "stop server")) {
            puts("client stop server cmd");
            cmd_stop = true;
            int x = send(cmd->client_fd, "\0\0\0", 3, MSG_WAITALL);
            if (x < 0) {
                perror("send");
            }
            pthread_mutex_lock(&cmd_stop_mutex);
            pthread_cond_signal(&cmd_stop_cond);
            pthread_mutex_unlock(&cmd_stop_mutex);
            continue;
        } else if (!strcmp(ptr_buffer, "exit")) {
            puts("client exit cmd");
            int x = send(cmd->client_fd, "\0\0\0", 3, MSG_WAITALL);
            if (x < 0) {
                perror("send");
                cmd_stop = true;
            }
            continue;
        } else if (!strcmp(ptr_buffer, "help")) {
            cmd_stop = cmd_help(cmd);
            continue;
        }else if (!strncmp(ptr_buffer, "ls", 2)) {
            size_t str_len = strlen(ptr_buffer) + 3;
            char *c = malloc(str_len);
            if (!c) {
                cmd_stop = true;
                continue;
            }
            snprintf(c, str_len + 3, "sh %s", ptr_buffer);
            cmd->cmd_str = c;
            cmd_stop = cmd_sh(cmd);
            free(c);
            continue;
        }else if (!strncmp(ptr_buffer, "sh" ,2)) {
            cmd_stop = cmd_sh(cmd);
            continue;
        } else {
            static const char str[] = "command not found";
            uint16_t str_len = htons(sizeof(str));
            int x = send(cmd->client_fd, &str_len, sizeof(str_len), MSG_WAITALL);
            if (x < 0) {
                perror("send");
                cmd_stop = true;
                continue;
            }
            x = send(cmd->client_fd, str, sizeof(str), MSG_WAITALL);
            if (x < 0) {
                perror("send");
                cmd_stop = true;
            }
            //printf("send x = %d\n", x);
            continue;
        }
    }

    close(cmd->client_fd);
    atomic_store_explicit(cmd->used, false, memory_order_release);
    free(cmd);
    return NULL;
}
bool cmd_help(struct cmd_struct *cmd) {
    static const char help_str[] = "help        =  show help message\n"
        "stop server =  stop server applicasion\n"
        "exit        =  stop client\n";
    uint16_t help_str_len = htons(sizeof(help_str));
    int x = send(cmd->client_fd, &help_str_len, sizeof(help_str_len), MSG_WAITALL);
    if (x < 0) {
        perror("send");
        return false;
    }
    x = send(cmd->client_fd, help_str, sizeof(help_str), MSG_WAITALL);
    if (x < 0) {
        perror("send");
        return false;
    }
    //printf("send = %d\n", x);
    return true;
}

void *cmd_accept_function(void *socket_fd_pointer) {
    pthread_t cmd_threads[CMD_THREADS];
    _Atomic(bool) cmd_thread_used[CMD_THREADS];
    int socket_fd = *(int *)socket_fd_pointer;
    bzero(&cmd_threads, sizeof(cmd_threads));
    bzero(&cmd_thread_used, sizeof(cmd_thread_used));
    while(cmd_stop != true) {
        struct cmd_struct *cmd;
        int client_fd = accept(socket_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        int i, x;
        bool cmd_thread_ok = false;
        for (i=0 ;i < CMD_THREADS; i++) {
            if (!atomic_load_explicit(&cmd_thread_used[i],
                                      memory_order_acquire)) {
                cmd_thread_ok = true;
                break;
            }
        }

        if(cmd_thread_ok == false){
            errno = EAGAIN;
            perror("cmd_thread_ok");
            close(client_fd);
            continue;
        }

        cmd = malloc(sizeof(*cmd));
        if (cmd == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }
        cmd->client_fd = client_fd;
        cmd->used = &cmd_thread_used[i];
        atomic_store_explicit(&cmd_thread_used[i], true, memory_order_release);
        x = pthread_create(&cmd_threads[i], NULL, &cmd_thread_function, cmd);
        if (x != 0) {
            errno = x;
            perror("pthread_create");
            free(cmd);
        }
        pthread_detach(cmd_threads[i]);
    }
    return NULL;
}
const char *get_dir_name(const char *cmd) {
    cmd += 2;
    if (*cmd == '\0') {
        return ".";
    }
    return cmd + 1;
}
bool cmd_ls(struct cmd_struct *cmd) {
    // const char *dir = get_dir_name(cmd->cmd_str);
    // DIR *fd_dir = opendir(dir);
    return true;
}
bool cmd_sh(struct cmd_struct *cmds) {
    const char *cmd = cmds->cmd_str + 2;
    int fd;
    char buffer[8000];
    int size;
    fd = system_fd(cmd);
    if (fd < 0) {
        return false;
    }
    size = read(fd, buffer, sizeof(buffer) - 1);
    printf("size = %d\n", size);
    close(fd);
    if (size < 0) {
        perror("read");
        return false;
    }
    buffer[size] = '\0';
    uint16_t buffer_len = htons((uint16_t)size);
    int x = send(cmds->client_fd, &buffer_len, sizeof(buffer_len), MSG_WAITALL);
    if (x < 0) {
        perror("send");
        return false;
    }
    x = send(cmds->client_fd, buffer, buffer_len, MSG_WAITALL);
    if (x < 0) {
        perror("send");
        return false;
    }
    return true;
}

static int system_fd(const char *cmd)
{
    int pipe_fd[2];
    pid_t child;

    if (pipe(pipe_fd)) {
        perror("pipe");
        return -1;
    }

    child = fork();
    if (child < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        perror("fork");
        return -1;
    }

    if (!child) {
        int exit_code;

        dup2(pipe_fd[1], 1);
        dup2(pipe_fd[1], 2);
        exit_code = system(cmd);
        close(0);
        close(1);
        close(2);
        close(pipe_fd[0]);
        exit(exit_code);
    }

    close(pipe_fd[1]);
    wait(NULL);
    return pipe_fd[0];
}

