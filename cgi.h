#ifndef CGI_H
#define CGI_H

#include "response.h"

HttpResult handle_cgi(int client_fd, const HttpRequest *request,
                      const ServerConfig *config, const char *resource_path);

#endif
