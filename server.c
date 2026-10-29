#define _POSIX_C_SOURCE 200809L

#include "server.h"

#include "request.h"
#include "response.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define REQUEST_BUFFER_SIZE 8192
#define MAX_LISTENERS 8

static void reap_children(int signal_number)
{
    int saved_errno = errno;
    (void)signal_number;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    errno = saved_errno;
}

static int open_listeners(const ServerConfig *config, struct pollfd listeners[])
{
    struct addrinfo hints;
    struct addrinfo *addresses;
    struct addrinfo *address;
    int count = 0;
    int fd;
    int one = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(config->bind_address, config->port, &hints, &addresses) != 0) {
        return -1;
    }
    for (address = addresses; address != NULL && count < MAX_LISTENERS;
         address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            continue;
        }
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (address->ai_family == AF_INET6) {
            (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
        }
        if (bind(fd, address->ai_addr, address->ai_addrlen) == 0 &&
            listen(fd, 64) == 0) {
            listeners[count].fd = fd;
            listeners[count].events = POLLIN;
            count++;
        } else {
            close(fd);
        }
    }
    freeaddrinfo(addresses);
    return count > 0 ? count : -1;
}

static int daemonize(void)
{
    pid_t pid = fork();
    int null_fd;

    if (pid < 0) {
        return -1;
    }
    if (pid > 0) {
        _exit(0);
    }
    if (setsid() < 0) {
        return -1;
    }
    null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0) {
        return -1;
    }
    (void)dup2(null_fd, STDIN_FILENO);
    (void)dup2(null_fd, STDOUT_FILENO);
    (void)dup2(null_fd, STDERR_FILENO);
    if (null_fd > STDERR_FILENO) {
        close(null_fd);
    }
    return 0;
}

static int request_is_complete(const char *buffer)
{
    return strstr(buffer, "\r\n\r\n") != NULL || strstr(buffer, "\n\n") != NULL;
}

static HttpResult serve_client(int client_fd, char *first_line, size_t line_capacity,
                               const ServerConfig *config)
{
    char buffer[REQUEST_BUFFER_SIZE];
    size_t used = 0;
    size_t line_length;
    ssize_t received;
    HttpRequest request;

    first_line[0] = '\0';
    while (used + 1 < sizeof(buffer)) {
        received = recv(client_fd, buffer + used, sizeof(buffer) - used - 1, 0);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            break;
        }
        used += (size_t)received;
        buffer[used] = '\0';
        if (request_is_complete(buffer)) {
            break;
        }
    }
    buffer[used] = '\0';
    line_length = strcspn(buffer, "\r\n");
    if (line_length >= line_capacity) {
        line_length = line_capacity - 1;
    }
    memcpy(first_line, buffer, line_length);
    first_line[line_length] = '\0';
    if (request_is_complete(buffer) && parse_request(buffer, &request) == 0) {
        return handle_request(client_fd, &request, config);
    }
    return send_error_response(client_fd, 400, "Bad Request", 0);
}

static void log_request(const ServerConfig *config, const char *remote,
                        const char *first_line, HttpResult result)
{
    char timestamp[32];
    char entry[4096];
    char clean_line[2048];
    time_t now = time(NULL);
    struct tm utc;
    FILE *log;
    size_t index;
    int length;

    if (!config->debug && config->log_file == NULL) {
        return;
    }
    if (gmtime_r(&now, &utc) == NULL) {
        return;
    }
    (void)strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    for (index = 0; first_line[index] != '\0' &&
                    index + 1 < sizeof(clean_line); index++) {
        clean_line[index] = first_line[index] == '"' ||
                            first_line[index] < ' ' ? '_' : first_line[index];
    }
    clean_line[index] = '\0';
    length = snprintf(entry, sizeof(entry), "%s %s \"%s\" %d %zu\n",
                      remote, timestamp, clean_line, result.status,
                      result.content_length);
    if (length < 0 || length >= (int)sizeof(entry)) {
        return;
    }
    if (config->debug) {
        (void)fwrite(entry, 1, (size_t)length, stdout);
        (void)fflush(stdout);
    } else {
        log = fopen(config->log_file, "a");
        if (log != NULL) {
            (void)fwrite(entry, 1, (size_t)length, log);
            (void)fclose(log);
        }
    }
}

int run_server(const ServerConfig *config)
{
    struct pollfd listeners[MAX_LISTENERS];
    struct sigaction action;
    struct timeval timeout;
    struct sockaddr_storage peer;
    socklen_t peer_length;
    char remote[128];
    char first_line[REQUEST_BUFFER_SIZE];
    HttpResult result;
    int count;
    int ready;
    int client_fd;
    int index;
    pid_t child;

    (void)signal(SIGPIPE, SIG_IGN);
    count = open_listeners(config, listeners);
    if (count < 0) {
        return -1;
    }
    if (!config->debug && daemonize() != 0) {
        return -1;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = reap_children;
    sigemptyset(&action.sa_mask);
    if (!config->debug) {
        (void)sigaction(SIGCHLD, &action, NULL);
    }

    for (;;) {
        ready = poll(listeners, (nfds_t)count, -1);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0) {
            return -1;
        }
        for (index = 0; index < count; index++) {
            if (!(listeners[index].revents & POLLIN)) {
                continue;
            }
            peer_length = sizeof(peer);
            client_fd = accept(listeners[index].fd,
                               (struct sockaddr *)&peer, &peer_length);
            if (client_fd < 0) {
                continue;
            }
            if (getnameinfo((struct sockaddr *)&peer, peer_length, remote,
                            sizeof(remote), NULL, 0, NI_NUMERICHOST) != 0) {
                (void)snprintf(remote, sizeof(remote), "unknown");
            }
            if (!config->debug) {
                child = fork();
                if (child < 0) {
                    close(client_fd);
                    continue;
                }
                if (child > 0) {
                    close(client_fd);
                    continue;
                }
                for (ready = 0; ready < count; ready++) {
                    close(listeners[ready].fd);
                }
                (void)signal(SIGCHLD, SIG_DFL);
            }
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;
            (void)setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                             &timeout, sizeof(timeout));
            result = serve_client(client_fd, first_line, sizeof(first_line), config);
            log_request(config, remote, first_line, result);
            close(client_fd);
            if (!config->debug) {
                _exit(0);
            }
        }
    }
}
