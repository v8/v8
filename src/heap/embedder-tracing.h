// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_EMBEDDER_TRACING_H_
#define V8_HEAP_EMBEDDER_TRACING_H_

#include <atomic>

#include "include/v8-cppgc.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/flags/flags.h"
#include "src/heap/cppgc-js/cpp-heap.h"

namespace v8 {
namespace internal {

class Heap;
class JSObject;

class V8_EXPORT_PRIVATE LocalEmbedderHeapTracer final {
 public:
  enum class CollectionType : uint8_t {
    kMinor,
    kMajor,
  };
  using WrapperInfo = std::pair<void*, void*>;

  // WrapperInfo is passed over the API. Use VerboseWrapperInfo to access pair
  // internals in a named way. See ProcessingScope::TracePossibleJSWrapper()
  // below on how a V8 object is parsed to gather the information.
  struct VerboseWrapperInfo {
    constexpr explicit VerboseWrapperInfo(const WrapperInfo& raw_info)
        : raw_info(raw_info) {}

    // Information describing the type pointed to via instance().
    void* type_info() const { return raw_info.first; }
    // Direct pointer to an instance described by type_info().
    void* instance() const { return raw_info.second; }
    // Returns whether the info is empty and thus does not keep a C++ object
    // alive.
    bool is_empty() const { return !type_info() || !instance(); }

    const WrapperInfo& raw_info;
  };

  static V8_INLINE bool ExtractWrappableInfo(Isolate*, JSObject,
                                             const WrapperDescriptor&,
                                             WrapperInfo*);
  static V8_INLINE bool ExtractWrappableInfo(
      Isolate*, const WrapperDescriptor&, const EmbedderDataSlot& type_slot,
      const EmbedderDataSlot& instance_slot, WrapperInfo*);

  explicit LocalEmbedderHeapTracer(Isolate* isolate) : isolate_(isolate) {}

  ~LocalEmbedderHeapTracer() {
    // CppHeap is not detached from Isolate here. Detaching is done explicitly
    // on Isolate/Heap/CppHeap destruction.
  }

  bool InUse() const { return cpp_heap_; }

  void SetCppHeap(CppHeap* cpp_heap);
  void PrepareForTrace(CollectionType type);
  void TracePrologue();
  void TraceEpilogue();
  void EnterFinalPause();
  bool Trace(double deadline);
  bool IsRemoteTracingDone();

  bool ShouldFinalizeIncrementalMarking() {
    // Covers cases where no remote tracer is in use or the flags for
    // incremental marking have been disabled.
    if (!SupportsIncrementalEmbedderSteps()) return true;

    return IsRemoteTracingDone() && embedder_worklist_empty_;
  }

  bool SupportsIncrementalEmbedderSteps() const {
    if (!InUse()) return false;

    return v8_flags.cppheap_incremental_marking;
  }

  void SetEmbedderWorklistEmpty(bool is_empty) {
    embedder_worklist_empty_ = is_empty;
  }

  void IncreaseAllocatedSize(size_t bytes) {
    remote_stats_.used_size.fetch_add(bytes, std::memory_order_relaxed);
    remote_stats_.allocated_size += bytes;
    if (remote_stats_.allocated_size >
        remote_stats_.allocated_size_limit_for_check) {
      StartIncrementalMarkingIfNeeded();
      remote_stats_.allocated_size_limit_for_check =
          remote_stats_.allocated_size + kEmbedderAllocatedThreshold;
    }
  }

  void DecreaseAllocatedSize(size_t bytes) {
    DCHECK_GE(remote_stats_.used_size.load(std::memory_order_relaxed), bytes);
    remote_stats_.used_size.fetch_sub(bytes, std::memory_order_relaxed);
  }

  void StartIncrementalMarkingIfNeeded();

  size_t used_size() const {
    return remote_stats_.used_size.load(std::memory_order_relaxed);
  }
  size_t allocated_size() const { return remote_stats_.allocated_size; }

  WrapperInfo ExtractWrapperInfo(Isolate* isolate, JSObject js_object);

  void UpdateRemoteStats(size_t, double);

  cppgc::EmbedderStackState embedder_stack_state() const {
    return embedder_stack_state_;
  }

  void EmbedderWriteBarrier(Heap*, JSObject);

 private:
  static constexpr size_t kEmbedderAllocatedThreshold = 128 * KB;

  CppHeap* cpp_heap() {
    DCHECK_NOT_NULL(cpp_heap_);
    DCHECK_IMPLIES(isolate_, cpp_heap_ == isolate_->heap()->cpp_heap());
    return cpp_heap_;
  }

  WrapperDescriptor wrapper_descriptor() {
    return cpp_heap()->wrapper_descriptor();
  }

  Isolate* const isolate_;
  CppHeap* cpp_heap_ = nullptr;

  cppgc::EmbedderStackState embedder_stack_state_ =
      cppgc::EmbedderStackState::kMayContainHeapPointers;
  // Indicates whether the embedder worklist was observed empty on the main
  // thread. This is opportunistic as concurrent marking tasks may hold local
  // segments of potential embedder fields to move to the main thread.
  bool embedder_worklist_empty_ = false;

  struct RemoteStatistics {
    // Used size of objects in bytes reported by the embedder. Updated via
    // TraceSummary at the end of tracing and incrementally when the GC is not
    // in progress.
    std::atomic<size_t> used_size{0};
    // Totally bytes allocated by the embedder. Monotonically
    // increasing value. Used to approximate allocation rate.
    size_t allocated_size = 0;
    // Limit for |allocated_size| in bytes to avoid checking for starting a GC
    // on each increment.
    size_t allocated_size_limit_for_check = 0;
  } remote_stats_;

  friend class EmbedderStackStateScope;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_EMBEDDER_TRACING_H_
