// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/embedder-tracing.h"

#include "include/v8-embedder-heap.h"
#include "include/v8-function.h"
#include "include/v8-template.h"
#include "src/handles/global-handles.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using LocalEmbedderHeapTracerWithIsolate = TestWithHeapInternals;

namespace heap {

using testing::StrictMock;
using testing::_;
using testing::Return;
using v8::EmbedderHeapTracer;
using v8::internal::LocalEmbedderHeapTracer;

namespace {

LocalEmbedderHeapTracer::WrapperInfo CreateWrapperInfo() {
  return LocalEmbedderHeapTracer::WrapperInfo(nullptr, nullptr);
}

}  // namespace

START_ALLOW_USE_DEPRECATED()
class MockEmbedderHeapTracer : public EmbedderHeapTracer {
 public:
  MOCK_METHOD(void, TracePrologue, (EmbedderHeapTracer::TraceFlags),
              (override));
  MOCK_METHOD(void, TraceEpilogue, (EmbedderHeapTracer::TraceSummary*),
              (override));
  MOCK_METHOD(void, EnterFinalPause, (EmbedderHeapTracer::EmbedderStackState),
              (override));
  MOCK_METHOD(bool, IsTracingDone, (), (override));
  MOCK_METHOD(void, RegisterV8References,
              ((const std::vector<std::pair<void*, void*> >&)), (override));
  MOCK_METHOD(bool, AdvanceTracing, (double deadline_in_ms), (override));
};

END_ALLOW_USE_DEPRECATED()

TEST(LocalEmbedderHeapTracer, InUse) {
  MockEmbedderHeapTracer mock_remote_tracer;
  LocalEmbedderHeapTracer local_tracer(nullptr);
  local_tracer.SetRemoteTracer(&mock_remote_tracer);
  EXPECT_TRUE(local_tracer.InUse());
}

TEST(LocalEmbedderHeapTracer, NoRemoteTracer) {
  LocalEmbedderHeapTracer local_tracer(nullptr);
  // We should be able to call all functions without a remote tracer being
  // attached.
  EXPECT_FALSE(local_tracer.InUse());
  local_tracer.TracePrologue(EmbedderHeapTracer::TraceFlags::kNoFlags);
  local_tracer.EnterFinalPause();
  bool done = local_tracer.Trace(std::numeric_limits<double>::infinity());
  EXPECT_TRUE(done);
  local_tracer.TraceEpilogue();
}

TEST(LocalEmbedderHeapTracer, TracePrologueForwards) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(nullptr);
  local_tracer.SetRemoteTracer(&remote_tracer);
  EXPECT_CALL(remote_tracer, TracePrologue(_));
  local_tracer.TracePrologue(EmbedderHeapTracer::TraceFlags::kNoFlags);
}

TEST(LocalEmbedderHeapTracer, TracePrologueForwardsMemoryReducingFlag) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(nullptr);
  local_tracer.SetRemoteTracer(&remote_tracer);
  EXPECT_CALL(remote_tracer,
              TracePrologue(EmbedderHeapTracer::TraceFlags::kReduceMemory));
  local_tracer.TracePrologue(EmbedderHeapTracer::TraceFlags::kReduceMemory);
}

TEST(LocalEmbedderHeapTracer, TraceEpilogueForwards) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(nullptr);
  local_tracer.SetRemoteTracer(&remote_tracer);
  EXPECT_CALL(remote_tracer, TraceEpilogue(_));
  local_tracer.TraceEpilogue();
}

TEST(LocalEmbedderHeapTracer, EnterFinalPauseForwards) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(nullptr);
  local_tracer.SetRemoteTracer(&remote_tracer);
  EXPECT_CALL(remote_tracer, EnterFinalPause(_));
  local_tracer.EnterFinalPause();
}

TEST(LocalEmbedderHeapTracer, IsRemoteTracingDoneForwards) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(nullptr);
  local_tracer.SetRemoteTracer(&remote_tracer);
  EXPECT_CALL(remote_tracer, IsTracingDone());
  local_tracer.IsRemoteTracingDone();
}

TEST(LocalEmbedderHeapTracer, EnterFinalPauseDefaultStackStateUnkown) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(nullptr);
  local_tracer.SetRemoteTracer(&remote_tracer);
  // The default stack state is expected to be unkown.
  EXPECT_CALL(
      remote_tracer,
      EnterFinalPause(
          EmbedderHeapTracer::EmbedderStackState::kMayContainHeapPointers));
  local_tracer.EnterFinalPause();
}

TEST_F(LocalEmbedderHeapTracerWithIsolate,
       EnterFinalPauseStackStateIsForwarded) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(isolate());
  local_tracer.SetRemoteTracer(&remote_tracer);
  EmbedderStackStateScope scope =
      EmbedderStackStateScope::ExplicitScopeForTesting(
          &local_tracer,
          EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers);
  EXPECT_CALL(
      remote_tracer,
      EnterFinalPause(EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers));
  local_tracer.EnterFinalPause();
}

TEST_F(LocalEmbedderHeapTracerWithIsolate, TemporaryEmbedderStackState) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(isolate());
  local_tracer.SetRemoteTracer(&remote_tracer);
  // Default is unknown, see above.
  {
    EmbedderStackStateScope scope =
        EmbedderStackStateScope::ExplicitScopeForTesting(
            &local_tracer,
            EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers);
    EXPECT_CALL(remote_tracer,
                EnterFinalPause(
                    EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers));
    local_tracer.EnterFinalPause();
  }
}

TEST_F(LocalEmbedderHeapTracerWithIsolate,
       TemporaryEmbedderStackStateRestores) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(isolate());
  local_tracer.SetRemoteTracer(&remote_tracer);
  // Default is unknown, see above.
  {
    EmbedderStackStateScope scope =
        EmbedderStackStateScope::ExplicitScopeForTesting(
            &local_tracer,
            EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers);
    {
      EmbedderStackStateScope nested_scope =
          EmbedderStackStateScope::ExplicitScopeForTesting(
              &local_tracer,
              EmbedderHeapTracer::EmbedderStackState::kMayContainHeapPointers);
      EXPECT_CALL(
          remote_tracer,
          EnterFinalPause(
              EmbedderHeapTracer::EmbedderStackState::kMayContainHeapPointers));
      local_tracer.EnterFinalPause();
    }
    EXPECT_CALL(remote_tracer,
                EnterFinalPause(
                    EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers));
    local_tracer.EnterFinalPause();
  }
}

TEST_F(LocalEmbedderHeapTracerWithIsolate, TraceEpilogueStackStateResets) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(isolate());
  local_tracer.SetRemoteTracer(&remote_tracer);
  EmbedderStackStateScope scope =
      EmbedderStackStateScope::ExplicitScopeForTesting(
          &local_tracer,
          EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers);
  EXPECT_CALL(
      remote_tracer,
      EnterFinalPause(EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers));
  local_tracer.EnterFinalPause();
  EXPECT_CALL(remote_tracer, TraceEpilogue(_));
  local_tracer.TraceEpilogue();
  EXPECT_CALL(
      remote_tracer,
      EnterFinalPause(
          EmbedderHeapTracer::EmbedderStackState::kMayContainHeapPointers));
  local_tracer.EnterFinalPause();
}

TEST(LocalEmbedderHeapTracer, IsRemoteTracingDoneIncludesRemote) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(nullptr);
  local_tracer.SetRemoteTracer(&remote_tracer);
  EXPECT_CALL(remote_tracer, IsTracingDone());
  local_tracer.IsRemoteTracingDone();
}

TEST(LocalEmbedderHeapTracer, RegisterV8ReferencesWithRemoteTracer) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(nullptr);
  local_tracer.SetRemoteTracer(&remote_tracer);
  {
    LocalEmbedderHeapTracer::ProcessingScope scope(&local_tracer);
    scope.AddWrapperInfoForTesting(CreateWrapperInfo());
    EXPECT_CALL(remote_tracer, RegisterV8References(_));
  }
  EXPECT_CALL(remote_tracer, IsTracingDone()).WillOnce(Return(false));
  EXPECT_FALSE(local_tracer.IsRemoteTracingDone());
}

TEST_F(LocalEmbedderHeapTracerWithIsolate, SetRemoteTracerSetsIsolate) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  LocalEmbedderHeapTracer local_tracer(isolate());
  local_tracer.SetRemoteTracer(&remote_tracer);
  EXPECT_EQ(isolate(), reinterpret_cast<Isolate*>(remote_tracer.isolate()));
}

TEST_F(LocalEmbedderHeapTracerWithIsolate, DestructorClearsIsolate) {
  StrictMock<MockEmbedderHeapTracer> remote_tracer;
  {
    LocalEmbedderHeapTracer local_tracer(isolate());
    local_tracer.SetRemoteTracer(&remote_tracer);
    EXPECT_EQ(isolate(), reinterpret_cast<Isolate*>(remote_tracer.isolate()));
  }
  EXPECT_EQ(nullptr, remote_tracer.isolate());
}

namespace {

v8::Local<v8::Object> ConstructTraceableJSApiObject(
    v8::Local<v8::Context> context, void* first_field, void* second_field) {
  v8::EscapableHandleScope scope(context->GetIsolate());
  v8::Local<v8::FunctionTemplate> function_t =
      v8::FunctionTemplate::New(context->GetIsolate());
  v8::Local<v8::ObjectTemplate> instance_t = function_t->InstanceTemplate();
  instance_t->SetInternalFieldCount(2);
  v8::Local<v8::Function> function =
      function_t->GetFunction(context).ToLocalChecked();
  v8::Local<v8::Object> instance =
      function->NewInstance(context).ToLocalChecked();
  instance->SetAlignedPointerInInternalField(0, first_field);
  instance->SetAlignedPointerInInternalField(1, second_field);
  EXPECT_FALSE(instance.IsEmpty());
  i::Handle<i::JSReceiver> js_obj = v8::Utils::OpenHandle(*instance);
  EXPECT_EQ(i::JS_API_OBJECT_TYPE, js_obj->map().instance_type());
  return scope.Escape(instance);
}

enum class TracePrologueBehavior { kNoop, kCallV8WriteBarrier };

START_ALLOW_USE_DEPRECATED()

class TestEmbedderHeapTracer final : public v8::EmbedderHeapTracer {
 public:
  TestEmbedderHeapTracer() = default;
  TestEmbedderHeapTracer(TracePrologueBehavior prologue_behavior,
                         v8::Global<v8::Array> array)
      : prologue_behavior_(prologue_behavior), array_(std::move(array)) {}

  void RegisterV8References(
      const std::vector<std::pair<void*, void*>>& embedder_fields) final {
    registered_from_v8_.insert(registered_from_v8_.end(),
                               embedder_fields.begin(), embedder_fields.end());
  }

  void AddReferenceForTracing(v8::TracedReference<v8::Value>* ref) {
    to_register_with_v8_references_.push_back(ref);
  }

  bool AdvanceTracing(double deadline_in_ms) final {
    for (auto ref : to_register_with_v8_references_) {
      RegisterEmbedderReference(ref->As<v8::Data>());
    }
    to_register_with_v8_references_.clear();
    return true;
  }

  bool IsTracingDone() final { return to_register_with_v8_references_.empty(); }

  void TracePrologue(EmbedderHeapTracer::TraceFlags) final {
    if (prologue_behavior_ == TracePrologueBehavior::kCallV8WriteBarrier) {
      auto local = array_.Get(isolate());
      local
          ->Set(local->GetCreationContext().ToLocalChecked(), 0,
                v8::Object::New(isolate()))
          .Check();
    }
  }

  void TraceEpilogue(TraceSummary*) final {}
  void EnterFinalPause(EmbedderStackState) final {}

  bool IsRegisteredFromV8(void* first_field) const {
    for (auto pair : registered_from_v8_) {
      if (pair.first == first_field) return true;
    }
    return false;
  }

  void DoNotConsiderAsRootForScavenge(v8::TracedReference<v8::Value>* handle) {
    handle->SetWrapperClassId(17);
    non_root_handles_.push_back(handle);
  }

  bool IsRootForNonTracingGC(
      const v8::TracedReference<v8::Value>& handle) final {
    return handle.WrapperClassId() != 17;
  }

  void ResetHandleInNonTracingGC(
      const v8::TracedReference<v8::Value>& handle) final {
    for (auto* non_root_handle : non_root_handles_) {
      if (*non_root_handle == handle) {
        non_root_handle->Reset();
      }
    }
  }

 private:
  std::vector<std::pair<void*, void*>> registered_from_v8_;
  std::vector<v8::TracedReference<v8::Value>*> to_register_with_v8_references_;
  TracePrologueBehavior prologue_behavior_ = TracePrologueBehavior::kNoop;
  v8::Global<v8::Array> array_;
  std::vector<v8::TracedReference<v8::Value>*> non_root_handles_;
};

class V8_NODISCARD TemporaryEmbedderHeapTracerScope final {
 public:
  TemporaryEmbedderHeapTracerScope(v8::Isolate* isolate,
                                   v8::EmbedderHeapTracer* tracer)
      : isolate_(isolate) {
    isolate_->SetEmbedderHeapTracer(tracer);
  }

  ~TemporaryEmbedderHeapTracerScope() {
    isolate_->SetEmbedderHeapTracer(nullptr);
  }

 private:
  v8::Isolate* const isolate_;
};

END_ALLOW_USE_DEPRECATED()

}  // namespace

using EmbedderTracingTest = TestWithHeapInternalsAndContext;

TEST_F(EmbedderTracingTest, V8RegisterEmbedderReference) {
  // Tests that wrappers are properly registered with the embedder heap
  // tracer.
  ManualGCScope manual_gc(i_isolate());
  TestEmbedderHeapTracer tracer;
  heap::TemporaryEmbedderHeapTracerScope tracer_scope(v8_isolate(), &tracer);
  v8::HandleScope scope(v8_isolate());
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);

  void* first_and_second_field = reinterpret_cast<void*>(0x2);
  v8::Local<v8::Object> api_object = ConstructTraceableJSApiObject(
      context, first_and_second_field, first_and_second_field);
  ASSERT_FALSE(api_object.IsEmpty());
  CollectGarbage(i::OLD_SPACE);
  EXPECT_TRUE(tracer.IsRegisteredFromV8(first_and_second_field));
}

TEST_F(EmbedderTracingTest, EmbedderRegisteringV8Reference) {
  // Tests that references that are registered by the embedder heap tracer are
  // considered live by V8.
  ManualGCScope manual_gc(i_isolate());
  TestEmbedderHeapTracer tracer;
  heap::TemporaryEmbedderHeapTracerScope tracer_scope(v8_isolate(), &tracer);
  v8::HandleScope scope(v8_isolate());
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);

  auto handle = std::make_unique<v8::TracedReference<v8::Value>>();
  {
    v8::HandleScope inner_scope(v8_isolate());
    v8::Local<v8::Value> o =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    handle->Reset(v8_isolate(), o);
  }
  tracer.AddReferenceForTracing(handle.get());
  CollectGarbage(i::OLD_SPACE);
  EXPECT_FALSE(handle->IsEmpty());
}

TEST_F(EmbedderTracingTest, FinalizeTracingIsNoopWhenNotMarking) {
  ManualGCScope manual_gc(i_isolate());
  TestEmbedderHeapTracer tracer;
  heap::TemporaryEmbedderHeapTracerScope tracer_scope(v8_isolate(), &tracer);

  // Finalize a potentially running garbage collection.
  CollectGarbage(OLD_SPACE);
  EXPECT_TRUE(i_isolate()->heap()->incremental_marking()->IsStopped());

  int gc_counter = i_isolate()->heap()->gc_count();
  tracer.FinalizeTracing();
  EXPECT_TRUE(i_isolate()->heap()->incremental_marking()->IsStopped());
  EXPECT_EQ(gc_counter, i_isolate()->heap()->gc_count());
}

TEST_F(EmbedderTracingTest, FinalizeTracingWhenMarking) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc(i_isolate());
  Heap* heap = i_isolate()->heap();
  TestEmbedderHeapTracer tracer;
  heap::TemporaryEmbedderHeapTracerScope tracer_scope(v8_isolate(), &tracer);

  // Finalize a potentially running garbage collection.
  CollectGarbage(OLD_SPACE);
  if (heap->sweeping_in_progress()) {
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
  }
  heap->tracer()->StopFullCycleIfNeeded();
  EXPECT_TRUE(heap->incremental_marking()->IsStopped());

  i::IncrementalMarking* marking = heap->incremental_marking();
  {
    IsolateSafepointScope scope(heap);
    heap->tracer()->StartCycle(
        GarbageCollector::MARK_COMPACTOR, GarbageCollectionReason::kTesting,
        "collector cctest", GCTracer::MarkingType::kIncremental);
    marking->Start(GarbageCollector::MARK_COMPACTOR,
                   GarbageCollectionReason::kTesting);
  }

  // Sweeping is not runing so we should immediately start marking.
  EXPECT_TRUE(marking->IsMarking());
  tracer.FinalizeTracing();
  EXPECT_TRUE(marking->IsStopped());
}

namespace {

void ConstructJSObject(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::TracedReference<v8::Object>* handle) {
  v8::HandleScope scope(isolate);
  v8::Local<v8::Object> object(v8::Object::New(isolate));
  EXPECT_FALSE(object.IsEmpty());
  *handle = v8::TracedReference<v8::Object>(isolate, object);
  EXPECT_FALSE(handle->IsEmpty());
}

}  // namespace

TEST_F(EmbedderTracingTest, TracedReferenceHandlesMarking) {
  ManualGCScope manual_gc(i_isolate());
  v8::HandleScope scope(v8_isolate());
  auto live = std::make_unique<v8::TracedReference<v8::Value>>();
  auto dead = std::make_unique<v8::TracedReference<v8::Value>>();
  live->Reset(v8_isolate(), v8::Undefined(v8_isolate()));
  dead->Reset(v8_isolate(), v8::Undefined(v8_isolate()));
  auto* traced_handles = i_isolate()->traced_handles();
  {
    TestEmbedderHeapTracer tracer;
    heap::TemporaryEmbedderHeapTracerScope tracer_scope(v8_isolate(), &tracer);
    tracer.AddReferenceForTracing(live.get());
    const size_t initial_count = traced_handles->used_node_count();
    {
      // Conservative scanning may find stale pointers to on-stack handles.
      // Disable scanning, assuming the slots are overwritten.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(
          i_isolate()->heap());
      EmbedderStackStateScope scope =
          EmbedderStackStateScope::ExplicitScopeForTesting(
              reinterpret_cast<i::Isolate*>(v8_isolate())
                  ->heap()
                  ->local_embedder_heap_tracer(),
              EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers);
      FullGC();
    }
    const size_t final_count = traced_handles->used_node_count();
    // Handles are not black allocated, so `dead` is immediately reclaimed.
    EXPECT_EQ(initial_count, final_count + 1);
  }
}

namespace {

START_ALLOW_USE_DEPRECATED()

class TracedReferenceVisitor final
    : public v8::EmbedderHeapTracer::TracedGlobalHandleVisitor {
 public:
  ~TracedReferenceVisitor() override = default;

  void VisitTracedReference(const TracedReference<Value>& value) final {
    if (value.WrapperClassId() == 57) {
      count_++;
    }
  }

  size_t count() const { return count_; }

 private:
  size_t count_ = 0;
};

END_ALLOW_USE_DEPRECATED()

}  // namespace

TEST_F(EmbedderTracingTest, TracedReferenceIteration) {
  ManualGCScope manual_gc(i_isolate());
  v8::HandleScope scope(v8_isolate());
  TestEmbedderHeapTracer tracer;
  heap::TemporaryEmbedderHeapTracerScope tracer_scope(v8_isolate(), &tracer);

  auto handle = std::make_unique<v8::TracedReference<v8::Object>>();
  ConstructJSObject(v8_isolate(), v8_isolate()->GetCurrentContext(),
                    handle.get());
  EXPECT_FALSE(handle->IsEmpty());
  handle->SetWrapperClassId(57);
  TracedReferenceVisitor visitor;
  {
    v8::HandleScope new_scope(v8_isolate());
    tracer.IterateTracedGlobalHandles(&visitor);
  }
  EXPECT_EQ(1u, visitor.count());
}

TEST_F(EmbedderTracingTest, TracePrologueCallingIntoV8WriteBarrier) {
  // Regression test: https://crbug.com/940003
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc(isolate());
  v8::HandleScope scope(v8_isolate());
  v8::Global<v8::Array> global;
  {
    v8::HandleScope new_scope(v8_isolate());
    auto local = v8::Array::New(v8_isolate(), 10);
    global.Reset(v8_isolate(), local);
  }
  TestEmbedderHeapTracer tracer(TracePrologueBehavior::kCallV8WriteBarrier,
                                std::move(global));
  TemporaryEmbedderHeapTracerScope tracer_scope(v8_isolate(), &tracer);
  SimulateIncrementalMarking();
  // Finish GC to avoid removing the tracer while GC is running which may end up
  // in an infinite loop because of unprocessed objects.
  FullGC();
}

TEST_F(EmbedderTracingTest, BasicTracedReference) {
  ManualGCScope manual_gc(i_isolate());
  v8::HandleScope scope(v8_isolate());
  TestEmbedderHeapTracer tracer;
  heap::TemporaryEmbedderHeapTracerScope tracer_scope(v8_isolate(), &tracer);
  tracer.SetStackStart(
      static_cast<void*>(base::Stack::GetCurrentFrameAddress()));
  auto* traced_handles = i_isolate()->traced_handles();

  const size_t initial_count = traced_handles->used_node_count();
  char* memory = new char[sizeof(v8::TracedReference<v8::Value>)];
  auto* traced = new (memory) v8::TracedReference<v8::Value>();
  {
    v8::HandleScope new_scope(v8_isolate());
    v8::Local<v8::Value> object(ConstructTraceableJSApiObject(
        v8_isolate()->GetCurrentContext(), nullptr, nullptr));
    EXPECT_TRUE(traced->IsEmpty());
    *traced = v8::TracedReference<v8::Value>(v8_isolate(), object);
    EXPECT_FALSE(traced->IsEmpty());
    EXPECT_EQ(initial_count + 1, traced_handles->used_node_count());
  }
  traced->~TracedReference<v8::Value>();
  EXPECT_EQ(initial_count + 1, traced_handles->used_node_count());
  {
    // Conservative scanning may find stale pointers to on-stack handles.
    // Disable scanning, assuming the slots are overwritten.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(
        i_isolate()->heap());
    EmbedderStackStateScope scope =
        EmbedderStackStateScope::ExplicitScopeForTesting(
            reinterpret_cast<i::Isolate*>(v8_isolate())
                ->heap()
                ->local_embedder_heap_tracer(),
            EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers);
    FullGC();
  }
  EXPECT_EQ(initial_count, traced_handles->used_node_count());
  delete[] memory;
}

}  // namespace heap
}  // namespace internal
}  // namespace v8
