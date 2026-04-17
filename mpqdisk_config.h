#pragma once
#include <string>
#include <vector>

struct MpqDiskConfig {
  std::wstring volume_letter;
  std::wstring volume_label;
  std::vector<std::wstring> mpq_files;
};

MpqDiskConfig parse_config_file(const std::wstring &config_path);
MpqDiskConfig parse_args(int argc, wchar_t *argv[]);