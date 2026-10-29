#ifndef SERVER_H
#define SERVER_H

typedef struct {
    const char *document_root;
    const char *cgi_root;
    const char *bind_address;
    const char *log_file;
    const char *port;
    int debug;
} ServerConfig;

int run_server(const ServerConfig *config);

#endif
