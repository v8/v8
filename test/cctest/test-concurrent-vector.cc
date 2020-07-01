// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api.h"
#include "src/base/platform/semaphore.h"
#include "src/handles/handles-inl.h"
#include "src/handles/local-handles-inl.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {

namespace {

const int kCycles = 5;

class VectorExplorationThread final : public v8::base::Thread {
 public:
  VectorExplorationThread(Heap* heap, base::Semaphore* sema_started,
                          std::unique_ptr<PersistentHandles> ph,
                          Handle<JSFunction> function)
      : v8::base::Thread(base::Thread::Options("ThreadWithLocalHeap")),
        heap_(heap),
        function_(function),
        ph_(std::move(ph)),
        sema_started_(sema_started) {}

  void Run() override {
    LocalHeap local_heap(heap_, std::move(ph_));
    LocalHandleScope scope(&local_heap);
    // Get the feedback vector
    Handle<FeedbackVector> vector(function_->feedback_vector(), &local_heap);
    FeedbackSlot slot(0);

    sema_started_->Signal();
    for (int i = 0; i < kCycles; i++) {
      FeedbackNexusBackground nexus(
          BackgroundThreadConfig(vector, slot, &local_heap));
      auto state = nexus.ic_state();
      CHECK(state == UNINITIALIZED || state == MONOMORPHIC ||
            state == POLYMORPHIC || state == MEGAMORPHIC);

      if (state == MONOMORPHIC || state == POLYMORPHIC) {
        MapHandles maps;
        nexus.ExtractMaps(&maps);
        for (unsigned int i = 0; i < maps.size(); i++) {
          CHECK(maps[i]->IsMap());
        }
      }
    }
    CHECK(!ph_);
    ph_ = local_heap.DetachPersistentHandles();
  }

  Heap* heap_;
  Handle<JSFunction> function_;
  std::unique_ptr<PersistentHandles> ph_;
  base::Semaphore* sema_started_;
};

// Verify that a LoadIC can be cycled through different states and safely
// read on a background thread.
TEST(CheckLoadICStates) {
  CcTest::InitializeVM();
  FLAG_local_heaps = true;
  FLAG_lazy_feedback_allocation = false;
  Isolate* isolate = CcTest::i_isolate();

  std::unique_ptr<PersistentHandles> ph = isolate->NewPersistentHandles();
  HandleScope handle_scope(isolate);

  Handle<HeapObject> o1 = Handle<HeapObject>::cast(
      Utils::OpenHandle(*CompileRun("o1 = { bar: {} };")));
  Handle<HeapObject> o2 = Handle<HeapObject>::cast(
      Utils::OpenHandle(*CompileRun("o2 = { baz: 3, bar: 3 };")));
  Handle<HeapObject> o3 = Handle<HeapObject>::cast(
      Utils::OpenHandle(*CompileRun("o3 = { blu: 3, baz: 3, bar: 3 };")));
  Handle<HeapObject> o4 = Handle<HeapObject>::cast(Utils::OpenHandle(
      *CompileRun("o4 = { ble: 3, blu: 3, baz: 3, bar: 3 };")));
  auto result = CompileRun(
      "function foo(o) {"
      "  let a = o.bar;"
      "  return a;"
      "}"
      "foo(o1);"
      "foo;");
  Handle<JSFunction> function =
      Handle<JSFunction>::cast(Utils::OpenHandle(*result));
  Handle<FeedbackVector> vector(function->feedback_vector(), isolate);
  FeedbackSlot slot(0);
  FeedbackNexus nexus(vector, slot, isolate);
  CHECK(IsLoadICKind(nexus.kind()));
  CHECK_EQ(MONOMORPHIC, nexus.ic_state());
  nexus.ConfigureUninitialized();

  // Now the basic environment is set up. Start the worker thread.
  base::Semaphore sema_started(0);
  Handle<JSFunction> persistent_function =
      Handle<JSFunction>::cast(ph->NewHandle(function->ptr()));
  std::unique_ptr<VectorExplorationThread> thread(new VectorExplorationThread(
      isolate->heap(), &sema_started, std::move(ph), persistent_function));
  CHECK(thread->Start());
  sema_started.Wait();

  // Cycle the IC through all states repeatedly.
  MaybeObjectHandle dummy_handler(Smi::FromInt(10), isolate);
  for (int i = 0; i < kCycles; i++) {
    CHECK_EQ(UNINITIALIZED, nexus.ic_state());
    nexus.ConfigureMonomorphic(Handle<Name>(), Handle<Map>(o1->map(), isolate),
                               dummy_handler);
    CHECK_EQ(MONOMORPHIC, nexus.ic_state());
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(50));

    // Go polymorphic.
    std::vector<MapAndHandler> map_and_handlers;
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o1->map(), isolate), dummy_handler));
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o2->map(), isolate), dummy_handler));
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o3->map(), isolate), dummy_handler));
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o4->map(), isolate), dummy_handler));
    nexus.ConfigurePolymorphic(Handle<Name>(), map_and_handlers);
    CHECK_EQ(POLYMORPHIC, nexus.ic_state());
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(50));

    // Go Megamorphic
    nexus.ConfigureMegamorphic();
    CHECK_EQ(MEGAMORPHIC, nexus.ic_state());
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(50));

    nexus.ConfigureUninitialized();
  }

  thread->Join();
}

}  // anonymous namespace

}  // namespace internal
}  // namespace v8
