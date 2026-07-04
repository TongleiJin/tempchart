#include "app_version.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_app_desc.h"

uint32_t app_version_code_from_string(const char *version)
{
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;

    if (version == NULL) {
        return 0;
    }

    while (*version != '\0' && (*version < '0' || *version > '9')) {
        version++;
    }

    if (sscanf(version, "%u.%u.%u", &major, &minor, &patch) < 1) {
        return 0;
    }

    if (major > 99 || minor > 99 || patch > 99) {
        return 0;
    }

    return (uint32_t)(major * 10000U + minor * 100U + patch);
}

int app_version_compare_strings(const char *lhs, const char *rhs)
{
    uint32_t left = app_version_code_from_string(lhs);
    uint32_t right = app_version_code_from_string(rhs);

    if (left > right) {
        return 1;
    }
    if (left < right) {
        return -1;
    }
    return 0;
}

uint32_t app_version_code(void)
{
    return app_version_code_from_string(esp_app_get_description()->version);
}
