#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *path_info = getenv("PATH_INFO");
    const char *method = getenv("REQUEST_METHOD");
    const char *query = getenv("QUERY_STRING");

    if (path_info == NULL || method == NULL || query == NULL) {
        return 1;
    }
    if (strcmp(path_info, "/redirect") == 0) {
        printf("Location: /\n\n");
    } else if (strcmp(path_info, "/created") == 0) {
        printf("Status: 201 Created\nContent-Type: text/plain\n\nNEW");
    } else if (strcmp(path_info, "/arguments") == 0) {
        printf("Content-Type: text/plain\n\n%s|%s",
               argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "");
    } else {
        printf("Content-Type: text/plain\n\n%s|%s|%s", method, query, path_info);
    }
    return 0;
}
