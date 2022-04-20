// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/assembler-buffer-cache.h"

#include "src/codegen/assembler.h"

namespace v8::internal::wasm {

std::unique_ptr<AssemblerBuffer> AssemblerBufferCache::GetAssemblerBuffer(
    int size) {
  // TODO(12809): Return PKU-protected buffers, and cache them.
  return NewAssemblerBuffer(size);
}

}  // namespace v8::internal::wasm
