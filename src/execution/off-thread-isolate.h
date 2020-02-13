// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_OFF_THREAD_ISOLATE_H_
#define V8_EXECUTION_OFF_THREAD_ISOLATE_H_

#include "src/base/logging.h"
#include "src/handles/handle-for.h"
#include "src/heap/off-thread-factory.h"

namespace v8 {
namespace internal {

class Isolate;
class OffThreadIsolate;
class OffThreadLogger;

template <>
struct HandleTraits<OffThreadIsolate> {
  template <typename T>
  using HandleType = OffThreadHandle<T>;
  template <typename T>
  using MaybeHandleType = OffThreadHandle<T>;
  using HandleScopeType = OffThreadHandleScope;
};

// HiddenOffThreadFactory parallels Isolate's HiddenFactory
class V8_EXPORT_PRIVATE HiddenOffThreadFactory : private OffThreadFactory {
 public:
  // Forward constructors.
  using OffThreadFactory::OffThreadFactory;
};

// And Isolate-like class that can be passed in to templated methods that need
// an isolate syntactically, but are usable off-thread.
//
// This class holds an OffThreadFactory, but is otherwise effectively a stub
// implementation of an Isolate. In particular, it doesn't allow throwing
// exceptions, and hard crashes if you try.
class V8_EXPORT_PRIVATE OffThreadIsolate final
    : private HiddenOffThreadFactory {
 public:
  explicit OffThreadIsolate(Isolate* isolate);
  ~OffThreadIsolate();

  v8::internal::OffThreadFactory* factory() {
    // Upcast to the privately inherited base-class using c-style casts to avoid
    // undefined behavior (as static_cast cannot cast across private bases).
    // NOLINTNEXTLINE (google-readability-casting)
    return (
        v8::internal::OffThreadFactory*)this;  // NOLINT(readability/casting)
  }

  template <typename T>
  OffThreadHandle<T> Throw(OffThreadHandle<Object> exception) {
    UNREACHABLE();
  }
  [[noreturn]] void FatalProcessOutOfHeapMemory(const char* location) {
    UNREACHABLE();
  }

  int GetNextScriptId();
#if V8_SFI_HAS_UNIQUE_ID
  int GetNextUniqueSharedFunctionInfoId();
#endif  // V8_SFI_HAS_UNIQUE_ID

  bool NeedsSourcePositionsForProfiling();
  bool is_collecting_type_profile();

  OffThreadLogger* logger() { return logger_; }

 private:
  // TODO(leszeks): Extract out the fields of the Isolate we want and store
  // those instead of the whole thing.
  Isolate* isolate_;

  OffThreadLogger* logger_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_OFF_THREAD_ISOLATE_H_
