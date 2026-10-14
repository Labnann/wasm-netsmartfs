#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ini_parser.h"

static char *ini_trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;

    char *end = s + strlen(s);

    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';

    return s;
}

static int ini_parse_line(
    char *line,
    char *type,
    ini_callback callback,
    void *userdata)
{
    line = ini_trim(line);

    if (!*line)
        return 0;

    if (line[0] == '[') {
        size_t len = strlen(line);

        if (len < 2 || line[len - 1] != ']')
            return -1;

        line[len - 1] = '\0';

        char *section = ini_trim(line + 1);
        memmove(line, section, strlen(section) + 1);

        return 1;
    }

    char *eq = strchr(line, '=');

    if (!eq)
        return callback(type, line, "", userdata);

    *eq = '\0';

    char *key = ini_trim(line);
    char *value = ini_trim(eq + 1);

    return callback(type, key, value, userdata);
}

int ini_parse(
    const char *input,
    ini_callback callback,
    void *userdata)
{
    if (!input || !callback)
        return -1;

    size_t capacity = 128;
    size_t length = 0;

    char *line = (char *)malloc(capacity);
    if (!line)
        return -1;

    size_t type_capacity = 128;
    char *type = (char *)malloc(type_capacity);
    if (!type) {
        free(line);
        return -1;
    }

    type[0] = '\0';

    const char *p = input;

    while (*p) {
        const char *start = p;

        while (*p && *p != '\n' && *p != '\r')
            p++;

        size_t n = (size_t)(p - start);

        if (*p == '\r') {
            p++;
            if (*p == '\n')
                p++;
        } else if (*p == '\n') {
            p++;
        }

        if (length + n + 1 > capacity) {
            size_t needed = length + n + 1;
            size_t new_capacity = capacity;

            while (new_capacity < needed) {
                if (new_capacity > (size_t)-1 / 2) {
                    free(type);
                    free(line);
                    return -1;
                }
                new_capacity *= 2;
            }

            char *tmp = (char *)realloc(line, new_capacity);
            if (!tmp) {
                free(type);
                free(line);
                return -1;
            }

            line = tmp;
            capacity = new_capacity;
        }

        memcpy(line + length, start, n);
        length += n;
        line[length] = '\0';

        if (*p && length == 0)
            continue;

        if (p > start + n && length == 0)
            continue;

        int result = ini_parse_line(line, type, callback, userdata);

        if (result == 1) {
            char *section = ini_trim(line);
            size_t section_length = strlen(section);

            if (section_length + 1 > type_capacity) {
                size_t new_capacity = type_capacity;

                while (new_capacity < section_length + 1) {
                    if (new_capacity > (size_t)-1 / 2) {
                        free(type);
                        free(line);
                        return -1;
                    }
                    new_capacity *= 2;
                }

                char *tmp = (char *)realloc(type, new_capacity);
                if (!tmp) {
                    free(type);
                    free(line);
                    return -1;
                }

                type = tmp;
                type_capacity = new_capacity;
            }

            memcpy(type, section, section_length + 1);
        } else if (result != 0) {
            free(type);
            free(line);
            return result;
        }

        length = 0;
    }

    free(type);
    free(line);
    return 0;
}
