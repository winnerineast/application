// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "application/lib/far/archive_reader.h"
#include "application/lib/far/archive_reader.cc"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  File<const char*> f(reinterpret_cast<const char*>(data), size);
  archive::ArchiveReader<const char*> reader(std::move(f));

  reader.Read();
  reader.CopyFile("meta/sandbox",STDOUT_FILENO);

  return 0;
}
