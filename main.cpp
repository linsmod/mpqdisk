#include "mpqdisk_config.h"
#include "mpqfs.h"
#include "mpqfs_ops.h"

#include <dokan/dokan.h>

#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

static DOKAN_HANDLE g_dokan_handle = nullptr;
WCHAR g_volume_label[MAX_PATH + 1] = L"MPQDisk";

static void ShowUsage() {
  fwprintf(stderr,
           L"mpqdisk.exe - Mount MPQ archives as a virtual disk using Dokan\n"
           L"\n"
           L"Copyright linsmod<at>qq.com\n"
           L"github:linsmod:mpqdisk\n"
           L"\n"
           L"Usage:\n"
           L"  mpqdisk.exe --mount --config <config_file>\n"
           L"  mpqdisk.exe --mount --files war3.mpq War3Local.mpq War3x.mpq "
           L"War3xlocal.mpq\n"
           L"  mpqdisk.exe --mount --files war3.mpq War3Local.mpq --volume Z: "
           L"MyLabel\n"
           L"  mpqdisk.exe --umount M:\n"
           L"\n"
           L"Options:\n"
           L"  --mount           Mount the filesystem (required)\n"
           L"  --config <path>   Path to config file\n"
           L"  --files <list>    List of MPQ files to mount (overlay mode, "
           L"last has highest priority)\n"
            L"  --volume <X:> [label]  Volume letter and optional label "
            L"(default: first available, label: MPQDisk)\n"
            L"  --umount <X:>     Unmount the filesystem at the specified drive\n"
            L"  --debug           Enable debug output\n"
           L"  --help            Show this help\n");
}

static MpqFileSystem g_mpqfs_obj;
MpqFileSystem *g_mpqfs = &g_mpqfs_obj;

BOOL WINAPI CtrlHandler(DWORD dwCtrlType) {
  switch (dwCtrlType) {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT:
  case CTRL_CLOSE_EVENT:
  case CTRL_LOGOFF_EVENT:
  case CTRL_SHUTDOWN_EVENT:
    wprintf(L"Unmounting...\n");
    if (g_dokan_handle) {
      DokanCloseHandle(g_dokan_handle);
      g_dokan_handle = nullptr;
    }
    return TRUE;
  default:
    return FALSE;
  }
}

int __cdecl wmain(int argc, wchar_t *argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  _setmode(_fileno(stdout), _O_U16TEXT);
  _setmode(_fileno(stderr), _O_U16TEXT);

  bool do_mount = false;
  bool do_umount = false;
  bool use_config = false;
  bool debug_mode = false;
  std::wstring config_path;
  std::wstring umount_point;
  std::wstring volume_letter;
  std::wstring volume_label = L"MPQDisk";
  std::vector<std::wstring> mpq_files;

  for (int i = 1; i < argc; i++) {
    std::wstring arg = argv[i];
    if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
      ShowUsage();
      return 0;
    } else if (arg == L"--mount") {
      do_mount = true;
    } else if (arg == L"--config") {
      use_config = true;
      if (i + 1 < argc)
        config_path = argv[++i];
    } else if (arg == L"--files") {
      while (i + 1 < argc && argv[i + 1][0] != L'-') {
        WCHAR full_path[MAX_PATH];
        GetFullPathNameW(argv[++i], MAX_PATH, full_path, nullptr);
        mpq_files.push_back(full_path);
      }
    } else if (arg == L"--volume") {
      if (i + 1 < argc) {
        volume_letter = argv[++i];
        if (i + 1 < argc && argv[i + 1][0] != L'-') {
          volume_label = argv[++i];
        }
      }
    } else if (arg == L"--debug") {
      debug_mode = true;
    } else if (arg == L"--umount") {
      do_umount = true;
      if (i + 1 < argc) {
        umount_point = argv[++i];
        if (umount_point.length() > 0 && umount_point.back() != L':') {
          umount_point += L':';
        }
        if (umount_point.length() < 2 || umount_point[1] != L':') {
          fwprintf(stderr, L"Error: Invalid drive letter for --umount\n");
          return 1;
        }
      } else {
        fwprintf(stderr, L"Error: --umount requires a drive letter\n");
        return 1;
      }
    }
  }

  if (do_umount) {
    DokanInit();
    bool success = DokanRemoveMountPoint(umount_point.c_str());
    if (success)
        wprintf(L"Unmounted %s\n", umount_point.c_str());
    else
        fwprintf(stderr, L"Error: Failed to unmount %s\n", umount_point.c_str());
    DokanShutdown();
    return 0;
  }

  if (!do_mount) {
    fwprintf(stderr, L"Error: --mount is required\n\n");
    ShowUsage();
    return 1;
  }

  MpqDiskConfig config;

  if (use_config && !config_path.empty()) {
    config = parse_config_file(config_path);
  } else if (!mpq_files.empty()) {
    config.mpq_files = mpq_files;
    config.volume_letter = volume_letter;
    config.volume_label = volume_label;
  } else {
    fwprintf(stderr,
             L"Error: Either --config or --files must be specified\n\n");
    ShowUsage();
    return 1;
  }

  if (config.mpq_files.empty()) {
    fwprintf(stderr, L"Error: No MPQ files specified\n");
    return 1;
  }

  if (!config.volume_label.empty()) {
    wcscpy_s(g_volume_label, config.volume_label.c_str());
  }

  wprintf(L"Opening MPQ archives...\n");
  if (!g_mpqfs->OpenArchives(config.mpq_files)) {
    fwprintf(stderr, L"Error: Failed to open MPQ archives\n");
    return 1;
  }

  DOKAN_OPTIONS dokan_options = {};
  dokan_options.Version = DOKAN_VERSION;
  dokan_options.Options =
      DOKAN_OPTION_WRITE_PROTECT | DOKAN_OPTION_MOUNT_MANAGER;
  if (debug_mode) {
    dokan_options.Options |= DOKAN_OPTION_DEBUG | DOKAN_OPTION_STDERR;
  }

  WCHAR mount_point[MAX_PATH] = L"M:\\";
  if (!config.volume_letter.empty()) {
    wcscpy_s(mount_point, config.volume_letter.c_str());
    size_t len = wcslen(mount_point);
    if (len > 0 && mount_point[len - 1] != L'\\') {
      wcscat_s(mount_point, L"\\");
    }
  }
  dokan_options.MountPoint = mount_point;
  dokan_options.GlobalContext = 0;

  DOKAN_OPERATIONS operations = mpqfs_get_operations();

  SetConsoleCtrlHandler(CtrlHandler, TRUE);
  DokanInit();

  //wprintf(L"Mounting at %s ...\n", mount_point);
  NTSTATUS status =
      DokanCreateFileSystem(&dokan_options, &operations, &g_dokan_handle);

  if (status != DOKAN_SUCCESS) {
    fwprintf(stderr, L"DokanCreateFileSystem failed: %d\n", status);
    g_mpqfs->CloseArchives();
    DokanShutdown();
    return 1;
  }

  WCHAR actual_mount[MAX_PATH] = L"";
  DWORD drives = GetLogicalDrives();
  for (WCHAR letter = L'A'; letter <= L'Z'; letter++) {
    if (drives & (1 << (letter - L'A'))) {
      WCHAR path[4] = {letter, L':', L'\\', 0};
      WCHAR name[MAX_PATH];
      DWORD serial, maxcomp, flags;
      WCHAR fsname[MAX_PATH];
      if (GetVolumeInformationW(path, name, MAX_PATH, &serial, &maxcomp, &flags, fsname, MAX_PATH)) {
        if (wcscmp(name, g_volume_label) == 0) {
          wcscpy_s(actual_mount, path);
          break;
        }
      }
    }
  }
  if (actual_mount[0]) {
    wprintf(L"Filesystem mounted at %s. Press Ctrl+C to unmount.\n", actual_mount);
  } else {
    wprintf(L"Filesystem mounted. Press Ctrl+C to unmount.\n");
  }
  DokanWaitForFileSystemClosed(g_dokan_handle, INFINITE);

  g_mpqfs->CloseArchives();
  DokanShutdown();
  return 0;
}