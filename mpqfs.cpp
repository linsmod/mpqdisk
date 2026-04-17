#include "mpqfs.h"
#include <algorithm>
#include <set>

MpqFileSystem::MpqFileSystem() {}

MpqFileSystem::~MpqFileSystem() { CloseArchivesInternal(); }

std::string MpqFileSystem::NormalizePath(const std::string &path) {
  std::string result = path;
  while (!result.empty() && result.front() == '\\')
    result.erase(result.begin());
  while (!result.empty() && result.back() == '\\')
    result.pop_back();
  if (result.empty())
    return "";
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}

std::string MpqFileSystem::DisplayPathToMpqPath(const std::string &path) {
  return path;
}

std::string MpqFileSystem::MpqPathToDisplayPath(const std::string &mpq_path) {
  std::string result = mpq_path;
  std::replace(result.begin(), result.end(), '/', '\\');
  return result;
}

std::string MpqFileSystem::GetParentDir(const std::string &path) {
  auto pos = path.find_last_of('\\');
  if (pos == std::string::npos)
    return "";
  return path.substr(0, pos);
}

bool MpqFileSystem::OpenArchives(const std::vector<std::wstring> &mpq_files) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mpq_files.empty())
    return false;
  HANDLE hLoading = NULL;
  for (size_t i = 0; i < mpq_files.size(); i++) {
    // 1. 检查文件是否存在
    if (GetFileAttributesW(mpq_files[i].c_str()) == INVALID_FILE_ATTRIBUTES) {
      fwprintf(stderr, L"Error: File not found: %s\n", mpq_files[i].c_str());
      continue;
    }

    if (i == 0) {
      // 第一个 MPQ：作为 Base 打开

      if (!SFileOpenArchive(mpq_files[i].c_str(), 0, STREAM_FLAG_READ_ONLY,
                            &hLoading)) {
        fwprintf(stderr, L"Error: Failed to open base MPQ: %u\n",
                 GetLastError());
        return false;
      }
      wprintf(L"Opened base MPQ: %s\n", mpq_files[i].c_str());
    } else {
      // 后续 MPQ：直接挂载为 Patch，不要预先 Open
      // 注意：如果你的 StormLib 版本不支持宽字符 Patch 接口，需要转码
      if (!SFileOpenPatchArchive(hLoading, mpq_files[i].c_str(), nullptr, 0)) {
        fwprintf(stderr, L"Warning: Failed to attach patch (Error %u): %s\n",
                 GetLastError(), mpq_files[i].c_str());
      } else {
        wprintf(L"Attached as patch: %s\n", mpq_files[i].c_str());
      }
    }
  }

  BuildFileTree(hLoading);
  hMpq = hLoading;
  return true;
}

void MpqFileSystem::CloseArchivesInternal() {
  if (hMpq) {
    SFileCloseArchive(hMpq);
    hMpq = NULL;
  }
  file_tree_.clear();
  dir_contents_.clear();
  total_size_ = 0;
}

void MpqFileSystem::CloseArchives() {
  std::lock_guard<std::mutex> lock(mutex_);
  CloseArchivesInternal();
}

void MpqFileSystem::InsertFileEntry(const SFILE_FIND_DATA &find_data,
                                    DWORD &i) {
  std::string mpq_name = find_data.cFileName;
  if (mpq_name.empty())
    return;

  std::string norm_name = NormalizePath(MpqPathToDisplayPath(mpq_name));
  if (norm_name.empty())
    return;

  MpqFileInfo info;
  info.index = i;
  i++;
  info.name = norm_name;
  info.file_size = find_data.dwFileSize;
  info.compressed_size = find_data.dwCompSize;
  info.flags = find_data.dwFileFlags;
  info.is_directory = false;
  info.parent_path = GetParentDir(norm_name);

  ULARGE_INTEGER ft;
  ft.LowPart = find_data.dwFileTimeLo;
  ft.HighPart = find_data.dwFileTimeHi;
  info.file_time.dwLowDateTime = ft.LowPart;
  info.file_time.dwHighDateTime = ft.HighPart;

  file_tree_[norm_name] = info;
  total_size_ += info.file_size;

  std::string parent = info.parent_path;
  while (!parent.empty()) {
    if (file_tree_.find(parent) == file_tree_.end()) {
      MpqFileInfo dir_info;
      dir_info.index = i;
      i++;
      dir_info.name = parent;
      dir_info.file_size = 0;
      dir_info.is_directory = true;
      dir_info.parent_path = GetParentDir(parent);
      dir_info.file_time.dwLowDateTime = 0;
      dir_info.file_time.dwHighDateTime = 0;
      file_tree_[parent] = dir_info;
    }
    parent = GetParentDir(parent);
  }
}

void MpqFileSystem::BuildFileTree(HANDLE hLoadingMpq) {
  file_tree_.clear();
  dir_contents_.clear();
  total_size_ = 0;

  if (!hLoadingMpq)
    return;

  SFILE_FIND_DATA find_data;
  HANDLE hFind = SFileFindFirstFile(hLoadingMpq, "*", &find_data, NULL);
  DWORD i = 0;
  if (hFind != NULL) {
    wprintf(L"SFileFindFirstFile: '%hs'\n", find_data.cFileName);
    InsertFileEntry(find_data, i);
    while (SFileFindNextFile(hFind, &find_data)) {
      InsertFileEntry(find_data, i);
    }
    SFileFindClose(hFind);
  }

  for (auto &[path, info] : file_tree_) {
    if (info.is_directory) {
      dir_contents_[path];
      std::string parent = info.parent_path;
      if (!parent.empty()) {
        dir_contents_[parent].push_back(path);
      } else {
        dir_contents_[""].push_back(path);
      }
    } else {
      std::string parent = info.parent_path;
      if (!parent.empty()) {
        dir_contents_[parent].push_back(path);
      } else {
        dir_contents_[""].push_back(path);
      }
    }
  }

  wprintf(L"File tree entries: %zu, directories: %zu\n", file_tree_.size(),
          dir_contents_.size());
  auto rootIt = dir_contents_.find("");
  if (rootIt != dir_contents_.end()) {
    wprintf(L"Root dir has %zu entries\n", rootIt->second.size());
    for (size_t i = 0; i < rootIt->second.size() && i < 20; i++) {
      const auto &name = rootIt->second[i];
      auto fi = file_tree_.find(name);
      wprintf(L"  [%zu] %hs (is_dir=%d)\n", i, name.c_str(),
              fi != file_tree_.end() ? (int)fi->second.is_directory : -1);
    }

    // 如果根目录条目不存在于file_tree_中，则创建它
    if (file_tree_.find("") == file_tree_.end()) {
      MpqFileInfo root_info;
      root_info.index = i++;
      root_info.name = "";
      root_info.file_size = 0;
      root_info.is_directory = true;
      root_info.parent_path = "";
      root_info.file_time.dwLowDateTime = 0;
      root_info.file_time.dwHighDateTime = 0;
      file_tree_[""] = root_info;
      wprintf(L"Created root directory entry in file_tree_\n");
    }
  } else {
    wprintf(L"Root dir is EMPTY!\n");
  }
}

bool MpqFileSystem::FileExists(const std::string &path) const {
  std::string norm = NormalizePath(path);
  return file_tree_.find(norm) != file_tree_.end();
}
HANDLE MpqFileSystem::OpenFile(MpqFileInfo info) const {
  HANDLE hFile = NULL;
  SFileOpenFileEx(hMpq, info.name.c_str(), SFILE_OPEN_FROM_MPQ, &hFile);
  return hFile;
}
HANDLE MpqFileSystem::OpenFile(std::string name) const {
  HANDLE hFile = NULL;
  SFileOpenFileEx(hMpq, name.c_str(), SFILE_OPEN_FROM_MPQ, &hFile);
  return hFile;
}
MpqFileInfo MpqFileSystem::LookupFile(const std::string &path) const {
  std::string norm = NormalizePath(path);
  auto it = file_tree_.find(norm);
  if (it == file_tree_.end())
    return {};
  if (it->second.is_directory) {
    return it->second;
  }

  std::string mpq_path = DisplayPathToMpqPath(norm);
  if (!hMpq) {
    wprintf(L"LookupFile: Listed, but Mpq context lost");
  } else if (!SFileHasFile(hMpq, mpq_path.c_str())) {
    wprintf(
        L"LookupFile: Listed, but find in pack FAILED norm=%S mpq=%S err=%u\n",
        norm.c_str(), mpq_path.c_str(), GetLastError());
  }
  return it->second;
}

void MpqFileSystem::CloseFile(HANDLE handle) const {
  if (handle && handle != INVALID_HANDLE_VALUE) {
    SFileCloseFile(handle);
  }
}

DWORD MpqFileSystem::ReadFileSize(HANDLE file_handle) const {
  DWORD high = 0;
  return SFileGetFileSize(file_handle, &high);
}

DWORD MpqFileSystem::ReadFileData(HANDLE file_handle, void *buffer,
                                  DWORD bytes_to_read, DWORD &bytes_read,
                                  LONGLONG offset) const {
  if (offset != 0) {
    LONG high = (LONG)(offset >> 32);
    DWORD new_pos = SFileSetFilePointer(
        file_handle, (LONG)(offset & 0xFFFFFFFF), &high, FILE_BEGIN);
    if (new_pos == SFILE_INVALID_POS) {
      return GetLastError();
    }
  }

  DWORD file_size = SFileGetFileSize(file_handle, NULL);
  bytes_read = 0;
  if (!SFileReadFile(file_handle, buffer, bytes_to_read, &bytes_read, NULL)) {
    DWORD err = GetLastError();
    if (err == ERROR_HANDLE_EOF && bytes_read > 0)
      return 0;
    //if (bytes_read == 0 && file_size > 0 && offset == 0)
    //  wprintf(L"ReadFileData: err=%u bytesRead=0 requested=%u fileSize=%u\n",
    //          err, bytes_to_read, file_size);
    //return err;
  }
  return 0;
}

bool MpqFileSystem::GetFileInfo(const std::string &path,
                                MpqFileInfo &info) const {
  std::string norm = NormalizePath(path);
  auto it = file_tree_.find(norm);
  if (it == file_tree_.end())
    return false;
  info = it->second;
  return true;
}

bool MpqFileSystem::EnumerateDirectory(
    const std::string &dir_path, std::vector<MpqFileInfo> &results) const {
  std::string norm_dir = NormalizePath(dir_path);

  auto it = dir_contents_.find(norm_dir);
  if (it != dir_contents_.end()) {
    for (const auto &name : it->second) {
      auto fi = file_tree_.find(name);
      if (fi != file_tree_.end()) {
        results.push_back(fi->second);
      }
    }
    return true;
  }

  if (file_tree_.find(norm_dir) != file_tree_.end() || norm_dir.empty()) {
    auto dit = dir_contents_.find(norm_dir);
    if (dit != dir_contents_.end()) {
      for (const auto &name : dit->second) {
        auto fi = file_tree_.find(name);
        if (fi != file_tree_.end()) {
          results.push_back(fi->second);
        }
      }
    }
    return true;
  }

  return false;
}