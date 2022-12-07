// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/conservative-stack-visitor.h"

#include "src/base/optional.h"
#include "src/base/platform/semaphore.h"
#include "src/heap/parked-scope.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

namespace {

class RecordingVisitor final : public RootVisitor {
 public:
  V8_NOINLINE explicit RecordingVisitor(Isolate* isolate) {
    // Allocate the object.
    auto h = isolate->factory()->NewFixedArray(256, AllocationType::kOld);
    the_object_ = h->GetHeapObject();
    base_address_ = the_object_.address();
    tagged_address_ = the_object_.ptr();
    inner_address_ = base_address_ + 42 * kTaggedSize;
#ifdef V8_COMPRESS_POINTERS
    compr_address_ = static_cast<uint32_t>(
        V8HeapCompressionScheme::CompressTagged(base_address_));
    compr_inner_ = static_cast<uint32_t>(
        V8HeapCompressionScheme::CompressTagged(inner_address_));
#else
    compr_address_ = static_cast<uint32_t>(base_address_);
    compr_inner_ = static_cast<uint32_t>(inner_address_);
#endif
  }

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot current = start; current != end; ++current) {
      if (*current == the_object_) found_ = true;
    }
  }

  void Reset() { found_ = false; }
  bool found() const { return found_; }

  Address base_address() const { return base_address_; }
  Address tagged_address() const { return tagged_address_; }
  Address inner_address() const { return inner_address_; }
  uint32_t compr_address() const { return compr_address_; }
  uint32_t compr_inner() const { return compr_inner_; }

 private:
  // Some heap object that we want to check if it is visited or not.
  HeapObject the_object_;

  // Addresses of this object.
  Address base_address_;    // Uncompressed base address
  Address tagged_address_;  // Tagged uncompressed base address
  Address inner_address_;   // Some inner address
  uint32_t compr_address_;  // Compressed base address
  uint32_t compr_inner_;    // Compressed inner address

  // Has the object been found?
  bool found_ = false;
};

}  // namespace

using ConservativeStackVisitorTest = TestWithHeapInternalsAndContext;

// In the following, we avoid negative tests, i.e., tests checking that objects
// are not visited when there are no pointers to them on the stack. Such tests
// are generally fragile and could fail on some platforms because of unforeseen
// compiler optimizations. In general we cannot ensure in a portable way that
// no pointer remained on the stack (or in some register) after the
// initialization of RecordingVisitor and until the invocation of
// Stack::IteratePointers.

TEST_F(ConservativeStackVisitorTest, DirectBasePointer) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile Address ptr = recorder->base_address();

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    SaveStackContextScope stack_context_scope(&heap()->stack());
    isolate()->heap()->stack().IteratePointers(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(kNullAddress, ptr);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

TEST_F(ConservativeStackVisitorTest, TaggedBasePointer) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile Address ptr = recorder->tagged_address();

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    SaveStackContextScope stack_context_scope(&heap()->stack());
    isolate()->heap()->stack().IteratePointers(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(kNullAddress, ptr);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

TEST_F(ConservativeStackVisitorTest, InnerPointer) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile Address ptr = recorder->inner_address();

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    SaveStackContextScope stack_context_scope(&heap()->stack());
    isolate()->heap()->stack().IteratePointers(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(kNullAddress, ptr);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

#ifdef V8_COMPRESS_POINTERS

TEST_F(ConservativeStackVisitorTest, HalfWord1) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t ptr[] = {recorder->compr_address(), 0};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    SaveStackContextScope stack_context_scope(&heap()->stack());
    isolate()->heap()->stack().IteratePointers(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr[0]);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

TEST_F(ConservativeStackVisitorTest, HalfWord2) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t ptr[] = {0, recorder->compr_address()};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    SaveStackContextScope stack_context_scope(&heap()->stack());
    isolate()->heap()->stack().IteratePointers(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr[1]);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

TEST_F(ConservativeStackVisitorTest, InnerHalfWord1) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t ptr[] = {recorder->compr_inner(), 0};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    SaveStackContextScope stack_context_scope(&heap()->stack());
    isolate()->heap()->stack().IteratePointers(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr[0]);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

TEST_F(ConservativeStackVisitorTest, InnerHalfWord2) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t ptr[] = {0, recorder->compr_inner()};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    SaveStackContextScope stack_context_scope(&heap()->stack());
    isolate()->heap()->stack().IteratePointers(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr[1]);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

#endif  // V8_COMPRESS_POINTERS

#if V8_CAN_CREATE_SHARED_HEAP_BOOL

using ConservativeStackScanningSharedTest = TestJSSharedMemoryWithIsolate;

namespace {

// An abstract class for threads that will be used in tests related to
// conservative stack scanning of the shared heap. When running, after
// initialization, it invokes the virtual method `RunTheTest`. The class
// provides basic functionality for allocating an object on the shared heap,
// synchronizing with the main thread (which triggers a GC), and verifying that
// the object has (or has not) been reclaimed by the GC.
class TestStackContextWithSharedHeapThread : public ParkingThread {
 public:
  TestStackContextWithSharedHeapThread(const char* name, StackState stack_state,
                                       v8::base::Semaphore* sema_ready,
                                       v8::base::Semaphore* sema_gc_complete)

      : ParkingThread(base::Thread::Options(name)),
        stack_state_(stack_state),
        sema_ready_(sema_ready),
        sema_gc_complete_(sema_gc_complete) {}

  void Run() override {
    IsolateWrapper isolate_wrapper(kNoCounters);
    Isolate* i_client_isolate =
        reinterpret_cast<Isolate*>(isolate_wrapper.isolate());

    base::Optional<DisableConservativeStackScanningScopeForTesting> scope;
    if (stack_state_ == StackState::kNoHeapPointers)
      scope.emplace(i_client_isolate->heap());

    RunTheTest(i_client_isolate);
  }

  virtual void RunTheTest(Isolate* i_client_isolate) = 0;

  // Signal to the main thread to invoke a shared GC, then wait in a safepoint
  // until the GC is finished.
  V8_INLINE void SignalReadyAndWait(Isolate* i_client_isolate) {
    sema_ready_->Signal();
    const auto timeout = base::TimeDelta::FromMilliseconds(100);
    do {
      i_client_isolate->main_thread_local_isolate()->heap()->Safepoint();
    } while (!sema_gc_complete_->WaitFor(timeout));
  }

  // Allocate an object on the shared heap and add a weak reference.
  // Also, allocate some garbage. Return the address of the allocated object.
  V8_INLINE Address AllocateObjectAndGarbage(Isolate* i_client_isolate,
                                             Persistent<v8::FixedArray>& weak) {
    HandleScope handle_scope(i_client_isolate);
    Handle<FixedArray> h = i_client_isolate->factory()->NewFixedArray(
        256, AllocationType::kSharedOld);
    weak.Reset(reinterpret_cast<v8::Isolate*>(i_client_isolate),
               Utils::FixedArrayToLocal(h));
    weak.SetWeak();

    // Allocate some garbage on the shared heap.
    for (int i = 0; i < 10; ++i) {
      i_client_isolate->factory()->NewFixedArray(256,
                                                 AllocationType::kSharedOld);
    }

    return h->GetHeapObject().address();
  }

  // Check whether an object has been reclaimed by GC.
  V8_INLINE void VerifyObject(const Persistent<v8::FixedArray>& weak) {
    switch (stack_state_) {
      case StackState::kNoHeapPointers:
        EXPECT_TRUE(weak.IsEmpty());
        break;
      case StackState::kMayContainHeapPointers:
        EXPECT_FALSE(weak.IsEmpty());
        break;
    }
  }

  bool IsPreciseGC() const {
    return stack_state_ == StackState::kNoHeapPointers;
  }

 private:
  StackState stack_state_;
  v8::base::Semaphore* sema_ready_;
  v8::base::Semaphore* sema_gc_complete_;
};

// Generic test template for conservative stack scanning of the shared heap. The
// `TestThread` must be a subclass of `TestStackContextWithSharedHeapThread`.
template <typename TestThread>
void StackContextWithSharedHeapTest(Isolate* isolate, StackState stack_state) {
  v8::base::Semaphore sema_thread_ready(0);
  v8::base::Semaphore sema_gc_complete(0);

  auto thread = std::make_unique<TestThread>(stack_state, &sema_thread_ready,
                                             &sema_gc_complete);
  CHECK(thread->Start());

  // Wait for the thread to be ready.
  sema_thread_ready.Wait();

  // Invoke shared garbage collection.
  isolate->heap()->CollectGarbageShared(isolate->main_thread_local_heap(),
                                        GarbageCollectionReason::kTesting);

  // Signal that the GC has been complete.
  sema_gc_complete.Signal();

  ParkedScope scope(isolate->main_thread_local_isolate());
  thread->ParkedJoin(scope);
}

// Test scenario #1: The thread just waits, so it is forced into a safepoint.
class TestWaitThread final : public TestStackContextWithSharedHeapThread {
 public:
  TestWaitThread(StackState stack_state, v8::base::Semaphore* sema_ready,
                 v8::base::Semaphore* sema_gc_complete)
      : TestStackContextWithSharedHeapThread("TestWaitThread", stack_state,
                                             sema_ready, sema_gc_complete) {}

  void RunTheTest(Isolate* i_client_isolate) override {
    Persistent<v8::FixedArray> weak;
    volatile Address ptr_on_stack =
        AllocateObjectAndGarbage(i_client_isolate, weak);

    SignalReadyAndWait(i_client_isolate);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr_on_stack);

    VerifyObject(weak);
  }
};

// Test scenario #2: The thread parks and waits.
class TestParkWaitThread final : public TestStackContextWithSharedHeapThread {
 public:
  TestParkWaitThread(StackState stack_state, v8::base::Semaphore* sema_ready,
                     v8::base::Semaphore* sema_gc_complete)
      : TestStackContextWithSharedHeapThread("TestParkWaitThread", stack_state,
                                             sema_ready, sema_gc_complete) {}

  void RunTheTest(Isolate* i_client_isolate) override {
    Persistent<v8::FixedArray> weak;
    volatile Address ptr_on_stack =
        AllocateObjectAndGarbage(i_client_isolate, weak);

    ParkedScope parked_scope(i_client_isolate->main_thread_local_isolate());
    SignalReadyAndWait(i_client_isolate);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr_on_stack);

    VerifyObject(weak);
  }
};

// Test scenario #3: The thread parks, then unparks and waits, so it is forced
// into a safepoint.
class TestParkUnparkWaitThread final
    : public TestStackContextWithSharedHeapThread {
 public:
  TestParkUnparkWaitThread(StackState stack_state,
                           v8::base::Semaphore* sema_ready,
                           v8::base::Semaphore* sema_gc_complete)
      : TestStackContextWithSharedHeapThread("TestParkUnparkWaitThread",
                                             stack_state, sema_ready,
                                             sema_gc_complete) {}

  void RunTheTest(Isolate* i_client_isolate) override {
    Persistent<v8::FixedArray> weak;
    volatile Address ptr_on_stack =
        AllocateObjectAndGarbage(i_client_isolate, weak);

    ParkedScope parked_scope(i_client_isolate->main_thread_local_isolate());

    // Call KeepRunning, which is not inlined, to add a frame on the stack.
    KeepRunning(i_client_isolate);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr_on_stack);

    VerifyObject(weak);
  }

  V8_NOINLINE void KeepRunning(Isolate* i_client_isolate) {
    UnparkedScope unparked_scope(i_client_isolate->main_thread_local_isolate());

    Persistent<v8::FixedArray> weak;
    volatile Address ptr_on_stack =
        AllocateObjectAndGarbage(i_client_isolate, weak);

    SignalReadyAndWait(i_client_isolate);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr_on_stack);

    VerifyObject(weak);
  }
};

// Test scenario #4: The thread parks, then unparks, then parks again and waits.
class TestParkUnparkParkWaitThread final
    : public TestStackContextWithSharedHeapThread {
 public:
  TestParkUnparkParkWaitThread(StackState stack_state,
                               v8::base::Semaphore* sema_ready,
                               v8::base::Semaphore* sema_gc_complete)
      : TestStackContextWithSharedHeapThread("TestParkUnparkParkWaitThread",
                                             stack_state, sema_ready,
                                             sema_gc_complete) {}

  void RunTheTest(Isolate* i_client_isolate) override {
    Persistent<v8::FixedArray> weak;
    volatile Address ptr_on_stack =
        AllocateObjectAndGarbage(i_client_isolate, weak);

    ParkedScope parked_scope(i_client_isolate->main_thread_local_isolate());

    // Call KeepRunning, which is not inlined, to add a frame on the stack.
    KeepRunning(i_client_isolate);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr_on_stack);

    VerifyObject(weak);
  }

  V8_NOINLINE void KeepRunning(Isolate* i_client_isolate) {
    UnparkedScope unparked_scope(i_client_isolate->main_thread_local_isolate());

    Persistent<v8::FixedArray> weak;
    volatile Address ptr_on_stack =
        AllocateObjectAndGarbage(i_client_isolate, weak);

    // Call KeepRunningStill, which is not inlined, to add one more frame on the
    // stack.
    KeepRunningStill(i_client_isolate);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr_on_stack);

    VerifyObject(weak);
  }

  V8_NOINLINE void KeepRunningStill(Isolate* i_client_isolate) {
    ParkedScope parked_scope(i_client_isolate->main_thread_local_isolate());
    SignalReadyAndWait(i_client_isolate);
  }
};

// Test scenario #5: The thread parks, then unparks, parks again by unrolling
// the stack and waits.
class TestParkUnparkUnrollWaitThread final
    : public TestStackContextWithSharedHeapThread {
 public:
  TestParkUnparkUnrollWaitThread(StackState stack_state,
                                 v8::base::Semaphore* sema_ready,
                                 v8::base::Semaphore* sema_gc_complete)
      : TestStackContextWithSharedHeapThread("TestParkUnparkUnrollWaitThread",
                                             stack_state, sema_ready,
                                             sema_gc_complete) {}

  struct AllocationInfo {
    Persistent<v8::FixedArray>* weak;
    volatile Address* ptr = nullptr;
  };

  void RunTheTest(Isolate* i_client_isolate) override {
    Persistent<v8::FixedArray> weak, weak0, weak1, weak2;
    volatile Address ptr_on_stack =
        AllocateObjectAndGarbage(i_client_isolate, weak);

    ParkedScope parked_scope(i_client_isolate->main_thread_local_isolate());

    // Call KeepRunning, which is not inlined, to roll and then unroll the
    // stack.
    std::vector<AllocationInfo> info = {{&weak0}, {&weak1}, {&weak2}};
    KeepRunning(i_client_isolate, info, 0);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr_on_stack);

    VerifyObject(weak);

    // The object referenced by weak0 must be live with CSS, as it there was a
    // pointer to it above the stack top.
    DCHECK_LT(kPointerDepth0, kUnrollDepth);
    VerifyObject(weak0);

    // The object referenced by weak1 may or may not be reclaimed with CSS, as
    // there was a pointer to it above the last saved stacked context but below
    // the stack top. It should always be reclaimed without CSS.
    DCHECK_LT(kUnrollDepth, kPointerDepth1);
    DCHECK_LT(kPointerDepth1, kUnparkDepth);
    if (IsPreciseGC()) VerifyObject(weak1);

    // The object referenced by weak2 must be always reclaimed (modulo false
    // positives for CSS), as the pointer to it was below the last saved stack
    // context.
    DCHECK_LT(kUnparkDepth, kPointerDepth2);
    EXPECT_TRUE(weak2.IsEmpty());
  }

  static constexpr int kPointerDepth0 = 17;
  static constexpr int kUnrollDepth = 42;
  static constexpr int kPointerDepth1 = 57;
  static constexpr int kUnparkDepth = 71;
  static constexpr int kPointerDepth2 = 87;
  static constexpr int kAllocationDepth = 100;

  V8_NOINLINE void KeepRunning(Isolate* i_client_isolate,
                               std::vector<AllocationInfo>& info, int depth) {
    // At three different recursion depths, store pointers to objects that will
    // be allocated later.
    if (depth == kPointerDepth0) {
      volatile Address ptr_on_stack;
      info[0].ptr = &ptr_on_stack;
      KeepRunning(i_client_isolate, info, depth + 1);
      // Make sure to keep the pointer alive.
      EXPECT_NE(static_cast<uint32_t>(0), ptr_on_stack);
      return;
    }
    if (depth == kPointerDepth1) {
      volatile Address ptr_on_stack;
      info[1].ptr = &ptr_on_stack;
      KeepRunning(i_client_isolate, info, depth + 1);
      // Make sure to keep the pointer alive.
      EXPECT_NE(static_cast<uint32_t>(0), ptr_on_stack);
      return;
    }
    if (depth == kPointerDepth2) {
      volatile Address ptr_on_stack;
      info[2].ptr = &ptr_on_stack;
      KeepRunning(i_client_isolate, info, depth + 1);
      // Make sure to keep the pointer alive.
      EXPECT_NE(static_cast<uint32_t>(0), ptr_on_stack);
      return;
    }
    // At this depth, wait for GC when unrolling the stack.
    if (depth == kUnrollDepth) {
      KeepRunning(i_client_isolate, info, depth + 1);
      SignalReadyAndWait(i_client_isolate);
      return;
    }
    // At this depth, unpark when rolling and park again when unrolling.
    if (depth == kUnparkDepth) {
      UnparkedScope unparked_scope(
          i_client_isolate->main_thread_local_isolate());
      KeepRunning(i_client_isolate, info, depth + 1);
      return;
    }
    // Keep recursing until the end is reached.
    if (depth < kAllocationDepth) {
      KeepRunning(i_client_isolate, info, depth + 1);
      return;
    }
    // The end of the recursion: allocate objects and store pointers at
    // various recursion depths.
    for (auto i : info)
      *i.ptr = AllocateObjectAndGarbage(i_client_isolate, *i.weak);
  }
};

}  // namespace

#define TEST_SCENARIO(name)                                \
  TEST_F(ConservativeStackScanningSharedTest,              \
         StackContextWith##name##Precise) {                \
    StackContextWithSharedHeapTest<Test##name##Thread>(    \
        i_isolate(), StackState::kNoHeapPointers);         \
  }                                                        \
  TEST_F(ConservativeStackScanningSharedTest,              \
         StackContextWith##name##Conservative) {           \
    StackContextWithSharedHeapTest<Test##name##Thread>(    \
        i_isolate(), StackState::kMayContainHeapPointers); \
  }

TEST_SCENARIO(Wait)
TEST_SCENARIO(ParkWait)
TEST_SCENARIO(ParkUnparkWait)
TEST_SCENARIO(ParkUnparkParkWait)
TEST_SCENARIO(ParkUnparkUnrollWait)

#undef TEST_SCENARIO

#endif  // V8_CAN_CREATE_SHARED_HEAP_BOOL

}  // namespace internal
}  // namespace v8
