#include "mpqdisk_config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <windows.h>

static std::wstring utf8_to_wide(const std::string &str) {
  if (str.empty())
    return L"";
  int len =
      MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
  if (len <= 0)
    return std::wstring(str.begin(), str.end());
  std::wstring result(len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0],
                      len);
  return result;
}

static std::wstring trim(const std::wstring &s) {
  auto start = s.find_first_not_of(L" \t\r\n");
  if (start == std::wstring::npos)
    return L"";
  auto end = s.find_last_not_of(L" \t\r\n");
  return s.substr(start, end - start + 1);
}

static bool is_directory(const std::wstring &path) {
  DWORD attrs = GetFileAttributesW(path.c_str());
  return (attrs != INVALID_FILE_ATTRIBUTES) &&
         (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring resolve_config_path(const std::wstring &input) {
  WCHAR abs_path[MAX_PATH];
  GetFullPathNameW(input.c_str(), MAX_PATH, abs_path, nullptr);
  if (is_directory(abs_path)) {
    std::wstring candidate = std::wstring(abs_path);
    if (candidate.back() != L'\\')
      candidate += L'\\';
    candidate += L"mpqdisk.conf";
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
      return candidate;
    }
    return std::wstring(abs_path) + L"\\mpqdisk.conf";
  }
  return abs_path;
}

static std::wstring get_full_path(const std::wstring &config_file_path,
                                  const std::wstring &path) {
  if (path.size() >= 2 &&
      (path[1] == L':' || (path[0] == L'\\' && path[1] == L'\\'))) {
    WCHAR full_result[MAX_PATH];
    GetFullPathNameW(path.c_str(), MAX_PATH, full_result, nullptr);
    return full_result;
  }
  WCHAR drive[_MAX_DRIVE];
  WCHAR dir[_MAX_DIR];
  _wsplitpath_s(config_file_path.c_str(), drive, _MAX_DRIVE, dir, _MAX_DIR,
                nullptr, 0, nullptr, 0);
  std::wstring base_dir = std::wstring(drive) + dir;
  WCHAR full_base[MAX_PATH];
  GetFullPathNameW(base_dir.c_str(), MAX_PATH, full_base, nullptr);
  std::wstring combined = std::wstring(full_base);
  if (!combined.empty() && combined.back() != L'\\')
    combined += L'\\';
  combined += path;
  WCHAR full_result[MAX_PATH];
  GetFullPathNameW(combined.c_str(), MAX_PATH, full_result, nullptr);
  return full_result;
}

MpqDiskConfig parse_config_file(const std::wstring &raw_path) {
  MpqDiskConfig config;
  std::wstring config_path = resolve_config_path(raw_path);
  std::ifstream file(config_path);
  if (!file.is_open()) {
    fwprintf(stderr, L"Error: Cannot open config file: %s\n",
             config_path.c_str());
    return config;
  }

  std::wstring wconfig_path = config_path;
  std::string line;
  int section = 0;

  while (std::getline(file, line)) {
    std::wstring wline = utf8_to_wide(line);
    wline = trim(wline);
    if (wline.empty() || wline[0] == L'#' || wline[0] == L';')
      continue;

    if (wline == L"[mount]") {
      section = 1;
      continue;
    }
    if (wline == L"[files]") {
      section = 2;
      continue;
    }
    if (wline[0] == L'[') {
      section = 0;
      continue;
    }

    if (section == 1) {
      auto eq = wline.find(L'=');
      if (eq != std::wstring::npos) {
        std::wstring key = trim(wline.substr(0, eq));
        std::wstring val = trim(wline.substr(eq + 1));
        if (key == L"volume") {
          auto sp = val.find(L' ');
          if (sp != std::wstring::npos) {
            config.volume_letter = trim(val.substr(0, sp));
            config.volume_label = trim(val.substr(sp + 1));
          } else {
            config.volume_letter = val;
          }
        }
      }
    } else if (section == 2) {
      std::wstring trimmed = trim(wline);
      if (!trimmed.empty()) {
        std::wstring full_path = get_full_path(wconfig_path, trimmed);
        config.mpq_files.push_back(full_path);
      }
    }
  }

  return config;
}

MpqDiskConfig parse_args(int argc, wchar_t *argv[]) {
  MpqDiskConfig config;
  bool in_files = false;

  for (int i = 1; i < argc; i++) {
    std::wstring arg = argv[i];
    if (arg == L"--mount") {
      in_files = false;
      continue;
    }
    if (arg == L"--config") {
      if (i + 1 < argc) {
        config = parse_config_file(argv[++i]);
        return config;
      }
    }
    if (arg == L"--files") {
      in_files = true;
      continue;
    }
    if (arg == L"--volume") {
      if (i + 1 < argc) {
        std::wstring val = argv[++i];
        auto sp = val.find(L' ');
        if (sp != std::wstring::npos) {
          config.volume_letter = trim(val.substr(0, sp));
          config.volume_label = trim(val.substr(sp + 1));
        } else {
          config.volume_letter = val;
        }
      }
      continue;
    }
    if (in_files) {
      WCHAR full_path[MAX_PATH];
      GetFullPathNameW(arg.c_str(), MAX_PATH, full_path, nullptr);
      config.mpq_files.push_back(full_path);
    }
  }

  return config;
}