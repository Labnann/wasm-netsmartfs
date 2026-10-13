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

static void ini_unescape(char *s)
{
    char *r = s;
    char *w = s;

    while (*r) {
        if (*r == '\\' && r[1]) {
            r++;
        }

        *w++ = *r++;
    }

    *w = '\0';
}

static int ini_parse_line(
    char *line,
    char **type,
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
        ini_unescape(section);

        *type = section;
        return 0;
    }

    char *eq = NULL;

    for (char *p = line; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            continue;
        }

        if (*p == '=') {
            eq = p;
            break;
        }
    }

    if (!eq) {
        char *key = ini_trim(line);
        ini_unescape(key);

        return callback(*type, key, "", userdata);
    }

    *eq = '\0';

    char *key = ini_trim(line);
    char *value = eq + 1;

    ini_unescape(key);
    ini_unescape(value);

    return callback(*type, key, value, userdata);
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

    char *line = malloc(capacity);
    if (!line)
        return -1;

    char *type = "";

    const char *p = input;

    while (*p) {
        const char *start = p;

        while (*p && *p != '\n' && *p != '\r')
            p++;

        size_t n = (size_t)(p - start);

        /* Consume LF, CR, or CRLF. */
        if (*p == '\r') {
            p++;
            if (*p == '\n')
                p++;
        } else if (*p == '\n') {
            p++;
        }

        int has_newline = p > start + n;
        size_t slashes = 0;
        while (slashes < n && start[n - slashes - 1] == '\\')
            slashes++;

        int continuation = has_newline && (slashes % 2 == 1);

        if (continuation)
            n--;

        if (length + n + 1 > capacity) {
            size_t needed = length + n + 1;
            size_t new_capacity = capacity;

            while (new_capacity < needed) {
                if (new_capacity > (size_t)-1 / 2) {
                    free(line);
                    return -1;
                }
                new_capacity *= 2;
            }

            char *tmp = realloc(line, new_capacity);
            if (!tmp) {
                free(line);
                return -1;
            }

            line = tmp;
            capacity = new_capacity;
        }

        memcpy(line + length, start, n);
        length += n;
        line[length] = '\0';

        if (continuation)
            continue;

        int result = ini_parse_line(
            line, &type, callback, userdata);

        if (result) {
            free(line);
            return result;
        }

        length = 0;
    }

    if (length) {
        int result = ini_parse_line(
            line, &type, callback, userdata);

        if (result) {
            free(line);
            return result;
        }
    }

    free(line);
    return 0;
}

