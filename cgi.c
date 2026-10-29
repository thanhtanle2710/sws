#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "cgi.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CGI_PATH_SIZE 4096
#define CGI_OUTPUT_LIMIT (16 * 1024 * 1024)

static int within_root(const char *root, const char *path)
{
    size_t length = strlen(root);
    if (strcmp(root, "/") == 0) return path[0] == '/';
    return strncmp(root, path, length) == 0 &&
           (path[length] == '/' || path[length] == '\0');
}

static int find_script(const char *root, const char *resource,
                       char *script, char *script_name, char *path_info)
{
    char canonical_root[CGI_PATH_SIZE];
    char candidate[CGI_PATH_SIZE];
    char prefix[HTTP_URI_SIZE];
    struct stat info;
    size_t length = strlen(resource);
    int count;

    if (realpath(root, canonical_root) == NULL) return 0;
    while (length > 0) {
        count = snprintf(candidate, sizeof(candidate), "%s%.*s",
                         root, (int)length, resource);
        if (count > 0 && count < (int)sizeof(candidate) &&
            realpath(candidate, script) != NULL &&
            within_root(canonical_root, script) &&
            stat(script, &info) == 0 && S_ISREG(info.st_mode) &&
            access(script, X_OK) == 0) {
            (void)snprintf(script_name, HTTP_URI_SIZE, "/cgi-bin%.*s",
                           (int)length, resource);
            (void)snprintf(path_info, HTTP_URI_SIZE, "%s", resource + length);
            return 1;
        }
        memcpy(prefix, resource, length);
        prefix[length] = '\0';
        while (length > 0 && prefix[length - 1] != '/') length--;
        if (length > 0) length--;
    }
    return 0;
}

static void set_connection_variables(int client_fd, const ServerConfig *config)
{
    struct sockaddr_storage address;
    socklen_t length = sizeof(address);
    char value[128];

    if (getpeername(client_fd, (struct sockaddr *)&address, &length) == 0 &&
        getnameinfo((struct sockaddr *)&address, length, value, sizeof(value),
                    NULL, 0, NI_NUMERICHOST) == 0) {
        (void)setenv("REMOTE_ADDR", value, 1);
    }
    length = sizeof(address);
    if (getsockname(client_fd, (struct sockaddr *)&address, &length) == 0 &&
        getnameinfo((struct sockaddr *)&address, length, value, sizeof(value),
                    NULL, 0, NI_NUMERICHOST) == 0) {
        (void)setenv("SERVER_NAME", value, 1);
    }
    (void)setenv("SERVER_PORT", config->port, 1);
}

static int hex_value(char digit)
{
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

static void set_query_arguments(const char *query, char *storage,
                                char *arguments[], size_t capacity)
{
    size_t count = 1;
    char *write_cursor = storage;
    const char *read_cursor = query;

    arguments[1] = NULL;
    if (query == NULL || *query == '\0' || strchr(query, '=') != NULL) return;
    arguments[count++] = write_cursor;
    while (*read_cursor != '\0' && *read_cursor != '#') {
        if (*read_cursor == '+') {
            if (write_cursor == arguments[count - 1] || count + 1 >= capacity) {
                arguments[1] = NULL;
                return;
            }
            *write_cursor++ = '\0';
            arguments[count++] = write_cursor;
            read_cursor++;
        } else if (*read_cursor == '%') {
            int high = hex_value(read_cursor[1]);
            int low = read_cursor[1] != '\0' ? hex_value(read_cursor[2]) : -1;
            if (high < 0 || low < 0 || (high == 0 && low == 0)) {
                arguments[1] = NULL;
                return;
            }
            *write_cursor++ = (char)(high * 16 + low);
            read_cursor += 3;
        } else {
            *write_cursor++ = *read_cursor++;
        }
    }
    if (write_cursor == arguments[count - 1]) {
        arguments[1] = NULL;
        return;
    }
    *write_cursor = '\0';
    arguments[count] = NULL;
}

static void execute_script(int client_fd, const HttpRequest *request,
                           const ServerConfig *config, const char *script,
                           const char *script_name, const char *path_info,
                           int pipe_write)
{
    const char *query = strchr(request->uri, '?');
    char translated[CGI_PATH_SIZE];
    int input = open("/dev/null", O_RDONLY);
    char *arguments[64];
    char query_arguments[HTTP_URI_SIZE];

    if (input < 0) _exit(127);
    (void)dup2(input, STDIN_FILENO);
    close(input);
    (void)dup2(pipe_write, STDOUT_FILENO);
    close(pipe_write);
    (void)setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
    (void)setenv("REQUEST_METHOD", request->method, 1);
    (void)setenv("SCRIPT_NAME", script_name, 1);
    (void)setenv("PATH_INFO", path_info, 1);
    if (*path_info != '\0' &&
        snprintf(translated, sizeof(translated), "%s%s",
                 config->document_root, path_info) < (int)sizeof(translated)) {
        (void)setenv("PATH_TRANSLATED", translated, 1);
    } else {
        (void)unsetenv("PATH_TRANSLATED");
    }
    (void)setenv("QUERY_STRING", query != NULL ? query + 1 : "", 1);
    (void)setenv("SERVER_PROTOCOL", "HTTP/1.0", 1);
    (void)setenv("SERVER_SOFTWARE", "sws/0.1", 1);
    if (request->host[0] != '\0') (void)setenv("HTTP_HOST", request->host, 1);
    set_connection_variables(client_fd, config);
    arguments[0] = (char *)script;
    set_query_arguments(query != NULL ? query + 1 : NULL,
                        query_arguments, arguments, 64);
    execv(script, arguments);
    _exit(127);
}

static int collect_output(int fd, char **output, size_t *length)
{
    char *buffer = NULL;
    size_t used = 0;
    size_t capacity = 8192;
    ssize_t count;
    char *grown;

    buffer = malloc(capacity);
    if (buffer == NULL) return -1;
    for (;;) {
        if (used + 4096 + 1 > capacity) {
            capacity *= 2;
            if (capacity > CGI_OUTPUT_LIMIT) {
                free(buffer);
                return -1;
            }
            grown = realloc(buffer, capacity);
            if (grown == NULL) {
                free(buffer);
                return -1;
            }
            buffer = grown;
        }
        count = read(fd, buffer + used, capacity - used - 1);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            free(buffer);
            return -1;
        }
        if (count == 0) break;
        used += (size_t)count;
    }
    buffer[used] = '\0';
    *output = buffer;
    *length = used;
    return 0;
}

static int parse_cgi_headers(char *output, size_t length, size_t *body_start,
                             int *status, char *reason, char *type,
                             char *location, char *extra)
{
    size_t index;
    size_t header_end = 0;
    char *cursor;
    char *line_end;
    char *colon;
    char *value;
    size_t remaining = 1024;

    for (index = 0; index + 1 < length && index < 8192; index++) {
        if (output[index] == '\n' && output[index + 1] == '\n') {
            header_end = index;
            *body_start = index + 2;
            break;
        }
        if (index + 3 < length && memcmp(output + index, "\r\n\r\n", 4) == 0) {
            header_end = index;
            *body_start = index + 4;
            break;
        }
    }
    if (*body_start == 0 || memchr(output, '\0', header_end) != NULL) return -1;
    output[header_end] = '\0';
    cursor = output;
    while (*cursor != '\0') {
        line_end = strchr(cursor, '\n');
        if (line_end != NULL) {
            *line_end = '\0';
            if (line_end > cursor && line_end[-1] == '\r') line_end[-1] = '\0';
        }
        colon = strchr(cursor, ':');
        if (colon == NULL) return -1;
        *colon++ = '\0';
        value = colon;
        while (*value == ' ' || *value == '\t') value++;
        if (strcasecmp(cursor, "Status") == 0) {
            *status = atoi(value);
            if (*status < 100 || *status > 599) return -1;
            value = strchr(value, ' ');
            if (value != NULL) (void)snprintf(reason, 64, "%s", value + 1);
        } else if (strcasecmp(cursor, "Content-Type") == 0) {
            (void)snprintf(type, 256, "%s", value);
        } else if (strcasecmp(cursor, "Location") == 0) {
            (void)snprintf(location, 1024, "%s", value);
            if (strlen(value) + 12 >= remaining) return -1;
            (void)snprintf(extra + strlen(extra), remaining, "Location: %s\r\n", value);
            remaining = 1024 - strlen(extra);
        } else if (strcasecmp(cursor, "Content-Length") != 0 &&
                   strcasecmp(cursor, "Connection") != 0 &&
                   strcasecmp(cursor, "Date") != 0 &&
                   strcasecmp(cursor, "Server") != 0) {
            size_t needed = strlen(cursor) + strlen(value) + 5;
            if (needed >= remaining) return -1;
            (void)snprintf(extra + strlen(extra), remaining, "%s: %s\r\n", cursor, value);
            remaining = 1024 - strlen(extra);
        }
        if (line_end == NULL) break;
        cursor = line_end + 1;
    }
    return 0;
}

HttpResult handle_cgi(int client_fd, const HttpRequest *request,
                      const ServerConfig *config, const char *resource_path)
{
    static int redirect_depth;
    char script[CGI_PATH_SIZE];
    char script_name[HTTP_URI_SIZE];
    char path_info[HTTP_URI_SIZE];
    char *output = NULL;
    size_t length = 0;
    size_t body_start = 0;
    char reason[64] = "OK";
    char type[256] = "";
    char location[1024] = "";
    char extra[1024] = "";
    int status = 200;
    int pipes[2];
    int exit_status;
    pid_t child;
    HttpResult result;

    if (!find_script(config->cgi_root, resource_path, script,
                     script_name, path_info)) {
        return send_error_response(client_fd, 404, "Not Found",
            strcmp(request->method, "HEAD") == 0);
    }
    if (pipe(pipes) != 0) return send_error_response(client_fd, 500,
        "Internal Server Error", 0);
    child = fork();
    if (child < 0) {
        close(pipes[0]); close(pipes[1]);
        return send_error_response(client_fd, 500, "Internal Server Error", 0);
    }
    if (child == 0) {
        close(pipes[0]);
        execute_script(client_fd, request, config, script,
                       script_name, path_info, pipes[1]);
    }
    close(pipes[1]);
    status = collect_output(pipes[0], &output, &length);
    close(pipes[0]);
    if (status != 0) (void)kill(child, SIGKILL);
    if (waitpid(child, &exit_status, 0) < 0 || status != 0 ||
        !WIFEXITED(exit_status) || WEXITSTATUS(exit_status) != 0) {
        free(output);
        return send_error_response(client_fd, 500, "Internal Server Error", 0);
    }
    status = 200;
    if (parse_cgi_headers(output, length, &body_start, &status,
                          reason, type, location, extra) != 0) {
        free(output);
        return send_error_response(client_fd, 502, "Bad Gateway", 0);
    }
    if (location[0] == '/' && type[0] == '\0' && body_start == length) {
        HttpRequest redirected = *request;
        if (++redirect_depth > 5) {
            redirect_depth--;
            free(output);
            return send_error_response(client_fd, 500, "Internal Server Error", 0);
        }
        (void)snprintf(redirected.uri, sizeof(redirected.uri), "%s", location);
        free(output);
        result = handle_request(client_fd, &redirected, config);
        redirect_depth--;
        return result;
    }
    if (location[0] != '\0' && status == 200 && type[0] == '\0') {
        status = 302;
        (void)snprintf(reason, sizeof(reason), "Found");
    }
    if (type[0] == '\0' && location[0] == '\0') {
        free(output);
        return send_error_response(client_fd, 502, "Bad Gateway", 0);
    }
    result.status = status;
    result.content_length = length - body_start;
    if (http_send_headers(client_fd, status, reason,
                          type[0] != '\0' ? type : NULL,
                          result.content_length, (time_t)-1, extra) == 0 &&
        strcmp(request->method, "HEAD") != 0) {
        (void)http_write_all(client_fd, output + body_start, result.content_length);
    }
    free(output);
    return result;
}
