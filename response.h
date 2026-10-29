#ifndef RESPONSE_H
#define RESPONSE_H

#include "request.h"
#include "server.h"
#include <stddef.h>
#include <time.h>

typedef struct {
    int status;
    size_t content_length;
} HttpResult;

HttpResult send_error_response(int client_fd, int status, const char *reason,
                               int head_only);
int http_write_all(int fd, const void *data, size_t length);
int http_send_headers(int fd, int status, const char *reason,
                      const char *type, size_t length, time_t modified,
                      const char *extra);
HttpResult handle_request(int client_fd, const HttpRequest *request,
                          const ServerConfig *config);

#endif
