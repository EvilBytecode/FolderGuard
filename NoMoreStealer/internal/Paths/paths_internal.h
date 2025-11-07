#pragma once

#include <fltKernel.h>

#define POOL_TAG_PATHS 'tPMM'

#define MAX_PROTECTED_PATHS 128

typedef struct _PROTECTED_PATH {
    UNICODE_STRING Path;
    PVOID Buffer;
} PROTECTED_PATH, *PPROTECTED_PATH;

