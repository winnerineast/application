// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/far/archive_reader.h"

#include <inttypes.h>
#include <unistd.h>

#include <limits>
#include <utility>

#include "application/lib/far/file_operations.h"
#include "application/lib/far/format.h"
#include "lib/ftl/files/unique_fd.h"

namespace archive {

namespace {

template<typename T>
struct PathComparator {
  const ArchiveReader<T>* reader = nullptr;

  bool operator()(const DirectoryTableEntry& lhs, const ftl::StringView& rhs) {
    return reader->GetPathView(lhs) < rhs;
  }
};

}  // namespace

template<typename T>
ArchiveReader<T>::ArchiveReader(File<T> f) : f_(std::move(f)) {}

template<typename T>
ArchiveReader<T>::~ArchiveReader() = default;

template<typename T>
bool ArchiveReader<T>::Read() {
  return ReadIndex() && ReadDirectory();
}

template<typename T>
bool ArchiveReader<T>::ExtractFile(ftl::StringView archive_path,
                                const char* output_path) const {
  DirectoryTableEntry entry;
  if (!GetDirectoryEntry(archive_path, &entry))
    return false;
  if (lseek(&f_, entry.data_offset, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to offset of file.\n");
    return false;
  }
  if (!CopyFileToPath(&f_, output_path, entry.data_length)) {
    fprintf(stderr, "error: Failed write contents to '%s'.\n", output_path);
    return false;
  }
  return true;
}

template<typename T>
bool ArchiveReader<T>::CopyFile(ftl::StringView archive_path, int dst_fd) const {
  DirectoryTableEntry entry;
  if (!GetDirectoryEntry(archive_path, &entry))
    return false;
  if (lseek(&f_, entry.data_offset, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to offset of file.\n");
    return false;
  }

  ftl::UniqueFD fd(dst_fd);
  if (!CopyFileToFile(&f_, &fd, entry.data_length)) {
    fprintf(stderr, "error: Failed write contents.\n");
    return false;
  }
  return true;
}


template<typename T>
bool ArchiveReader<T>::GetDirectoryEntry(ftl::StringView archive_path,

                                         DirectoryTableEntry* entry) const {
  PathComparator<T> comparator;
  comparator.reader = this;

  auto it = std::lower_bound(directory_table_.begin(), directory_table_.end(),
                             archive_path, comparator);
  if (it == directory_table_.end() || GetPathView(*it) != archive_path)
    return false;
  *entry = *it;
  return true;
}

template<typename T>
T ArchiveReader<T>::TakeFileDescriptor() {
  return std::move(f_);
}


template<typename T>
ftl::StringView ArchiveReader<T>::GetPathView(
    const DirectoryTableEntry& entry) const {
  return ftl::StringView(path_data_.data() + entry.name_offset,
                         entry.name_length);
}

template<typename T>
bool ArchiveReader<T>::ReadIndex() {
  if (lseek(&f_, 0, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to beginning of archive.\n");
    return false;
  }

  IndexChunk index_chunk;
  if (!ReadObject(&f_, &index_chunk)) {
    fprintf(stderr,
            "error: Failed read index chunk. Is this file an archive?\n");
    return false;
  }

  if (index_chunk.magic != kMagic) {
    fprintf(stderr,
            "error: Index chunk missing magic. Is this file an archive?\n");
    return false;
  }

  if (index_chunk.length % sizeof(IndexEntry) != 0 ||
      index_chunk.length >
          std::numeric_limits<uint64_t>::max() - sizeof(IndexChunk)) {
    fprintf(stderr, "error: Invalid index chunk length.\n");
    return false;
  }

  index_.resize(index_chunk.length / sizeof(IndexEntry));
  if (!ReadVector(&f_, &index_)) {
    fprintf(stderr, "error: Failed to read contents of index chunk.\n");
    return false;
  }

  uint64_t next_offset = sizeof(IndexChunk) + index_chunk.length;
  for (const auto& entry : index_) {
    if (entry.offset != next_offset) {
      fprintf(stderr,
              "error: Chunk at offset %" PRIu64 " not tightly packed.\n",
              entry.offset);
      return false;
    }
    if (entry.length % 8 != 0) {
      fprintf(stderr,
              "error: Chunk length %" PRIu64
              " not aligned to 8 byte boundary.\n",
              entry.length);
      return false;
    }
    if (entry.length > std::numeric_limits<uint64_t>::max() - entry.offset) {
      fprintf(stderr,
              "error: Chunk length %" PRIu64
              " overflowed total archive size.\n",
              entry.length);
      return false;
    }
    next_offset = entry.offset + entry.length;
  }

  return true;
}

template<typename T>
bool ArchiveReader<T>::ReadDirectory() {
  const IndexEntry* dir_entry = GetIndexEntry(kDirType);
  if (!dir_entry) {
    fprintf(stderr, "error: Cannot find directory chunk.\n");
    return false;
  }
  if (dir_entry->length % sizeof(DirectoryTableEntry) != 0) {
    fprintf(stderr, "error: Invalid directory chunk length: %" PRIu64 ".\n",
            dir_entry->length);
    return false;
  }
  uint64_t file_count = dir_entry->length / sizeof(DirectoryTableEntry);
  directory_table_.resize(file_count);

  if (lseek(&f_, dir_entry->offset, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to directory chunk.\n");
    return false;
  }
  if (!ReadVector(&f_, &directory_table_)) {
    fprintf(stderr, "error: Failed to read directory table.\n");
    return false;
  }

  const IndexEntry* dirnames_entry = GetIndexEntry(kDirnamesType);
  if (!dirnames_entry) {
    fprintf(stderr, "error: Cannot find directory names chunk.\n");
    return false;
  }
  path_data_.resize(dirnames_entry->length);

  if (lseek(&f_, dirnames_entry->offset, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to directory names chunk.\n");
    return false;
  }
  if (!ReadVector(&f_, &path_data_)) {
    fprintf(stderr, "error: Failed to read directory names.\n");
    return false;
  }

  return true;
}

template<typename T>
const IndexEntry* ArchiveReader<T>::GetIndexEntry(uint64_t type) const {
  for (auto& entry : index_) {
    printf("the type of the entry is: %zu, but the requested one is: %zu]\n", entry.type, type);
    if (entry.type == type)
      return &entry;
  }
  return nullptr;
}

}  // namespace archive
