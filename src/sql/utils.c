#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "datatype.h"

char* toLowerCase(const char* str) {
    u32 len = strlen(str);
    char* result = malloc(len + 1);
    for (int i = 0; i < len; i++) {
        result[i] = tolower(str[i]);
    }
    result[len] = '\0';
    return result;
}
