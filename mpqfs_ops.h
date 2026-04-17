#pragma once
#include <dokan/dokan.h>
#include "mpqfs.h"

extern MpqFileSystem *g_mpqfs;
extern WCHAR g_volume_label[];

DOKAN_OPERATIONS mpqfs_get_operations();

static NTSTATUS DOKAN_CALLBACK MpqReadFile(LPCWSTR FileName, LPVOID Buffer,
                                           DWORD BufferLength,
                                           LPDWORD ReadLength, LONGLONG Offset,
                                           PDOKAN_FILE_INFO DokanFileInfo);
