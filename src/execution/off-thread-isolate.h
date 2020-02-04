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

template <>
struct HandleTraits<OffThreadIsolate> {
  template <typename T>
  using HandleType = OffThreadHandle<T>;
  template <typename T>
  using MaybeHandleType = OffThreadHandle<T>;
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
class V8_EXPORT_PRIVATE OffThreadIsolate : private HiddenOffThreadFactory {
 public:
  explicit OffThreadIsolate(Isolate* isolate)
      : HiddenOffThreadFactory(isolate) {}

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
  inline bool CanAllocateInReadOnlySpace() { return false; }
  inline bool EmptyStringRootIsInitialized() { return true; }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_OFF_THREAD_ISOLATE_H_
