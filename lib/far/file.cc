#include <fcntl.h>

#include "application/lib/far/file.h"


off_t  lseek( const File<ftl::UniqueFD> *file, off_t offset, int whence) {
  return lseek(file->get().get(), offset, whence);
}

off_t  lseek( const File<const char*> *file, off_t offset, int whence) {
  File<const char*> *f = const_cast<File<const char*>*> (file);
  off_t curr_pos = file->curr_pos;
  off_t new_pos;

  switch (whence)
  {
    case SEEK_CUR:
      new_pos = curr_pos + offset;
      f->curr_pos = new_pos;
      return new_pos;

    case SEEK_SET:
      f->curr_pos = offset;
      return offset;

    case SEEK_END:
      new_pos = file->data_size + offset;
      f->curr_pos = new_pos;
      return new_pos;

    default:
      errno = EINVAL;
      return - 1;
  }

}

bool CopyFileToFile(const File<const char*> *src, ftl::UniqueFD *fd, uint64_t length) {
  int dst_fd = fd->get();
  constexpr uint64_t kBufferSize = 64 * 1024;
  char buffer[kBufferSize];
  ssize_t src_len = strlen(src->get());
  uint64_t requested = 0;
  for (uint64_t copied = 0; copied < length; copied += requested) {
    requested =
        std::min(kBufferSize, static_cast<uint64_t>(length - copied));
    if (src_len - copied < requested)
      return false;
    memcpy(buffer, src->get()  + copied, requested);
    buffer[copied + requested] = 0;
    if (!ftl::WriteFileDescriptor(dst_fd, buffer, requested))
      return false;
  }
  return true;
}

bool CopyFileToFile(const File<ftl::UniqueFD> *src, ftl::UniqueFD *dst, uint64_t length) {
  return archive::CopyFileToFile(src->get().get(), dst->get(), length);
}

bool CopyFileToPath(const File<const char *> *buff, const char* dst_path, uint64_t length) {
  ftl::UniqueFD dst_fd(open(dst_path, O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
  if (!dst_fd.is_valid())
    return false;
  return CopyFileToFile(buff, &dst_fd, length);
}

bool CopyFileToPath(const File<ftl::UniqueFD> *file, const char* dst_path, uint64_t length) {
  return archive::CopyFileToPath(file->get().get(), dst_path, length);
}
