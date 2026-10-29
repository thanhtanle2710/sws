#include "request.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

static int read_token(const char **cursor, char *output, size_t capacity)
{
    size_t length = 0;

    while (**cursor != '\0' && **cursor != ' ' && **cursor != '\t' &&
           **cursor != '\r' && **cursor != '\n') {
        if (length + 1 >= capacity) {
            return -1;
        }
        output[length++] = *(*cursor)++;
    }
    output[length] = '\0';
    return length == 0 ? -1 : 0;
}

static int copy_header_value(char *target, size_t capacity,
                             const char *start, size_t length)
{
    while (length > 0 && (*start == ' ' || *start == '\t')) {
        start++;
        length--;
    }
    while (length > 0 && (start[length - 1] == ' ' || start[length - 1] == '\t')) {
        length--;
    }
    if (length >= capacity) {
        return -1;
    }
    memcpy(target, start, length);
    target[length] = '\0';
    return 0;
}

int parse_request(const char *raw, HttpRequest *request)
{
    const char *cursor;
    const char *line_end;
    const char *colon;
    size_t name_length;

    if (raw == NULL || request == NULL) {
        return -1;
    }
    memset(request, 0, sizeof(*request));
    cursor = raw;
    if (read_token(&cursor, request->method, sizeof(request->method)) != 0 ||
        *cursor != ' ') {
        return -1;
    }
    cursor++;
    if (read_token(&cursor, request->uri, sizeof(request->uri)) != 0 ||
        *cursor != ' ') {
        return -1;
    }
    cursor++;
    if (read_token(&cursor, request->version, sizeof(request->version)) != 0) {
        return -1;
    }
    if (*cursor == '\r') {
        cursor++;
    }
    if (*cursor++ != '\n' || request->uri[0] != '/' ||
        (strcmp(request->version, "HTTP/1.0") != 0 &&
         strcmp(request->version, "HTTP/1.1") != 0)) {
        return -1;
    }

    while (*cursor != '\0') {
        line_end = strchr(cursor, '\n');
        if (line_end == NULL) {
            return -1;
        }
        if (line_end == cursor || (line_end == cursor + 1 && *cursor == '\r')) {
            return 0;
        }
        colon = memchr(cursor, ':', (size_t)(line_end - cursor));
        if (colon == NULL) {
            return -1;
        }
        name_length = (size_t)(colon - cursor);
        if (name_length == 0) {
            return -1;
        }
        if (name_length == 17 && strncasecmp(cursor, "If-Modified-Since", 17) == 0) {
            if (copy_header_value(request->if_modified_since,
                    sizeof(request->if_modified_since), colon + 1,
                    (size_t)(line_end - colon - 1 - (line_end[-1] == '\r'))) != 0) {
                return -1;
            }
        } else if (name_length == 4 && strncasecmp(cursor, "Host", 4) == 0) {
            if (copy_header_value(request->host, sizeof(request->host), colon + 1,
                    (size_t)(line_end - colon - 1 - (line_end[-1] == '\r'))) != 0) {
                return -1;
            }
        }
        cursor = line_end + 1;
    }
    return -1;
}
