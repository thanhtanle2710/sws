#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RESPONSE_SIZE 32768

static char root[128];
static char log_path[160];
static pid_t debug_pid = -1;
static pid_t daemon_pid = -1;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void check(int condition, const char *message)
{
    if (!condition) fail(message);
}

static void path_join(char *output, size_t capacity, const char *directory,
                      const char *name)
{
    int length = snprintf(output, capacity, "%s/%s", directory, name);
    check(length >= 0 && (size_t)length < capacity, "path too long");
}

static void write_file(const char *path, const char *contents)
{
    FILE *file = fopen(path, "wb");
    check(file != NULL, "cannot create fixture");
    check(fwrite(contents, 1, strlen(contents), file) == strlen(contents),
          "cannot write fixture");
    check(fclose(file) == 0, "cannot close fixture");
}

static void cleanup(void)
{
    char path[256];
    if (debug_pid > 0) {
        (void)kill(debug_pid, SIGTERM);
        (void)waitpid(debug_pid, NULL, 0);
    }
    if (daemon_pid > 0) (void)kill(daemon_pid, SIGTERM);
    if (root[0] == '\0') return;
    path_join(path, sizeof(path), root, "index.html"); (void)unlink(path);
    path_join(path, sizeof(path), root, "unusual.bin"); (void)unlink(path);
    path_join(path, sizeof(path), root, "listing/a.txt"); (void)unlink(path);
    path_join(path, sizeof(path), root, "listing/b.txt"); (void)unlink(path);
    path_join(path, sizeof(path), root, "listing/.hidden"); (void)unlink(path);
    path_join(path, sizeof(path), root, "listing"); (void)rmdir(path);
    (void)unlink(log_path);
    (void)rmdir(root);
}

static void prepare_files(void)
{
    char template[] = "/tmp/sws-test-XXXXXX";
    char path[256];
    check(mkdtemp(template) != NULL, "cannot create temporary directory");
    (void)snprintf(root, sizeof(root), "%s", template);
    path_join(log_path, sizeof(log_path), root, "requests.log");
    path_join(path, sizeof(path), root, "index.html"); write_file(path, "HOME");
    path_join(path, sizeof(path), root, "unusual.bin"); write_file(path, "plain text");
    path_join(path, sizeof(path), root, "listing");
    check(mkdir(path, 0700) == 0, "cannot create listing directory");
    path_join(path, sizeof(path), root, "listing/a.txt"); write_file(path, "a");
    path_join(path, sizeof(path), root, "listing/b.txt"); write_file(path, "b");
    path_join(path, sizeof(path), root, "listing/.hidden"); write_file(path, "hidden");
}

static int free_port(void)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    check(fd >= 0, "cannot create port reservation");
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    check(bind(fd, (struct sockaddr *)&address, sizeof(address)) == 0,
          "cannot reserve port");
    check(getsockname(fd, (struct sockaddr *)&address, &length) == 0,
          "cannot read port");
    close(fd);
    return ntohs(address.sin_port);
}

static int connect_to(int port)
{
    struct sockaddr_in address;
    struct timeval timeout = {2, 0};
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((unsigned short)port);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void wait_until_ready(int port)
{
    struct timespec pause = {0, 20000000};
    int attempt;
    int fd;
    for (attempt = 0; attempt < 100; attempt++) {
        fd = connect_to(port);
        if (fd >= 0) {
            close(fd);
            return;
        }
        (void)nanosleep(&pause, NULL);
    }
    fail("server did not start");
}

static void send_all(int fd, const char *data)
{
    size_t remaining = strlen(data);
    while (remaining > 0) {
        ssize_t written = write(fd, data, remaining);
        check(written > 0, "request write failed");
        data += written;
        remaining -= (size_t)written;
    }
}

static void request(int port, const char *first, const char *second,
                    char response[RESPONSE_SIZE])
{
    struct timespec pause = {0, 20000000};
    size_t used = 0;
    ssize_t count;
    int fd = connect_to(port);
    check(fd >= 0, "client cannot connect");
    send_all(fd, first);
    if (second != NULL) {
        (void)nanosleep(&pause, NULL);
        send_all(fd, second);
    }
    (void)shutdown(fd, SHUT_WR);
    while (used + 1 < RESPONSE_SIZE) {
        count = read(fd, response + used, RESPONSE_SIZE - used - 1);
        if (count < 0 && errno == EINTR) continue;
        check(count >= 0, "response read failed");
        if (count == 0) break;
        used += (size_t)count;
    }
    response[used] = '\0';
    close(fd);
}

static void status_is(const char *response, const char *status)
{
    check(strncmp(response, status, strlen(status)) == 0, status);
    check(strstr(response, "\r\nDate: ") != NULL, "Date header missing");
    check(strstr(response, "\r\nServer: sws/0.1\r\n") != NULL,
          "Server header missing");
}

static void start_debug(int port)
{
    char port_text[16];
    int output;
    (void)snprintf(port_text, sizeof(port_text), "%d", port);
    debug_pid = fork();
    check(debug_pid >= 0, "cannot fork debug server");
    if (debug_pid == 0) {
        output = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (output < 0) _exit(127);
        (void)dup2(output, STDOUT_FILENO);
        (void)dup2(output, STDERR_FILENO);
        close(output);
        execl("./sws", "./sws", "-d", "-i", "127.0.0.1",
              "-p", port_text, "-c", "tests", root, (char *)NULL);
        _exit(127);
    }
    wait_until_ready(port);
}

static void test_request(int port)
{
    char response[RESPONSE_SIZE];
    request(port, "GET / HTTP/1.1\r\n", "Host: localhost\r\n\r\n", response);
    status_is(response, "HTTP/1.0 200 OK");
    request(port, "bad request\r\n\r\n", NULL, response);
    status_is(response, "HTTP/1.0 400 Bad Request");
    puts("request parser: passed");
}

static void test_response(int port)
{
    char response[RESPONSE_SIZE];
    char request_text[256];
    char modified[128];
    char *start;
    char *end;
    request(port, "GET / HTTP/1.0\r\n\r\n", NULL, response);
    status_is(response, "HTTP/1.0 200 OK");
    check(strstr(response, "\r\nContent-Length: 4\r\n") != NULL, "length wrong");
    check(strstr(response, "\r\n\r\nHOME") != NULL, "GET body missing");
    start = strstr(response, "Last-Modified: ");
    check(start != NULL, "Last-Modified missing");
    start += strlen("Last-Modified: ");
    end = strstr(start, "\r\n");
    check(end != NULL && (size_t)(end - start) < sizeof(modified), "date invalid");
    memcpy(modified, start, (size_t)(end - start));
    modified[end - start] = '\0';
    (void)snprintf(request_text, sizeof(request_text),
                   "GET / HTTP/1.0\r\nIf-Modified-Since: %s\r\n\r\n", modified);
    request(port, request_text, NULL, response);
    status_is(response, "HTTP/1.0 304 Not Modified");
    request(port, "HEAD / HTTP/1.0\r\n\r\n", NULL, response);
    status_is(response, "HTTP/1.0 200 OK");
    start = strstr(response, "\r\n\r\n");
    check(start != NULL && start[4] == '\0', "HEAD returned body");
    request(port, "GET /listing/ HTTP/1.0\r\n\r\n", NULL, response);
    status_is(response, "HTTP/1.0 200 OK");
    start = strstr(response, "a.txt");
    end = strstr(response, "b.txt");
    check(start != NULL && end != NULL && start < end, "index order wrong");
    check(strstr(response, ".hidden") == NULL, "hidden file listed");
    request(port, "GET /missing HTTP/1.0\r\n\r\n", NULL, response);
    status_is(response, "HTTP/1.0 404 Not Found");
    request(port, "GET /../main.c HTTP/1.0\r\n\r\n", NULL, response);
    status_is(response, "HTTP/1.0 403 Forbidden");
    request(port, "GET /unusual.bin HTTP/1.0\r\n\r\n", NULL, response);
    status_is(response, "HTTP/1.0 200 OK");
    check(strstr(response, "Content-Type: text/plain") != NULL,
          "MIME detection wrong");
    puts("static responses: passed");
}

static void test_cgi(int port)
{
    char response[RESPONSE_SIZE];
    request(port, "GET /cgi-bin/cgi_fixture/part?x=1 HTTP/1.0\r\n\r\n",
            NULL, response);
    status_is(response, "HTTP/1.0 200 OK");
    check(strstr(response, "GET|x=1|/part") != NULL, "CGI environment wrong");
    request(port, "GET /cgi-bin/cgi_fixture/redirect HTTP/1.0\r\n\r\n",
            NULL, response);
    status_is(response, "HTTP/1.0 200 OK");
    check(strstr(response, "\r\n\r\nHOME") != NULL, "local redirect failed");
    request(port, "GET /cgi-bin/cgi_fixture/created HTTP/1.0\r\n\r\n",
            NULL, response);
    status_is(response, "HTTP/1.0 201 Created");
    check(strstr(response, "\r\n\r\nNEW") != NULL, "CGI status body wrong");
    request(port, "GET /cgi-bin/cgi_fixture/arguments?one+two HTTP/1.0\r\n\r\n",
            NULL, response);
    status_is(response, "HTTP/1.0 200 OK");
    check(strstr(response, "\r\n\r\none|two") != NULL, "CGI arguments wrong");
    puts("CGI: passed");
}

static pid_t find_daemon(const char *port_text)
{
    DIR *directory = opendir("/proc");
    struct dirent *entry;
    char path[512];
    char command[4096];
    ssize_t length;
    size_t offset;
    int fd;
    pid_t found = -1;
    if (directory == NULL) return -1;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        if (snprintf(path, sizeof(path), "/proc/%s/cmdline",
                     entry->d_name) >= (int)sizeof(path)) continue;
        fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        length = read(fd, command, sizeof(command) - 1);
        close(fd);
        if (length <= 0) continue;
        command[length] = '\0';
        if (strcmp(command, "./sws") != 0) continue;
        offset = strlen(command) + 1;
        while (offset < (size_t)length) {
            if (strcmp(command + offset, port_text) == 0) {
                found = (pid_t)atoi(entry->d_name);
                break;
            }
            offset += strlen(command + offset) + 1;
        }
        if (found > 0) break;
    }
    closedir(directory);
    return found;
}

static double elapsed_seconds(const struct timespec *start,
                              const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static void test_server(void)
{
    char port_text[16];
    char response[RESPONSE_SIZE];
    char log_contents[4096];
    struct timespec start;
    struct timespec end;
    struct timespec pause = {0, 20000000};
    FILE *log;
    pid_t launcher;
    int port;
    int slow;
    int status;
    int attempt;
    int exit_code;
    int output_pipe[2];
    struct pollfd output_ready;
    char output_byte;
    struct utsname system;

    if (uname(&system) != 0 || strcmp(system.sysname, "Linux") != 0) {
        puts("daemon/concurrency: skipped (Linux test)");
        return;
    }
    DIR *proc = opendir("/proc");
    check(proc != NULL, "cannot open /proc");
    closedir(proc);
    log = fopen(log_path, "wb");
    check(log != NULL && fclose(log) == 0, "cannot reset request log");
    port = free_port();
    (void)snprintf(port_text, sizeof(port_text), "%d", port);
    check(pipe(output_pipe) == 0, "cannot create stdout pipe");
    launcher = fork();
    check(launcher >= 0, "cannot fork daemon launcher");
    if (launcher == 0) {
        close(output_pipe[0]);
        (void)dup2(output_pipe[1], STDOUT_FILENO);
        close(output_pipe[1]);
        execl("./sws", "./sws", "-i", "127.0.0.1", "-p", port_text,
              "-l", log_path, root, (char *)NULL);
        _exit(127);
    }
    close(output_pipe[1]);
    check(waitpid(launcher, &status, 0) == launcher &&
          WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "daemon launcher failed");
    output_ready.fd = output_pipe[0];
    output_ready.events = POLLIN;
    check(poll(&output_ready, 1, 2000) > 0, "daemon kept stdout open");
    check(read(output_pipe[0], &output_byte, 1) == 0,
          "daemon wrote to stdout");
    close(output_pipe[0]);
    for (attempt = 0; attempt < 100; attempt++) {
        daemon_pid = find_daemon(port_text);
        if (daemon_pid > 0) break;
        (void)nanosleep(&pause, NULL);
    }
    check(daemon_pid > 0, "daemon process not found");
    wait_until_ready(port);
    slow = connect_to(port);
    check(slow >= 0, "slow client cannot connect");
    send_all(slow, "GET / HTTP/1.0\r\n");
    check(clock_gettime(CLOCK_MONOTONIC, &start) == 0, "clock failed");
    request(port, "GET / HTTP/1.0\r\n\r\n", NULL, response);
    check(clock_gettime(CLOCK_MONOTONIC, &end) == 0, "clock failed");
    status_is(response, "HTTP/1.0 200 OK");
    check(elapsed_seconds(&start, &end) < 1.5, "requests were sequential");
    close(slow);
    exit_code = 0;
    for (attempt = 0; attempt < 50; attempt++) {
        log = fopen(log_path, "rb");
        if (log != NULL) {
            size_t count = fread(log_contents, 1, sizeof(log_contents) - 1, log);
            log_contents[count] = '\0';
            fclose(log);
            if (strstr(log_contents, "\"GET / HTTP/1.0\" 200 4") != NULL) {
                exit_code = 1;
                break;
            }
        }
        (void)nanosleep(&pause, NULL);
    }
    check(exit_code == 1, "request log missing");
    puts("daemon/concurrency: passed");
}

int main(int argc, char **argv)
{
    const char *section = argc > 1 ? argv[1] : "all";
    int port;
    check(strcmp(section, "all") == 0 || strcmp(section, "request") == 0 ||
          strcmp(section, "response") == 0 || strcmp(section, "cgi") == 0 ||
          strcmp(section, "server") == 0, "unknown test section");
    check(atexit(cleanup) == 0, "cannot register cleanup");
    prepare_files();
    if (strcmp(section, "server") != 0) {
        port = free_port();
        start_debug(port);
        if (strcmp(section, "all") == 0 || strcmp(section, "request") == 0)
            test_request(port);
        if (strcmp(section, "all") == 0 || strcmp(section, "response") == 0)
            test_response(port);
        if (strcmp(section, "all") == 0 || strcmp(section, "cgi") == 0)
            test_cgi(port);
        (void)kill(debug_pid, SIGTERM);
        (void)waitpid(debug_pid, NULL, 0);
        debug_pid = -1;
    }
    if (strcmp(section, "all") == 0 || strcmp(section, "server") == 0)
        test_server();
    return 0;
}
