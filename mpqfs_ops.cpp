#include "mpqfs_ops.h"
#include <stdio.h>
#include "log_macros.h"
#include <vector>

struct MpqFileContext {
  MpqFileInfo info;
  HANDLE hOpened;
};

static std::string WideToUtf8(LPCWSTR wide) {
  if (!wide)
    return std::string();
  int len =
      WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 0)
    return std::string();
  std::vector<char> buf(len);
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, buf.data(), len, nullptr, nullptr);
  return std::string(buf.data());
}

static std::wstring Utf8ToWide(const std::string &utf8) {
  if (utf8.empty())
    return std::wstring();
  int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (len <= 0)
    return std::wstring();
  std::vector<wchar_t> buf(len);
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, buf.data(), len);
  return std::wstring(buf.data());
}

static NTSTATUS DOKAN_CALLBACK
MpqCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
              ACCESS_MASK DesiredAccess, ULONG FileAttributes,
              ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions,
              PDOKAN_FILE_INFO DokanFileInfo) {
  std::string path = WideToUtf8(FileName);
  if (path.empty())
    path = "";
  std::string norm = g_mpqfs->NormalizePath(path);

  if (norm.empty()) {
    DokanFileInfo->IsDirectory = TRUE;
    return STATUS_SUCCESS;
  }
  MpqFileInfo info = g_mpqfs->LookupFile(norm);
  bool exists = info.index!=-1;

  if (CreateOptions & FILE_DIRECTORY_FILE) {
    if (CreateDisposition == FILE_CREATE || CreateDisposition == FILE_OPEN_IF) {
      if (CreateDisposition == FILE_CREATE && exists && !info.is_directory)
        return STATUS_OBJECT_NAME_COLLISION;
    }
    if (!exists)
      return STATUS_OBJECT_NAME_NOT_FOUND;
    if (!info.is_directory)
      return STATUS_NOT_A_DIRECTORY;
    DokanFileInfo->IsDirectory = TRUE;
    return STATUS_SUCCESS;
  }

  if (CreateDisposition == FILE_CREATE ||
      CreateDisposition == FILE_OVERWRITE_IF) {
    return STATUS_MEDIA_WRITE_PROTECTED;
  }

  if (!exists) {
    LOG_PRINT(L"CreateFile: NOT FOUND path=%S norm=%S\n", path.c_str(),
            norm.c_str());
    return STATUS_OBJECT_NAME_NOT_FOUND;
  }

  if (info.is_directory) {
    if (CreateOptions & FILE_NON_DIRECTORY_FILE)
      return STATUS_FILE_IS_A_DIRECTORY;
    DokanFileInfo->IsDirectory = TRUE;
    return STATUS_SUCCESS;
  }

  if (DesiredAccess & GENERIC_WRITE)
    return STATUS_MEDIA_WRITE_PROTECTED;

  auto ctx = new MpqFileContext{info};
  DokanFileInfo->Context = (ULONG64)ctx;
  DokanFileInfo->IsDirectory = info.is_directory;

  return STATUS_SUCCESS;
}

static void DOKAN_CALLBACK MpqCleanup(LPCWSTR FileName,
                                      PDOKAN_FILE_INFO DokanFileInfo) {
  if (DokanFileInfo->Context) {
    MpqFileContext *ctx = (MpqFileContext *)DokanFileInfo->Context;
    delete ctx;
    DokanFileInfo->Context = 0;
    LOG_PRINT(L"MpqCleanup: clean context for %s\n", FileName);
  }
}

static void DOKAN_CALLBACK MpqCloseFile(LPCWSTR FileName,
                                        PDOKAN_FILE_INFO DokanFileInfo) {
  if (DokanFileInfo->Context) {
    MpqFileContext *ctx = (MpqFileContext *)DokanFileInfo->Context;
    HANDLE hFile = ctx->hOpened;
    delete ctx;
    DokanFileInfo->Context = 0;
    if (hFile) {
      g_mpqfs->CloseFile(hFile);
    }
    LOG_PRINT(L"MpqCloseFile: close handle for %s\n", FileName);
  }
}

static NTSTATUS DOKAN_CALLBACK MpqReadFile(LPCWSTR FileName, LPVOID Buffer,
                                           DWORD BufferLength,
                                           LPDWORD ReadLength, LONGLONG Offset,
                                           PDOKAN_FILE_INFO DokanFileInfo) {
  MpqFileContext *ctx = (MpqFileContext *)DokanFileInfo->Context;
  if (!ctx) {
    std::string path = WideToUtf8(FileName);
    if (path.empty())
      path = "";
    std::string norm = g_mpqfs->NormalizePath(path);
    MpqFileInfo info;
    if (g_mpqfs->GetFileInfo(norm, info)) {
      MpqFileContext* ctx;
      ctx = new MpqFileContext{info};
      DokanFileInfo->Context = (ULONG64)ctx;
    }
  }
  HANDLE hFile =  g_mpqfs->OpenFile(ctx->info);
  if (!hFile) {
    return STATUS_OBJECT_NAME_NOT_FOUND;
  }
  ctx->hOpened = hFile;
  DWORD bytesRead = 0;
      g_mpqfs->ReadFileData(hFile, Buffer, BufferLength, bytesRead, Offset);
  LOG_PRINT(L"ReadFile: bytesRead %d %s\n", bytesRead, FileName);
  
  *ReadLength = bytesRead;
  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MpqWriteFile(LPCWSTR FileName, LPCVOID Buffer,
                                            DWORD NumberOfBytesToWrite,
                                            LPDWORD NumberOfBytesWritten,
                                            LONGLONG Offset,
                                            PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS DOKAN_CALLBACK
MpqGetFileInformation(LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION Buffer,
                      PDOKAN_FILE_INFO DokanFileInfo) {
  MpqFileInfo info;

  MpqFileContext *ctx = (MpqFileContext *)DokanFileInfo->Context;
  if (ctx) {
    info = ctx->info;
  } else {
    std::string path = WideToUtf8(FileName);
    std::string norm = g_mpqfs->NormalizePath(path);

    if (norm.empty()) {
      Buffer->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
      Buffer->ftCreationTime = {};
      Buffer->ftLastAccessTime = {};
      Buffer->ftLastWriteTime = {};
      Buffer->nFileSizeHigh = 0;
      Buffer->nFileSizeLow = 0;
      return STATUS_SUCCESS;
    }

    if (!g_mpqfs->GetFileInfo(norm, info)) {
      return STATUS_OBJECT_NAME_NOT_FOUND;
    }
  }

  Buffer->dwFileAttributes =
      info.is_directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
  if (!info.is_directory) {
    Buffer->dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
  }
  Buffer->ftCreationTime = info.file_time;
  Buffer->ftLastAccessTime = info.file_time;
  Buffer->ftLastWriteTime = info.file_time;
  Buffer->nFileSizeHigh = 0;
  Buffer->nFileSizeLow = info.file_size;

  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MpqFindFiles(LPCWSTR FileName,
                                            PFillFindData FillFindData,
                                            PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK MpqFindFilesWithPattern(
    LPCWSTR PathName, LPCWSTR SearchPattern, PFillFindData FillFindData,
    PDOKAN_FILE_INFO DokanFileInfo) {
  std::string path = WideToUtf8(PathName);
  std::vector<MpqFileInfo> files;

  if (!g_mpqfs->EnumerateDirectory(path, files)) {
    return STATUS_OBJECT_NAME_NOT_FOUND;
  }

  std::wstring wpattern(SearchPattern);

  for (auto &fi : files) {
    std::string display_name = fi.name;
    auto pos = display_name.find_last_of('\\');
    std::string leaf_name = (pos != std::string::npos)
                                ? display_name.substr(pos + 1)
                                : display_name;

    if (wpattern != L"*" && wpattern != L"*.*") {
      std::wstring wleaf = Utf8ToWide(leaf_name);
      if (!DokanIsNameInExpression(wpattern.c_str(), wleaf.c_str(), FALSE))
        continue;
    }

    WIN32_FIND_DATAW findData = {};
    std::wstring wname = Utf8ToWide(leaf_name);
    wcsncpy_s(findData.cFileName, wname.c_str(), wname.length());
    findData.nFileSizeLow = fi.is_directory ? 0 : fi.file_size;
    findData.nFileSizeHigh = 0;
    findData.dwFileAttributes =
        fi.is_directory ? FILE_ATTRIBUTE_DIRECTORY
                        : (FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY);
    findData.ftCreationTime = fi.file_time;
    findData.ftLastAccessTime = fi.file_time;
    findData.ftLastWriteTime = fi.file_time;

    FillFindData(&findData, DokanFileInfo);
  }

  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MpqSetFileAttributes(
    LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS DOKAN_CALLBACK MpqSetFileTime(LPCWSTR FileName,
                                              CONST FILETIME *CreationTime,
                                              CONST FILETIME *LastAccessTime,
                                              CONST FILETIME *LastWriteTime,
                                              PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS DOKAN_CALLBACK MpqDeleteFile(LPCWSTR FileName,
                                             PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS DOKAN_CALLBACK
MpqDeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS DOKAN_CALLBACK MpqMoveFile(LPCWSTR FileName,
                                           LPCWSTR NewFileName,
                                           BOOL ReplaceIfExisting,
                                           PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS DOKAN_CALLBACK MpqSetEndOfFile(LPCWSTR FileName,
                                               LONGLONG ByteOffset,
                                               PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS DOKAN_CALLBACK MpqSetAllocationSize(
    LPCWSTR FileName, LONGLONG AllocSize, PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS DOKAN_CALLBACK MpqLockFile(LPCWSTR FileName,
                                           LONGLONG ByteOffset, LONGLONG Length,
                                           PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MpqUnlockFile(LPCWSTR FileName,
                                             LONGLONG ByteOffset,
                                             LONGLONG Length,
                                             PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MpqGetDiskFreeSpace(
    PULONGLONG FreeBytesAvailable, PULONGLONG TotalNumberOfBytes,
    PULONGLONG TotalNumberOfFreeBytes, PDOKAN_FILE_INFO DokanFileInfo) {
  ULONGLONG total = g_mpqfs->GetTotalSize();
  ULONGLONG free = 0;
  *FreeBytesAvailable = free;
  *TotalNumberOfBytes = total;
  *TotalNumberOfFreeBytes = free;
  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MpqGetVolumeInformation(
    LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
    LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags,
    LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
    PDOKAN_FILE_INFO DokanFileInfo) {
  wcscpy_s(VolumeNameBuffer, VolumeNameSize, g_volume_label);
  if (VolumeSerialNumber)
    *VolumeSerialNumber = 0x4D505131;
  if (MaximumComponentLength)
    *MaximumComponentLength = 255;
  if (FileSystemFlags)
    *FileSystemFlags = FILE_CASE_PRESERVED_NAMES | FILE_UNICODE_ON_DISK |
                       FILE_READ_ONLY_VOLUME;
  wcscpy_s(FileSystemNameBuffer, FileSystemNameSize, L"NTFS");
  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MpqMounted(LPCWSTR MountPoint,
                                          PDOKAN_FILE_INFO DokanFileInfo) {
  LOG_PRINT(L"Mounted at: %s\n", MountPoint);
  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MpqUnmounted(PDOKAN_FILE_INFO DokanFileInfo) {
  LOG_PRINT(L"Unmounted\n");
  return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
MpqGetFileSecurity(LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
                   PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength,
                   PULONG LengthNeeded, PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK
MpqSetFileSecurity(LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
                   PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength,
                   PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS DOKAN_CALLBACK
MpqFindStreams(LPCWSTR FileName, PFillFindStreamData FillFindStreamData,
               PVOID FindStreamContext, PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK
MpqFlushFileBuffers(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
  return STATUS_SUCCESS;
}

DOKAN_OPERATIONS mpqfs_get_operations() {
  DOKAN_OPERATIONS ops = {};
  ops.ZwCreateFile = MpqCreateFile;
  ops.Cleanup = MpqCleanup;
  ops.CloseFile = MpqCloseFile;
  ops.ReadFile = MpqReadFile;
  ops.WriteFile = MpqWriteFile;
  ops.FlushFileBuffers = MpqFlushFileBuffers;
  ops.GetFileInformation = MpqGetFileInformation;
  ops.FindFiles = MpqFindFiles;
  ops.FindFilesWithPattern = MpqFindFilesWithPattern;
  ops.SetFileAttributes = MpqSetFileAttributes;
  ops.SetFileTime = MpqSetFileTime;
  ops.DeleteFile = MpqDeleteFile;
  ops.DeleteDirectory = MpqDeleteDirectory;
  ops.MoveFile = MpqMoveFile;
  ops.SetEndOfFile = MpqSetEndOfFile;
  ops.SetAllocationSize = MpqSetAllocationSize;
  ops.LockFile = MpqLockFile;
  ops.UnlockFile = MpqUnlockFile;
  ops.GetDiskFreeSpace = MpqGetDiskFreeSpace;
  ops.GetVolumeInformation = MpqGetVolumeInformation;
  ops.Mounted = MpqMounted;
  ops.Unmounted = MpqUnmounted;
  ops.GetFileSecurity = MpqGetFileSecurity;
  ops.SetFileSecurity = MpqSetFileSecurity;
  ops.FindStreams = MpqFindStreams;
  return ops;
}