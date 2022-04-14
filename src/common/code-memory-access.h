// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_CODE_MEMORY_ACCESS_H_
#define V8_COMMON_CODE_MEMORY_ACCESS_H_

#include "src/base/build_config.h"

namespace v8 {
namespace internal {

class CodePageCollectionMemoryModificationScope;
class CodePageMemoryModificationScope;
class CodeSpaceMemoryModificationScope;
namespace wasm {
class CodeSpaceWriteScope;
}

// This scope is a wrapper for APRR/MAP_JIT machinery on MacOS on ARM64
// ("Apple M1"/Apple Silicon) with respective semantics.
// See pthread_jit_write_protect_np() for details.
// On other platforms the scope is a no-op.
//
// The semantics is the following: the scope switches permissions between
// writable and executable for all the pages allocated with RWX permissions.
// Only current thread is affected. This achieves "real" W^X and it's fast.
// By default it is assumed that the state is executable.
//
// The scope is reentrant and thread safe.
class V8_NODISCARD RwxMemoryWriteScope final {
 public:
  V8_INLINE explicit RwxMemoryWriteScope();
  V8_INLINE virtual ~RwxMemoryWriteScope();

  // Disable copy constructor and copy-assignment operator, since this manages
  // a resource and implicit copying of the scope can yield surprising errors.
  RwxMemoryWriteScope(const RwxMemoryWriteScope&) = delete;
  RwxMemoryWriteScope& operator=(const RwxMemoryWriteScope&) = delete;

 private:
  friend class CodePageCollectionMemoryModificationScope;
  friend class CodePageMemoryModificationScope;
  friend class CodeSpaceMemoryModificationScope;
  friend class wasm::CodeSpaceWriteScope;

  // {SetWritable} and {SetExecutable} implicitly enters/exits the scope.
  // These methods are exposed only for the purpose of implementing other
  // scope classes that affect executable pages permissions.
  V8_INLINE static void SetWritable();
  V8_INLINE static void SetExecutable();

#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT
  // This counter is used for supporting scope reentrance.
  static thread_local int code_space_write_nesting_level_;
#endif  // V8_HAS_PTHREAD_JIT_WRITE_PROTECT || defined(DEBUG)
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_CODE_MEMORY_ACCESS_H_
