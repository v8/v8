// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/array-buffer-tracker-inl.h"
#include "src/heap/array-buffer-tracker.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

namespace {

typedef i::LocalArrayBufferTracker LocalTracker;

void VerifyTrackedInNewSpace(i::JSArrayBuffer* buf) {
  CHECK(i::Page::FromAddress(buf->address())->InNewSpace());
  CHECK(i::Page::FromAddress(buf->address())
            ->local_tracker<i::Page::kCreateIfNotPresent>()
            ->IsTracked(buf));
}

void VerifyTrackedInOldSpace(i::JSArrayBuffer* buf) {
  CHECK(!i::Page::FromAddress(buf->address())->InNewSpace());
  CHECK(i::Page::FromAddress(buf->address())
            ->local_tracker<i::Page::kCreateIfNotPresent>()
            ->IsTracked(buf));
}

void VerifyUntracked(i::JSArrayBuffer* buf) {
  CHECK(!i::Page::FromAddress(buf->address())
             ->local_tracker<i::Page::kCreateIfNotPresent>()
             ->IsTracked(buf));
}

}  // namespace

namespace v8 {
namespace internal {

// The following tests make sure that JSArrayBuffer tracking works expected when
// moving the objects through various spaces during GC phases.

TEST(ArrayBuffer_OnlyMC) {
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();

  JSArrayBuffer* raw_ab = nullptr;
  {
    v8::HandleScope handle_scope(isolate);
    Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, 100);
    Handle<JSArrayBuffer> buf = v8::Utils::OpenHandle(*ab);
    VerifyTrackedInNewSpace(*buf);
    heap::GcAndSweep(heap, OLD_SPACE);
    VerifyTrackedInNewSpace(*buf);
    heap::GcAndSweep(heap, OLD_SPACE);
    VerifyTrackedInOldSpace(*buf);
    raw_ab = *buf;
    // Prohibit page from being released.
    Page::FromAddress(buf->address())->MarkNeverEvacuate();
  }
  // 2 GCs are needed because we promote to old space as live, meaning that
  // we will survive one GC.
  heap::GcAndSweep(heap, OLD_SPACE);
  heap::GcAndSweep(heap, OLD_SPACE);
  VerifyUntracked(raw_ab);
}

TEST(ArrayBuffer_OnlyScavenge) {
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();

  JSArrayBuffer* raw_ab = nullptr;
  {
    v8::HandleScope handle_scope(isolate);
    Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, 100);
    Handle<JSArrayBuffer> buf = v8::Utils::OpenHandle(*ab);
    VerifyTrackedInNewSpace(*buf);
    heap::GcAndSweep(heap, NEW_SPACE);
    VerifyTrackedInNewSpace(*buf);
    heap::GcAndSweep(heap, NEW_SPACE);
    VerifyTrackedInOldSpace(*buf);
    heap::GcAndSweep(heap, NEW_SPACE);
    VerifyTrackedInOldSpace(*buf);
    raw_ab = *buf;
    // Prohibit page from being released.
    Page::FromAddress(buf->address())->MarkNeverEvacuate();
  }
  // 2 GCs are needed because we promote to old space as live, meaning that
  // we will survive one GC.
  heap::GcAndSweep(heap, OLD_SPACE);
  heap::GcAndSweep(heap, OLD_SPACE);
  VerifyUntracked(raw_ab);
}

TEST(ArrayBuffer_ScavengeAndMC) {
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();

  JSArrayBuffer* raw_ab = nullptr;
  {
    v8::HandleScope handle_scope(isolate);
    Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, 100);
    Handle<JSArrayBuffer> buf = v8::Utils::OpenHandle(*ab);
    VerifyTrackedInNewSpace(*buf);
    heap::GcAndSweep(heap, NEW_SPACE);
    VerifyTrackedInNewSpace(*buf);
    heap::GcAndSweep(heap, NEW_SPACE);
    VerifyTrackedInOldSpace(*buf);
    heap::GcAndSweep(heap, OLD_SPACE);
    VerifyTrackedInOldSpace(*buf);
    heap::GcAndSweep(heap, NEW_SPACE);
    VerifyTrackedInOldSpace(*buf);
    raw_ab = *buf;
    // Prohibit page from being released.
    Page::FromAddress(buf->address())->MarkNeverEvacuate();
  }
  // 2 GCs are needed because we promote to old space as live, meaning that
  // we will survive one GC.
  heap::GcAndSweep(heap, OLD_SPACE);
  heap::GcAndSweep(heap, OLD_SPACE);
  VerifyUntracked(raw_ab);
}

TEST(ArrayBuffer_Compaction) {
  FLAG_manual_evacuation_candidates_selection = true;
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  heap::AbandonCurrentlyFreeMemory(heap->old_space());

  v8::HandleScope handle_scope(isolate);
  Local<v8::ArrayBuffer> ab1 = v8::ArrayBuffer::New(isolate, 100);
  Handle<JSArrayBuffer> buf1 = v8::Utils::OpenHandle(*ab1);
  VerifyTrackedInNewSpace(*buf1);
  heap::GcAndSweep(heap, NEW_SPACE);
  heap::GcAndSweep(heap, NEW_SPACE);

  Page* page_before_gc = Page::FromAddress(buf1->address());
  page_before_gc->SetFlag(MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);
  VerifyTrackedInOldSpace(*buf1);

  heap->CollectAllGarbage();

  Page* page_after_gc = Page::FromAddress(buf1->address());
  VerifyTrackedInOldSpace(*buf1);

  CHECK_NE(page_before_gc, page_after_gc);
}

TEST(ArrayBuffer_UnregisterDuringSweep) {
// Regular pages in old space (without compaction) are processed concurrently
// in the sweeper. If we happen to unregister a buffer (either explicitly, or
// implicitly through e.g. |Externalize|) we need to sync with the sweeper
// task.
//
// Note: This test will will only fail on TSAN configurations.

// Disable verify-heap since it forces sweeping to be completed in the
// epilogue of the GC.
#ifdef VERIFY_HEAP
  i::FLAG_verify_heap = false;
#endif  // VERIFY_HEAP

  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  {
    v8::HandleScope handle_scope(isolate);
    Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, 100);
    Handle<JSArrayBuffer> buf = v8::Utils::OpenHandle(*ab);

    {
      v8::HandleScope handle_scope(isolate);
      // Allocate another buffer on the same page to force processing a
      // non-empty set of buffers in the last GC.
      Local<v8::ArrayBuffer> ab2 = v8::ArrayBuffer::New(isolate, 100);
      Handle<JSArrayBuffer> buf2 = v8::Utils::OpenHandle(*ab2);
      VerifyTrackedInNewSpace(*buf);
      VerifyTrackedInNewSpace(*buf2);
      heap::GcAndSweep(heap, NEW_SPACE);
      VerifyTrackedInNewSpace(*buf);
      VerifyTrackedInNewSpace(*buf2);
      heap::GcAndSweep(heap, NEW_SPACE);
      VerifyTrackedInOldSpace(*buf);
      VerifyTrackedInOldSpace(*buf2);
    }

    heap->CollectGarbage(OLD_SPACE);
    // |Externalize| will cause the buffer to be |Unregister|ed. Without
    // barriers and proper synchronization this will trigger a data race on
    // TSAN.
    v8::ArrayBuffer::Contents contents = ab->Externalize();
    heap->isolate()->array_buffer_allocator()->Free(contents.Data(),
                                                    contents.ByteLength());
  }
}

}  // namespace internal
}  // namespace v8
