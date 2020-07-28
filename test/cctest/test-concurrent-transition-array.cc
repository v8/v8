// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api.h"
#include "src/base/platform/semaphore.h"
#include "src/handles/handles-inl.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"
#include "src/objects/transitions-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"
#include "test/cctest/test-transitions.h"

namespace v8 {
namespace internal {

namespace {

class ConcurrentSearchThread final : public v8::base::Thread {
 public:
  ConcurrentSearchThread(Heap* heap, base::Semaphore* sema_started,
                         std::unique_ptr<PersistentHandles> ph,
                         Handle<Name> name, Handle<Map> map,
                         base::Optional<Handle<Map>> result_map)
      : v8::base::Thread(base::Thread::Options("ThreadWithLocalHeap")),
        heap_(heap),
        sema_started_(sema_started),
        ph_(std::move(ph)),
        name_(name),
        map_(map),
        result_map_(result_map) {}

  void Run() override {
    LocalHeap local_heap(heap_, std::move(ph_));

    sema_started_->Signal();

    CHECK_EQ(TransitionsAccessor(CcTest::i_isolate(), map_, true)
                 .SearchTransition(*name_, kData, NONE),
             result_map_ ? **result_map_ : Map());

    CHECK(!ph_);
    ph_ = local_heap.DetachPersistentHandles();
  }

  Heap* heap_;
  base::Semaphore* sema_started_;
  std::unique_ptr<PersistentHandles> ph_;
  Handle<Name> name_;
  Handle<Map> map_;
  base::Optional<Handle<Map>> result_map_;
};

// Search on the main thread and in the background thread at the same time.
TEST(FullFieldTransitions_OnlySearch) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<String> name = CcTest::MakeString("name");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = kData;

  // Set map0 to be a full transition array with transition 'name' to map1.
  Handle<Map> map0 = Map::Create(isolate, 0);
  Handle<Map> map1 =
      Map::CopyWithField(isolate, map0, name, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  TransitionsAccessor(isolate, map0).Insert(name, map1, PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(isolate, map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = isolate->NewPersistentHandles();

  Handle<Name> persistent_name = ph->NewHandle(name);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);
  Handle<Map> persistent_result_map1 = ph->NewHandle(map1);

  base::Semaphore sema_started(0);

  // Pass persistent handles to background thread.
  std::unique_ptr<ConcurrentSearchThread> thread(new ConcurrentSearchThread(
      isolate->heap(), &sema_started, std::move(ph), persistent_name,
      persistent_map0, persistent_result_map1));
  CHECK(thread->Start());

  sema_started.Wait();

  CHECK_EQ(*map1, TransitionsAccessor(isolate, map0)
                      .SearchTransition(*name, kind, attributes));

  thread->Join();
}

// Search and insert on the main thread, while the background thread searches at
// the same time.
TEST(FullFieldTransitions) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<String> name1 = CcTest::MakeString("name1");
  Handle<String> name2 = CcTest::MakeString("name2");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = kData;

  // Set map0 to be a full transition array with transition 'name1' to map1.
  Handle<Map> map0 = Map::Create(isolate, 0);
  Handle<Map> map1 =
      Map::CopyWithField(isolate, map0, name1, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  Handle<Map> map2 =
      Map::CopyWithField(isolate, map0, name2, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  TransitionsAccessor(isolate, map0).Insert(name1, map1, PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(isolate, map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = isolate->NewPersistentHandles();

  Handle<Name> persistent_name1 = ph->NewHandle(name1);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);
  Handle<Map> persistent_result_map1 = ph->NewHandle(map1);

  base::Semaphore sema_started(0);

  // Pass persistent handles to background thread.
  std::unique_ptr<ConcurrentSearchThread> thread(new ConcurrentSearchThread(
      isolate->heap(), &sema_started, std::move(ph), persistent_name1,
      persistent_map0, persistent_result_map1));
  CHECK(thread->Start());

  sema_started.Wait();

  CHECK_EQ(*map1, TransitionsAccessor(isolate, map0)
                      .SearchTransition(*name1, kind, attributes));
  TransitionsAccessor(isolate, map0).Insert(name2, map2, PROPERTY_TRANSITION);
  CHECK_EQ(*map2, TransitionsAccessor(isolate, map0)
                      .SearchTransition(*name2, kind, attributes));

  thread->Join();
}

// Search and insert on the main thread which changes the encoding from kWeakRef
// to kFullTransitionArray, while the background thread searches at the same
// time.
TEST(WeakRefToFullFieldTransitions) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<String> name1 = CcTest::MakeString("name1");
  Handle<String> name2 = CcTest::MakeString("name2");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = kData;

  // Set map0 to be a simple transition array with transition 'name1' to map1.
  Handle<Map> map0 = Map::Create(isolate, 0);
  Handle<Map> map1 =
      Map::CopyWithField(isolate, map0, name1, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  Handle<Map> map2 =
      Map::CopyWithField(isolate, map0, name2, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  TransitionsAccessor(isolate, map0)
      .Insert(name1, map1, SIMPLE_PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(isolate, map0);
    CHECK(transitions.IsWeakRefEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = isolate->NewPersistentHandles();

  Handle<Name> persistent_name1 = ph->NewHandle(name1);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);
  Handle<Map> persistent_result_map1 = ph->NewHandle(map1);

  base::Semaphore sema_started(0);

  // Pass persistent handles to background thread.
  std::unique_ptr<ConcurrentSearchThread> thread(new ConcurrentSearchThread(
      isolate->heap(), &sema_started, std::move(ph), persistent_name1,
      persistent_map0, persistent_result_map1));
  CHECK(thread->Start());

  sema_started.Wait();

  CHECK_EQ(*map1, TransitionsAccessor(isolate, map0)
                      .SearchTransition(*name1, kind, attributes));
  TransitionsAccessor(isolate, map0)
      .Insert(name2, map2, SIMPLE_PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(isolate, map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }
  CHECK_EQ(*map2, TransitionsAccessor(isolate, map0)
                      .SearchTransition(*name2, kind, attributes));

  thread->Join();
}

// Search and insert on the main thread, while the background thread searches at
// the same time. In this case, we have a kFullTransitionArray with enough slack
// when we are concurrently writing.
TEST(FullFieldTransitions_withSlack) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<String> name1 = CcTest::MakeString("name1");
  Handle<String> name2 = CcTest::MakeString("name2");
  Handle<String> name3 = CcTest::MakeString("name3");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = kData;

  // Set map0 to be a full transition array with transition 'name1' to map1.
  Handle<Map> map0 = Map::Create(isolate, 0);
  Handle<Map> map1 =
      Map::CopyWithField(isolate, map0, name1, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  Handle<Map> map2 =
      Map::CopyWithField(isolate, map0, name2, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  Handle<Map> map3 =
      Map::CopyWithField(isolate, map0, name3, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  TransitionsAccessor(isolate, map0).Insert(name1, map1, PROPERTY_TRANSITION);
  TransitionsAccessor(isolate, map0).Insert(name2, map2, PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(isolate, map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = isolate->NewPersistentHandles();

  Handle<Name> persistent_name1 = ph->NewHandle(name1);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);
  Handle<Map> persistent_result_map1 = ph->NewHandle(map1);

  base::Semaphore sema_started(0);

  // Pass persistent handles to background thread.
  std::unique_ptr<ConcurrentSearchThread> thread(new ConcurrentSearchThread(
      isolate->heap(), &sema_started, std::move(ph), persistent_name1,
      persistent_map0, persistent_result_map1));
  CHECK(thread->Start());

  sema_started.Wait();

  CHECK_EQ(*map1, TransitionsAccessor(isolate, map0)
                      .SearchTransition(*name1, kind, attributes));
  CHECK_EQ(*map2, TransitionsAccessor(isolate, map0)
                      .SearchTransition(*name2, kind, attributes));
  {
    // Check that we have enough slack for the 3rd insertion into the
    // TransitionArray.
    TestTransitionsAccessor transitions(isolate, map0);
    CHECK_GE(transitions.Capacity(), 3);
  }
  TransitionsAccessor(isolate, map0).Insert(name3, map3, PROPERTY_TRANSITION);
  CHECK_EQ(*map3, TransitionsAccessor(isolate, map0)
                      .SearchTransition(*name3, kind, attributes));

  thread->Join();
}

// Search and insert on the main thread which changes the encoding from
// kUninitialized to kFullTransitionArray, while the background thread searches
// at the same time.
TEST(UninitializedToFullFieldTransitions) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<String> name1 = CcTest::MakeString("name1");
  Handle<String> name2 = CcTest::MakeString("name2");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = kData;

  // Set map0 to be a full transition array with transition 'name1' to map1.
  Handle<Map> map0 = Map::Create(isolate, 0);
  Handle<Map> map1 =
      Map::CopyWithField(isolate, map0, name1, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  {
    TestTransitionsAccessor transitions(isolate, map0);
    CHECK(transitions.IsUninitializedEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = isolate->NewPersistentHandles();

  Handle<Name> persistent_name2 = ph->NewHandle(name2);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);

  base::Semaphore background_thread_started(0);

  // Pass persistent handles to background thread.
  // Background thread will search for name2, guaranteed to *not* be on the map.
  std::unique_ptr<ConcurrentSearchThread> thread(new ConcurrentSearchThread(
      isolate->heap(), &background_thread_started, std::move(ph),
      persistent_name2, persistent_map0, base::nullopt));
  CHECK(thread->Start());

  background_thread_started.Wait();

  TransitionsAccessor(isolate, map0).Insert(name1, map1, PROPERTY_TRANSITION);
  CHECK_EQ(*map1, TransitionsAccessor(isolate, map0)
                      .SearchTransition(*name1, kind, attributes));
  {
    TestTransitionsAccessor transitions(isolate, map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }
  thread->Join();
}

}  // anonymous namespace

}  // namespace internal
}  // namespace v8
