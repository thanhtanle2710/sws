#define _POSIX_C_SOURCE 200809L

#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [-dh] [-c dir] [-i address] [-l file] [-p port] dir\n",
            program);
}

static int valid_directory(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

int main(int argc, char **argv)
{
    ServerConfig config = {0};
    char *end;
    long port;
    int option;

    config.port = "8080";
    while ((option = getopt(argc, argv, "c:dhi:l:p:")) != -1) {
        switch (option) {
        case 'c': config.cgi_root = optarg; break;
        case 'd': config.debug = 1; break;
        case 'h': usage(argv[0]); return 0;
        case 'i': config.bind_address = optarg; break;
        case 'l': config.log_file = optarg; break;
        case 'p': config.port = optarg; break;
        default: usage(argv[0]); return 1;
        }
    }
    if (optind + 1 != argc || !valid_directory(argv[optind]) ||
        (config.cgi_root != NULL && !valid_directory(config.cgi_root))) {
        usage(argv[0]);
        return 1;
    }
    port = strtol(config.port, &end, 10);
    if (*config.port == '\0' || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", config.port);
        return 1;
    }
    config.document_root = argv[optind];
    if (run_server(&config) != 0) {
        perror("sws");
        return 1;
    }
    return 0;
}
