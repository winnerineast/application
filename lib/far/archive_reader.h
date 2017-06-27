// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_LIB_FAR_ARCHIVE_READER_H_
#define APPLICATION_LIB_FAR_ARCHIVE_READER_H_

#include <vector>

#include "application/lib/far/format.h"
#include "lib/ftl/files/unique_fd.h"
#include "application/lib/far/file.h"
#include "lib/ftl/strings/string_view.h"

namespace archive {

template<typename T>
class ArchiveReader {
 public:
  // explicit ArchiveReader(ftl::UniqueFD fd);
  explicit ArchiveReader(File<T> f);
  ~ArchiveReader();
  ArchiveReader(const ArchiveReader& other) = delete;

  bool Read();

  uint64_t file_count() const { return directory_table_.size(); }

  template <typename Callback>
  void ListPaths(Callback callback) const {
    for (const auto& entry : directory_table_)
      callback(GetPathView(entry));
  }

  template <typename Callback>
  void ListDirectory(Callback callback) const {
    for (const auto& entry : directory_table_)
      callback(entry);
  }

  bool ExtractFile(ftl::StringView archive_path, const char* output_path) const;
  bool CopyFile(ftl::StringView archive_path, int dst_fd) const;
  bool GetDirectoryEntry(ftl::StringView archive_path,
                         DirectoryTableEntry* entry) const;

  T TakeFileDescriptor();

  ftl::StringView GetPathView(const DirectoryTableEntry& entry) const;

 private:
  bool ReadIndex();
  bool ReadDirectory();

  const IndexEntry* GetIndexEntry(uint64_t type) const;

  File<T> f_;
  std::vector<IndexEntry> index_;
  std::vector<DirectoryTableEntry> directory_table_;
  std::vector<char> path_data_;
};

}  // namespace archive

#endif  // APPLICATION_LIB_FAR_ARCHIVE_READER_H_
