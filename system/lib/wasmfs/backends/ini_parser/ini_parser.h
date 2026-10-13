
#ifndef INI_H
#define INI_H

typedef int (*ini_callback)(
    const char *type,
    const char *key,
    const char *value,
    void *userdata
);

/*
 * Parse INI text:
 *   [TYPE]
 *   KEY=VALUE
 *
 * Backslash escapes the next character.
 * Backslash followed by a newline continues the value.
 *
 * Returns 0 on success, -1 on parse/allocation error,
 * or the nonzero value returned by the callback.
 */
int ini_parse(
    const char *input,
    ini_callback callback,
    void *userdata
);

#endif /* INI_H */

