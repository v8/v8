// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/allocation.h"
#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/persistent.h"
#include "include/cppgc/platform.h"
#include "include/v8-cppgc.h"
#include "include/v8.h"
#include "src/api/api-inl.h"
#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/heap/unified-heap-utils.h"

namespace v8 {
namespace internal {

namespace {

class Wrappable final : public cppgc::GarbageCollected<Wrappable> {
 public:
  static size_t destructor_callcount;

  ~Wrappable() { destructor_callcount++; }

  void Trace(cppgc::Visitor* visitor) const { visitor->Trace(wrapper_); }

  void SetWrapper(v8::Isolate* isolate, v8::Local<v8::Object> wrapper) {
    wrapper_.Reset(isolate, wrapper);
  }

  TracedReference<v8::Object>& wrapper() { return wrapper_; }

 private:
  TracedReference<v8::Object> wrapper_;
};

size_t Wrappable::destructor_callcount = 0;

}  // namespace

TEST_F(UnifiedHeapTest, OnlyGC) { CollectGarbageWithEmbedderStack(); }

TEST_F(UnifiedHeapTest, FindingV8ToBlinkReference) {
  v8::HandleScope scope(v8_isolate());
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Object> api_object = WrapperHelper::CreateWrapper(
      context, cppgc::MakeGarbageCollected<Wrappable>(allocation_handle()));
  Wrappable::destructor_callcount = 0;
  EXPECT_FALSE(api_object.IsEmpty());
  EXPECT_EQ(0u, Wrappable::destructor_callcount);
  CollectGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  EXPECT_EQ(0u, Wrappable::destructor_callcount);
  WrapperHelper::ResetWrappableConnection(api_object);
  CollectGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  EXPECT_EQ(1u, Wrappable::destructor_callcount);
}

TEST_F(UnifiedHeapTest, WriteBarrierV8ToCppReference) {
  v8::HandleScope scope(v8_isolate());
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  void* wrappable = cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());
  v8::Local<v8::Object> api_object =
      WrapperHelper::CreateWrapper(context, wrappable);
  Wrappable::destructor_callcount = 0;
  WrapperHelper::ResetWrappableConnection(api_object);
  SimulateIncrementalMarking();
  {
    // The following snippet shows the embedder code for implementing a GC-safe
    // setter for JS to C++ references.
    WrapperHelper::SetWrappableConnection(api_object, wrappable, wrappable);
    JSHeapConsistency::WriteBarrierParams params;
    auto barrier_type = JSHeapConsistency::GetWriteBarrierType(
        api_object, 1, wrappable, params);
    EXPECT_EQ(JSHeapConsistency::WriteBarrierType::kMarking, barrier_type);
    JSHeapConsistency::DijkstraMarkingBarrier(
        params, cpp_heap().GetHeapHandle(), wrappable);
  }
  CollectGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  EXPECT_EQ(0u, Wrappable::destructor_callcount);
}

TEST_F(UnifiedHeapTest, WriteBarrierCppToV8Reference) {
  v8::HandleScope scope(v8_isolate());
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  cppgc::Persistent<Wrappable> wrappable =
      cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());
  Wrappable::destructor_callcount = 0;
  SimulateIncrementalMarking();
  // Pick a sentinel to compare against.
  void* kMagicAddress = &Wrappable::destructor_callcount;
  {
    // The following snippet shows the embedder code for implementing a GC-safe
    // setter for C++ to JS references.
    v8::HandleScope nested_scope(v8_isolate());
    v8::Local<v8::Object> api_object =
        WrapperHelper::CreateWrapper(context, nullptr);
    // Setting only one field to avoid treating this as wrappable backref, see
    // `LocalEmbedderHeapTracer::ExtractWrapperInfo`.
    api_object->SetAlignedPointerInInternalField(1, kMagicAddress);
    wrappable->SetWrapper(v8_isolate(), api_object);
    JSHeapConsistency::WriteBarrierParams params;
    auto barrier_type =
        JSHeapConsistency::GetWriteBarrierType(wrappable->wrapper(), params);
    EXPECT_EQ(JSHeapConsistency::WriteBarrierType::kMarking, barrier_type);
    JSHeapConsistency::DijkstraMarkingBarrier(
        params, cpp_heap().GetHeapHandle(), wrappable->wrapper());
  }
  CollectGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  EXPECT_EQ(0u, Wrappable::destructor_callcount);
  EXPECT_EQ(kMagicAddress,
            wrappable->wrapper()->GetAlignedPointerFromInternalField(1));
}

}  // namespace internal
}  // namespace v8
