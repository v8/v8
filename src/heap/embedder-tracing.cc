// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/embedder-tracing.h"

#include "include/cppgc/common.h"
#include "include/v8-cppgc.h"
#include "src/base/logging.h"
#include "src/heap/marking-worklist-inl.h"

namespace v8::internal {

void LocalEmbedderHeapTracer::SetCppHeap(CppHeap* cpp_heap) {
  cpp_heap_ = cpp_heap;
}

void LocalEmbedderHeapTracer::PrepareForTrace(CollectionType type) {
  if (!InUse()) return;

  CppHeap::GarbageCollectionFlags flags =
      CppHeap::GarbageCollectionFlagValues::kNoFlags;
  auto* heap = isolate_->heap();
  if (heap->is_current_gc_forced()) {
    flags |= CppHeap::GarbageCollectionFlagValues::kForced;
  }
  if (heap->ShouldReduceMemory()) {
    flags |= CppHeap::GarbageCollectionFlagValues::kReduceMemory;
  }
  cpp_heap()->InitializeTracing(type == CollectionType::kMajor
                                    ? cppgc::internal::CollectionType::kMajor
                                    : cppgc::internal::CollectionType::kMinor,
                                flags);
}

void LocalEmbedderHeapTracer::TracePrologue() {
  if (!InUse()) return;

  embedder_worklist_empty_ = false;

  cpp_heap()->StartTracing();
}

void LocalEmbedderHeapTracer::TraceEpilogue() {
  if (!InUse()) return;

  // Resetting to state unknown as there may be follow up garbage collections
  // triggered from callbacks that have a different stack state.
  embedder_stack_state_ = cppgc::EmbedderStackState::kMayContainHeapPointers;

  cpp_heap()->TraceEpilogue();
}

void LocalEmbedderHeapTracer::EnterFinalPause() {
  if (!InUse()) return;

  cpp_heap()->EnterFinalPause(embedder_stack_state_);
}

bool LocalEmbedderHeapTracer::Trace(double max_duration) {
  return !InUse() || cpp_heap()->AdvanceTracing(max_duration);
}

bool LocalEmbedderHeapTracer::IsRemoteTracingDone() {
  return !InUse() || cpp_heap()->IsTracingDone();
}

}  // namespace v8::internal
