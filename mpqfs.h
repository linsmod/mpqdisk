#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <StormLib.h>

struct MpqFileInfo {
  DWORD index = -1;
  std::string name;
  DWORD file_size = 0;
  DWORD compressed_size = 0;
  DWORD flags = 0;
  FILETIME file_time = {};
  bool is_directory = false;
  std::string parent_path;
  MpqFileInfo() : index(-1), file_size(0) {}
};

class MpqFileSystem {
public:
  MpqFileSystem();
  ~MpqFileSystem();

  bool OpenArchives(const std::vector<std::wstring> &mpq_files);
  void CloseArchives();
  bool IsOpen() const { return hMpq!=NULL; }

  bool FileExists(const std::string &path) const;
  HANDLE OpenFile(MpqFileInfo info) const;
  HANDLE OpenFile(std::string name) const;
  MpqFileInfo LookupFile(const std::string &path) const;
  void CloseFile(HANDLE handle) const;

  DWORD ReadFileSize(HANDLE file_handle) const;
  DWORD ReadFileData(HANDLE file_handle, void *buffer, DWORD bytes_to_read,
                     DWORD &bytes_read, LONGLONG offset) const;
  bool GetFileInfo(const std::string &path, MpqFileInfo &info) const;
  bool EnumerateDirectory(const std::string &dir_path,
                          std::vector<MpqFileInfo> &results) const;

  void BuildFileTree(HANDLE hLoadingMpq);
  const std::unordered_map<std::string, MpqFileInfo> &GetFileTree() const {
    return file_tree_;
  }
  const std::unordered_map<std::string, std::vector<std::string>> &
  GetDirContents() const {
    return dir_contents_;
  }

  ULONGLONG GetTotalSize() const { return total_size_; }
  DWORD GetFileCount() const { return (DWORD)file_tree_.size(); }

  static std::string NormalizePath(const std::string &path);
  static std::string DisplayPathToMpqPath(const std::string &path);
  static std::string MpqPathToDisplayPath(const std::string &mpq_path);
  static std::string GetParentDir(const std::string &path);

private:
  void InsertFileEntry(const SFILE_FIND_DATA &find_data, DWORD& i);
  void CloseArchivesInternal();
  HANDLE hMpq = NULL;
  std::unordered_map<std::string, MpqFileInfo> file_tree_;
  std::unordered_map<std::string, std::vector<std::string>> dir_contents_;
  ULONGLONG total_size_ = 0;
  mutable std::mutex mutex_;
};