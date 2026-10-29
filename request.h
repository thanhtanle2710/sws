#ifndef REQUEST_H
#define REQUEST_H

#define HTTP_METHOD_SIZE 16
#define HTTP_URI_SIZE 2048
#define HTTP_VERSION_SIZE 16
#define HTTP_DATE_SIZE 128
#define HTTP_HOST_SIZE 256

typedef struct {
    char method[HTTP_METHOD_SIZE];
    char uri[HTTP_URI_SIZE];
    char version[HTTP_VERSION_SIZE];
    char if_modified_since[HTTP_DATE_SIZE];
    char host[HTTP_HOST_SIZE];
} HttpRequest;

/* Trả về 0 nếu yêu cầu hợp lệ, -1 nếu sai định dạng. */
int parse_request(const char *raw, HttpRequest *request);

#endif
