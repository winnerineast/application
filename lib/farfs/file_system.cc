// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/farfs/file_system.h"

#include <fcntl.h>

#include "lib/mtl/vfs/vfs_serve.h"

namespace archive {
namespace {

struct DirRecord {
  DirRecord() = default;
  DirRecord(DirRecord&& other)
      : names(std::move(other.names)), children(std::move(other.children)) {}
  DirRecord(const DirRecord& other) = delete;

  void swap(DirRecord& other) {
    names.swap(other.names);
    children.swap(other.children);
  }

  mxtl::RefPtr<vmofs::VnodeDir> CreateDirectory(fs::Dispatcher* dispatcher) {
    size_t count = names.size();
    mxtl::Array<mxtl::StringPiece> names_array(new mxtl::StringPiece[count],
                                               count);
    mxtl::Array<mxtl::RefPtr<vmofs::Vnode>> children_array(
        new mxtl::RefPtr<vmofs::Vnode>[count], count);
    for (size_t i = 0; i < count; ++i) {
      names_array[i] = names[i];
      children_array[i] = std::move(children[i]);
    }

    return mxtl::AdoptRef(new vmofs::VnodeDir(
        dispatcher, std::move(names_array), std::move(children_array)));
  }

  mtl::VFSDispatcher dispatcher_;
  std::vector<mxtl::StringPiece> names;
  std::vector<mxtl::RefPtr<vmofs::Vnode>> children;
};

ftl::StringView PopLastDirectory(ftl::StringView* path) {
  FTL_DCHECK(path->size() >= 2);  // Shortest path: "x/".
  size_t end = path->size() - 1;
  FTL_DCHECK(path->at(end) == '/');  // Must end with '/'.
  size_t begin = path->rfind('/', end - 1);
  if (begin == ftl::StringView::npos) {
    ftl::StringView result = path->substr(0, end);
    *path = ftl::StringView();
    return result;
  }
  ftl::StringView result = path->substr(begin + 1, end - begin - 1);
  FTL_DCHECK(!result.empty());  // "//" is disallowed.
  *path = path->substr(0, begin + 1);
  return result;
}

bool PopFirstDirectory(ftl::StringView* path) {
  size_t end = path->find('/');
  if (end == ftl::StringView::npos)
    return false;
  *path = path->substr(end + 1);
  return true;
}

mxtl::StringPiece ToStringPiece(ftl::StringView view) {
  return mxtl::StringPiece(view.data(), view.size());
}

mxtl::RefPtr<vmofs::VnodeFile> CreateFile(fs::Dispatcher* dispatcher,
                                          mx_handle_t vmo,
                                          const DirectoryTableEntry& entry) {
  return mxtl::AdoptRef(new vmofs::VnodeFile(dispatcher, vmo, entry.data_offset,
                                             entry.data_length));
}

void LeaveDirectory(fs::Dispatcher* dispatcher,
                    ftl::StringView name,
                    std::vector<DirRecord>* stack) {
  auto child = stack->back().CreateDirectory(dispatcher);
  stack->pop_back();
  DirRecord& parent = stack->back();
  parent.names.push_back(ToStringPiece(name));
  parent.children.push_back(std::move(child));
}

}  // namespace

FileSystem::FileSystem(mx::vmo vmo) : vmo_(vmo.get()) {
  uint64_t num_bytes = 0;
  mx_status_t status = vmo.get_size(&num_bytes);
  if (status != MX_OK)
    return;
  ftl::UniqueFD fd(mxio_vmo_fd(vmo.release(), 0, num_bytes));
  if (!fd.is_valid())
    return;
  File<ftl::UniqueFD> f(std::move(fd));
  reader_ = std::make_unique<ArchiveReader<ftl::UniqueFD>>(std::move(f));
  CreateDirectory();
}

FileSystem::~FileSystem() = default;

bool FileSystem::Serve(mx::channel channel) {
  return directory_ && mtl::VFSServe(directory_, std::move(channel));
}

mx::channel FileSystem::OpenAsDirectory() {
  mx::channel h1, h2;
  if (mx::channel::create(0, &h1, &h2) < 0)
    return mx::channel();
  if (!Serve(std::move(h1)))
    return mx::channel();
  return h2;
}

mx::vmo FileSystem::GetFileAsVMO(ftl::StringView path) {
  if (!reader_)
    return mx::vmo();
  DirectoryTableEntry entry;
  if (!reader_->GetDirectoryEntry(path, &entry))
    return mx::vmo();
  mx_handle_t result = MX_HANDLE_INVALID;
  mx_vmo_clone(vmo_, MX_VMO_CLONE_COPY_ON_WRITE, entry.data_offset,
               entry.data_length, &result);
  return mx::vmo(result);
}

bool FileSystem::GetFileAsString(ftl::StringView path, std::string* result) {
  if (!reader_)
    return false;
  DirectoryTableEntry entry;
  if (!reader_->GetDirectoryEntry(path, &entry))
    return false;
  std::string data;
  data.resize(entry.data_length);
  size_t actual;
  mx_status_t status = mx_vmo_read(vmo_, &data[0], entry.data_offset,
                                   entry.data_length, &actual);
  if (status != MX_OK || actual != entry.data_length)
    return false;
  result->swap(data);
  return true;
}

void FileSystem::CreateDirectory() {
  if (!reader_ || !reader_->Read())
    return;

  std::vector<DirRecord> stack;
  stack.push_back(DirRecord());
  ftl::StringView current_dir;

  reader_->ListDirectory([&](const DirectoryTableEntry& entry) {
    ftl::StringView path = reader_->GetPathView(entry);

    while (path.substr(0, current_dir.size()) != current_dir)
      LeaveDirectory(&dispatcher_, PopLastDirectory(&current_dir), &stack);

    ftl::StringView remaining = path.substr(current_dir.size());
    while (PopFirstDirectory(&remaining))
      stack.push_back(DirRecord());  // Enter directory.

    current_dir = path.substr(0, path.size() - remaining.size());

    DirRecord& parent = stack.back();
    parent.names.push_back(ToStringPiece(remaining));
    parent.children.push_back(CreateFile(&dispatcher_, vmo_, entry));
  });

  while (!current_dir.empty())
    LeaveDirectory(&dispatcher_, PopLastDirectory(&current_dir), &stack);

  FTL_DCHECK(stack.size() == 1);

  directory_ = stack.back().CreateDirectory(&dispatcher_);
}

}  // namespace archive
