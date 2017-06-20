// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef APPLICATION_LIB_FAR_FILE_H_
#define APPLICATION_LIB_FAR_FILE_H_

#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "application/lib/far/file_operations.h"
#include "lib/ftl/files/unique_fd.h"

template<typename T>
class File {
 public:
  File< T> (T file_object) : actual_file(std::move(file_object)) {}
  File< T> (T file_object, size_t input_size) : data_size(input_size) {}

  const T& get() const { return actual_file; }

  T actual_file;
  off_t curr_pos;
  size_t data_size;

};


off_t  lseek( const File<ftl::UniqueFD> *file, off_t offset, int whence);
off_t  lseek( const File<const char*> *file, off_t offset, int whence);
bool CopyFileToFile(const File<const char*> *src,  ftl::UniqueFD *dst, uint64_t length);
bool CopyFileToFile(const File<ftl::UniqueFD> *src,  ftl::UniqueFD *dst, uint64_t length);
bool CopyFileToPath(const File<const char*> *src, const char* dst_path, uint64_t length);
bool CopyFileToPath(const File<ftl::UniqueFD> *src, const char* dst_path, uint64_t length);


template <typename T>
bool ReadObject(File<ftl::UniqueFD> *file, T* object) {
  return archive::ReadObject(file->get().get(), object);
}

template <typename T>
bool ReadObject(File<const char*> *file, T* object) {
  size_t data_size = file->data_size;
  const char* buff = file->get();
  size_t requested = sizeof(T);
  if (requested > data_size) {
    return false;
  }

  memcpy(object, buff, requested);
  return true;
}

template <typename T>
bool ReadVector( File<const char*> *file, std::vector<T>* vector) {
  size_t requested = vector->size() * sizeof(T);
  size_t data_size = file->data_size;
  char* vec_buffer = reinterpret_cast<char*>(vector->data());
  if (requested > data_size )
    return false;
  strncpy(vec_buffer, file->get(), requested);
  return true;
}

template <typename T>
bool ReadVector(File<ftl::UniqueFD> *file, std::vector<T>* vector) {
  return archive::ReadVector(file->get().get(), vector);
}

#endif  // APPLICATION_LIB_FAR_FILE_H_
