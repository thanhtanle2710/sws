#define _XOPEN_SOURCE 700

#include "response.h"
#include "cgi.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PATH_BUFFER_SIZE 4096

int http_write_all(int fd, const void *data, size_t length)
{
    const char *bytes = data;
    while (length > 0) {
        ssize_t written = write(fd, bytes, length);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return -1;
        }
        bytes += written;
        length -= (size_t)written;
    }
    return 0;
}

static void http_date(time_t when, char *output, size_t capacity)
{
    struct tm utc;
    if (gmtime_r(&when, &utc) == NULL ||
        strftime(output, capacity, "%a, %d %b %Y %H:%M:%S GMT", &utc) == 0) {
        output[0] = '\0';
    }
}

int http_send_headers(int fd, int status, const char *reason,
                      const char *type, size_t length, time_t modified,
                      const char *extra)
{
    char header[2048];
    char date[64];
    char last_modified[64];
    int count;

    http_date(time(NULL), date, sizeof(date));
    if (modified != (time_t)-1) {
        http_date(modified, last_modified, sizeof(last_modified));
    } else {
        last_modified[0] = '\0';
    }
    count = snprintf(header, sizeof(header),
        "HTTP/1.0 %d %s\r\nDate: %s\r\nServer: sws/0.1\r\n"
        "%s%s%s%s%s%sContent-Length: %zu\r\nConnection: close\r\n%s\r\n",
        status, reason, date,
        last_modified[0] ? "Last-Modified: " : "", last_modified,
        last_modified[0] ? "\r\n" : "",
        type != NULL ? "Content-Type: " : "", type != NULL ? type : "",
        type != NULL ? "\r\n" : "",
        length, extra != NULL ? extra : "");
    if (count < 0 || count >= (int)sizeof(header)) {
        return -1;
    }
    return http_write_all(fd, header, (size_t)count);
}

HttpResult send_error_response(int fd, int status, const char *reason, int head_only)
{
    char body[256];
    int length = snprintf(body, sizeof(body), "%d %s\n", status, reason);
    HttpResult result = {status, 0};

    if (length < 0 || length >= (int)sizeof(body)) {
        return result;
    }
    result.content_length = (size_t)length;
    if (http_send_headers(fd, status, reason, "text/plain", result.content_length,
                     (time_t)-1, NULL) == 0 && !head_only) {
        (void)http_write_all(fd, body, result.content_length);
    }
    return result;
}

static int parent_component(const char *path)
{
    const char *part = path;
    while (*part != '\0') {
        while (*part == '/') part++;
        if (part[0] == '.' && part[1] == '.' &&
            (part[2] == '/' || part[2] == '\0')) return 1;
        while (*part != '\0' && *part != '/') part++;
    }
    return 0;
}

static int within_root(const char *root, const char *path)
{
    size_t length = strlen(root);
    if (strcmp(root, "/") == 0) return path[0] == '/';
    return strncmp(root, path, length) == 0 &&
           (path[length] == '/' || path[length] == '\0');
}

static int resolve_path(const char *root, const char *uri_path,
                        char *resolved, size_t capacity)
{
    char canonical_root[PATH_BUFFER_SIZE];
    char joined[PATH_BUFFER_SIZE];
    int length;

    if (parent_component(uri_path)) return 403;
    if (realpath(root, canonical_root) == NULL) return 500;
    length = snprintf(joined, sizeof(joined), "%s%s", root, uri_path);
    if (length < 0 || length >= (int)sizeof(joined)) return 414;
    if (realpath(joined, resolved) == NULL) {
        return errno == EACCES ? 403 : 404;
    }
    if (strlen(resolved) >= capacity || !within_root(canonical_root, resolved)) {
        return 403;
    }
    return 0;
}

static int compare_dates(const struct tm *left, const struct tm *right)
{
    if (left->tm_year != right->tm_year) return left->tm_year - right->tm_year;
    if (left->tm_mon != right->tm_mon) return left->tm_mon - right->tm_mon;
    if (left->tm_mday != right->tm_mday) return left->tm_mday - right->tm_mday;
    if (left->tm_hour != right->tm_hour) return left->tm_hour - right->tm_hour;
    if (left->tm_min != right->tm_min) return left->tm_min - right->tm_min;
    return left->tm_sec - right->tm_sec;
}

static int not_modified(const char *header, time_t modified)
{
    struct tm requested = {0};
    struct tm file_time;
    struct tm now_time;
    char *end;
    time_t now = time(NULL);

    if (*header == '\0') return 0;
    end = strptime(header, "%a, %d %b %Y %H:%M:%S GMT", &requested);
    if (end == NULL || *end != '\0') {
        memset(&requested, 0, sizeof(requested));
        end = strptime(header, "%A, %d-%b-%y %H:%M:%S GMT", &requested);
    }
    if (end == NULL || *end != '\0') {
        memset(&requested, 0, sizeof(requested));
        end = strptime(header, "%a %b %e %H:%M:%S %Y", &requested);
    }
    if (end == NULL || *end != '\0' ||
        gmtime_r(&modified, &file_time) == NULL ||
        gmtime_r(&now, &now_time) == NULL) return 0;
    return compare_dates(&requested, &now_time) <= 0 &&
           compare_dates(&file_time, &requested) <= 0;
}

static const char *fallback_type(const char *path)
{
    unsigned char sample[512];
    FILE *file = fopen(path, "rb");
    size_t count = 0;
    size_t index;
    const char *extension = strrchr(path, '.');

    if (file != NULL) {
        count = fread(sample, 1, sizeof(sample), file);
        fclose(file);
    }
    if (count >= 4 && memcmp(sample, "%PDF", 4) == 0) return "application/pdf";
    if (count >= 8 && memcmp(sample, "\211PNG\r\n\032\n", 8) == 0) return "image/png";
    if (count >= 3 && memcmp(sample, "\377\330\377", 3) == 0) return "image/jpeg";
    if (count >= 6 && (memcmp(sample, "GIF87a", 6) == 0 ||
                       memcmp(sample, "GIF89a", 6) == 0)) return "image/gif";
    if (count >= 5 && memcmp(sample, "<html", 5) == 0) return "text/html";
    if (count >= 9 && memcmp(sample, "<!doctype", 9) == 0) return "text/html";
    if (extension == NULL) extension = "";
    if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) return "text/html";
    if (strcmp(extension, ".txt") == 0) return "text/plain";
    if (strcmp(extension, ".css") == 0) return "text/css";
    if (strcmp(extension, ".js") == 0) return "application/javascript";
    if (strcmp(extension, ".png") == 0) return "image/png";
    if (strcmp(extension, ".jpg") == 0 || strcmp(extension, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(extension, ".pdf") == 0) return "application/pdf";
    for (index = 0; index < count; index++) {
        if (sample[index] == 0 ||
            (sample[index] < 32 && sample[index] != '\n' &&
             sample[index] != '\r' && sample[index] != '\t')) {
            return "application/octet-stream";
        }
    }
    if (count > 0) return "text/plain";
    return "application/octet-stream";
}

static void detect_type(const char *path, char *output, size_t capacity)
{
    int pipe_fd[2];
    pid_t child;
    ssize_t count;
    int status;

    (void)snprintf(output, capacity, "%s", fallback_type(path));
    if (pipe(pipe_fd) != 0) return;
    child = fork();
    if (child < 0) {
        close(pipe_fd[0]); close(pipe_fd[1]); return;
    }
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        close(pipe_fd[0]);
        (void)dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        execlp("file", "file", "-b", "-i", "--", path, (char *)NULL);
        _exit(127);
    }
    close(pipe_fd[1]);
    count = read(pipe_fd[0], output, capacity - 1);
    close(pipe_fd[0]);
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || count <= 0) {
        (void)snprintf(output, capacity, "%s", fallback_type(path));
        return;
    }
    output[count] = '\0';
    output[strcspn(output, "\r\n")] = '\0';
    {
        size_t media_length = strcspn(output, ";");
        if (memchr(output, '/', media_length) == NULL ||
            memchr(output, ' ', media_length) != NULL) {
            (void)snprintf(output, capacity, "%s", fallback_type(path));
        }
    }
}

static HttpResult serve_file(int fd, const char *path, const HttpRequest *request)
{
    char type[256];
    char buffer[8192];
    struct stat info;
    FILE *file;
    size_t count;
    HttpResult result = {200, 0};
    int head_only = strcmp(request->method, "HEAD") == 0;

    file = fopen(path, "rb");
    if (file == NULL) return send_error_response(fd, errno == EACCES ? 403 : 404,
        errno == EACCES ? "Forbidden" : "Not Found", head_only);
    if (fstat(fileno(file), &info) != 0 || !S_ISREG(info.st_mode)) {
        fclose(file);
        return send_error_response(fd, 403, "Forbidden", head_only);
    }
    if (strcmp(request->method, "GET") == 0 &&
        not_modified(request->if_modified_since, info.st_mtime)) {
        result.status = 304;
        (void)http_send_headers(fd, 304, "Not Modified", NULL, 0, info.st_mtime, NULL);
        fclose(file);
        return result;
    }
    detect_type(path, type, sizeof(type));
    result.content_length = (size_t)info.st_size;
    if (http_send_headers(fd, 200, "OK", type, result.content_length,
                     info.st_mtime, NULL) == 0 && !head_only) {
        while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            if (http_write_all(fd, buffer, count) != 0) break;
        }
    }
    fclose(file);
    return result;
}

static int compare_names(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static void write_escaped(FILE *file, const char *name)
{
    for (; *name != '\0'; name++) {
        if (*name == '&') fputs("&amp;", file);
        else if (*name == '<') fputs("&lt;", file);
        else if (*name == '>') fputs("&gt;", file);
        else if (*name == '"') fputs("&quot;", file);
        else fputc(*name, file);
    }
}

static HttpResult serve_index(int fd, const char *path, const HttpRequest *request)
{
    DIR *directory = opendir(path);
    struct dirent *entry;
    char **names = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t index;
    FILE *body;
    long length;
    char buffer[4096];
    HttpResult result = {200, 0};
    struct stat info;

    if (directory == NULL) return send_error_response(fd, 403, "Forbidden",
        strcmp(request->method, "HEAD") == 0);
    while ((entry = readdir(directory)) != NULL) {
        char **grown;
        if (entry->d_name[0] == '.') continue;
        if (count == capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            grown = realloc(names, capacity * sizeof(*names));
            if (grown == NULL) break;
            names = grown;
        }
        names[count] = strdup(entry->d_name);
        if (names[count] == NULL) break;
        count++;
    }
    closedir(directory);
    qsort(names, count, sizeof(*names), compare_names);
    body = tmpfile();
    if (body == NULL) {
        for (index = 0; index < count; index++) free(names[index]);
        free(names);
        return send_error_response(fd, 500, "Internal Server Error", 0);
    }
    fputs("<!doctype html><html><body><h1>Index</h1><ul>\n", body);
    for (index = 0; index < count; index++) {
        fputs("<li><a href=\"", body);
        write_escaped(body, names[index]);
        fputs("\">", body);
        write_escaped(body, names[index]);
        fputs("</a></li>\n", body);
        free(names[index]);
    }
    free(names);
    fputs("</ul></body></html>\n", body);
    length = ftell(body);
    rewind(body);
    if (length >= 0) {
        result.content_length = (size_t)length;
        if (stat(path, &info) != 0) info.st_mtime = (time_t)-1;
        if (http_send_headers(fd, 200, "OK", "text/html", result.content_length,
                         info.st_mtime, NULL) == 0 && strcmp(request->method, "HEAD") != 0) {
            while ((count = fread(buffer, 1, sizeof(buffer), body)) > 0) {
                if (http_write_all(fd, buffer, count) != 0) break;
            }
        }
    } else {
        result = send_error_response(fd, 500, "Internal Server Error",
                                     strcmp(request->method, "HEAD") == 0);
    }
    fclose(body);
    return result;
}

static HttpResult handle_static(int fd, const HttpRequest *request,
                                const char *root, const char *uri_path)
{
    char path[PATH_BUFFER_SIZE];
    char index_path[PATH_BUFFER_SIZE];
    char canonical_index[PATH_BUFFER_SIZE];
    char canonical_root[PATH_BUFFER_SIZE];
    struct stat info;
    int error = resolve_path(root, uri_path, path, sizeof(path));
    int head_only = strcmp(request->method, "HEAD") == 0;

    if (error != 0) return send_error_response(fd, error,
        error == 403 ? "Forbidden" : error == 414 ? "URI Too Long" :
        error == 500 ? "Internal Server Error" : "Not Found", head_only);
    if (stat(path, &info) != 0) return send_error_response(fd, 404, "Not Found", head_only);
    if (!S_ISDIR(info.st_mode)) return serve_file(fd, path, request);
    if (snprintf(index_path, sizeof(index_path), "%s/index.html", path) >=
        (int)sizeof(index_path)) return send_error_response(fd, 414, "URI Too Long", head_only);
    if (stat(index_path, &info) == 0 && S_ISREG(info.st_mode)) {
        if (realpath(index_path, canonical_index) == NULL ||
            realpath(root, canonical_root) == NULL ||
            !within_root(canonical_root, canonical_index)) {
            return send_error_response(fd, 403, "Forbidden", head_only);
        }
        return serve_file(fd, index_path, request);
    }
    return serve_index(fd, path, request);
}

HttpResult handle_request(int fd, const HttpRequest *request,
                          const ServerConfig *config)
{
    char uri_path[HTTP_URI_SIZE];
    char user_root[PATH_BUFFER_SIZE];
    const char *root = config->document_root;
    const char *relative;
    const char *slash;
    struct passwd *user;
    size_t length = strcspn(request->uri, "?#");
    int head_only = strcmp(request->method, "HEAD") == 0;

    if (strcmp(request->method, "GET") != 0 && !head_only) {
        return send_error_response(fd, 501, "Not Implemented", 0);
    }
    memcpy(uri_path, request->uri, length);
    uri_path[length] = '\0';
    if (parent_component(uri_path)) return send_error_response(fd, 403, "Forbidden", head_only);
    if (strncmp(uri_path, "/cgi-bin", 8) == 0 &&
        (uri_path[8] == '/' || uri_path[8] == '\0') && config->cgi_root != NULL) {
        return handle_cgi(fd, request, config, uri_path + 8);
    }
    relative = uri_path;
    if (strncmp(uri_path, "/~", 2) == 0) {
        slash = strchr(uri_path + 2, '/');
        length = slash != NULL ? (size_t)(slash - (uri_path + 2)) : strlen(uri_path + 2);
        if (length == 0 || length > 255) return send_error_response(fd, 404, "Not Found", head_only);
        memcpy(user_root, uri_path + 2, length);
        user_root[length] = '\0';
        user = getpwnam(user_root);
        if (user == NULL) return send_error_response(fd, 404, "Not Found", head_only);
        if (snprintf(user_root, sizeof(user_root), "%s/sws", user->pw_dir) >=
            (int)sizeof(user_root)) return send_error_response(fd, 414, "URI Too Long", head_only);
        root = user_root;
        relative = slash != NULL ? slash : "/";
    }
    return handle_static(fd, request, root, relative);
}
