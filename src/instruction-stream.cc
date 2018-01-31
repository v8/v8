// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/instruction-stream.h"

#include "src/builtins/builtins.h"
#include "src/heap/heap.h"
#include "src/objects-inl.h"
#include "src/objects/code-inl.h"

namespace v8 {
namespace internal {

InstructionStream::InstructionStream(Code* code) {
  DCHECK(Builtins::IsIsolateIndependent(code->builtin_index()));
  const size_t page_size = AllocatePageSize();
  byte_length_ =
      RoundUp(static_cast<size_t>(code->instruction_size()), page_size);

  bytes_ = static_cast<uint8_t*>(AllocatePages(
      GetRandomMmapAddr(), byte_length_, page_size, PageAllocator::kReadWrite));
  CHECK_NOT_NULL(bytes_);

  std::memcpy(bytes_, code->instruction_start(), code->instruction_size());
  CHECK(SetPermissions(bytes_, byte_length_, PageAllocator::kReadExecute));
}

InstructionStream::~InstructionStream() {
  CHECK(FreePages(bytes_, byte_length_));
}

}  // namespace internal
}  // namespace v8
