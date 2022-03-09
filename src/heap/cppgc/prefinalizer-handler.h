// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_PREFINALIZER_HANDLER_H_
#define V8_HEAP_CPPGC_PREFINALIZER_HANDLER_H_

#include <utility>
#include <vector>

#include "include/cppgc/prefinalizer.h"
#include "src/base/pointer-with-payload.h"

namespace cppgc {
namespace internal {

class HeapBase;

struct PreFinalizer final {
  using Callback = PrefinalizerRegistration::Callback;

  PreFinalizer(void* object, const void* base_object_payload,
               Callback callback);

#if defined(CPPGC_CAGED_HEAP)

  uint32_t object_offset;
  uint32_t base_object_payload_offset;

#else  // !defined(CPPGC_CAGED_HEAP)

  enum class PointerType : uint8_t {
    kAtBase,
    kInnerPointer,
  };

  // Contains the pointer and also an indicator of whether the pointer points to
  // the base of the object or is an inner pointer.
  v8::base::PointerWithPayload<void, PointerType, 1> object_and_offset;

#endif  // !defined(CPPGC_CAGED_HEAP)

  Callback callback;

  bool operator==(const PreFinalizer& other) const;
};

class PreFinalizerHandler final {
 public:
  explicit PreFinalizerHandler(HeapBase& heap);

  void RegisterPrefinalizer(PreFinalizer pre_finalizer);

  void InvokePreFinalizers();

  bool IsInvokingPreFinalizers() const { return is_invoking_; }

  void NotifyAllocationInPrefinalizer(size_t);
  size_t ExtractBytesAllocatedInPrefinalizers() {
    return std::exchange(bytes_allocated_in_prefinalizers, 0);
  }

 private:
  // Checks that the current thread is the thread that created the heap.
  bool CurrentThreadIsCreationThread();

  // Pre-finalizers are called in the reverse order in which they are
  // registered by the constructors (including constructors of Mixin
  // objects) for an object, by processing the ordered_pre_finalizers_
  // back-to-front.
  std::vector<PreFinalizer> ordered_pre_finalizers_;
  std::vector<PreFinalizer>* current_ordered_pre_finalizers_;

  HeapBase& heap_;
  bool is_invoking_ = false;
#ifdef DEBUG
  int creation_thread_id_;
#endif

  // Counter of bytes allocated during prefinalizers.
  size_t bytes_allocated_in_prefinalizers = 0u;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_PREFINALIZER_HANDLER_H_
