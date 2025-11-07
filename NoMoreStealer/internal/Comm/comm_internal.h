#pragma once

#include <fltKernel.h>
#include <ntifs.h>

typedef struct _NOTIFY_WORK_ITEM {
    PIO_WORKITEM WorkItem;
    UNICODE_STRING Path;
    CHAR ProcName[64];
    ULONG Pid;
} NOTIFY_WORK_ITEM, *PNOTIFY_WORK_ITEM;

#define POOL_TAG_COMM 'mCfG'

