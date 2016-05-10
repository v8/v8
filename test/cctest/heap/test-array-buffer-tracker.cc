// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/array-buffer-tracker.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/utils-inl.h"

namespace {

typedef i::ArrayBufferTracker Tracker;

void VerifyTrackedInNewSpace(Tracker* tracker, i::JSArrayBuffer* buf) {
  CHECK(tracker->IsTrackedInYoungGenForTesting(buf));
  CHECK(!tracker->IsTrackedInOldGenForTesting(buf));
}

void VerifyTrackedInOldSpace(Tracker* tracker, i::JSArrayBuffer* buf) {
  CHECK(!tracker->IsTrackedInYoungGenForTesting(buf));
  CHECK(tracker->IsTrackedInOldGenForTesting(buf));
}

void VerifyUntracked(Tracker* tracker, i::JSArrayBuffer* buf) {
  CHECK(!tracker->IsTrackedInYoungGenForTesting(buf));
  CHECK(!tracker->IsTrackedInOldGenForTesting(buf));
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
  Tracker* tracker = heap->array_buffer_tracker();

  JSArrayBuffer* raw_ab = nullptr;
  {
    v8::HandleScope handle_scope(isolate);
    Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, 100);
    Handle<JSArrayBuffer> buf = v8::Utils::OpenHandle(*ab);
    VerifyTrackedInNewSpace(tracker, *buf);
    heap->CollectGarbage(OLD_SPACE);
    VerifyTrackedInNewSpace(tracker, *buf);
    heap->CollectGarbage(OLD_SPACE);
    VerifyTrackedInOldSpace(tracker, *buf);
    raw_ab = *buf;
  }
  heap->CollectGarbage(OLD_SPACE);
  VerifyUntracked(tracker, raw_ab);
}

TEST(ArrayBuffer_OnlyScavenge) {
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  Tracker* tracker = heap->array_buffer_tracker();

  JSArrayBuffer* raw_ab = nullptr;
  {
    v8::HandleScope handle_scope(isolate);
    Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, 100);
    Handle<JSArrayBuffer> buf = v8::Utils::OpenHandle(*ab);
    VerifyTrackedInNewSpace(tracker, *buf);
    heap->CollectGarbage(NEW_SPACE);
    VerifyTrackedInNewSpace(tracker, *buf);
    heap->CollectGarbage(NEW_SPACE);
    VerifyTrackedInOldSpace(tracker, *buf);
    heap->CollectGarbage(NEW_SPACE);
    VerifyTrackedInOldSpace(tracker, *buf);
    raw_ab = *buf;
  }
  heap->CollectGarbage(NEW_SPACE);
  VerifyTrackedInOldSpace(tracker, raw_ab);
}

TEST(ArrayBuffer_ScavengeAndMC) {
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  Tracker* tracker = heap->array_buffer_tracker();

  JSArrayBuffer* raw_ab = nullptr;
  {
    v8::HandleScope handle_scope(isolate);
    Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, 100);
    Handle<JSArrayBuffer> buf = v8::Utils::OpenHandle(*ab);
    VerifyTrackedInNewSpace(tracker, *buf);
    heap->CollectGarbage(NEW_SPACE);
    VerifyTrackedInNewSpace(tracker, *buf);
    heap->CollectGarbage(NEW_SPACE);
    VerifyTrackedInOldSpace(tracker, *buf);
    heap->CollectGarbage(OLD_SPACE);
    VerifyTrackedInOldSpace(tracker, *buf);
    heap->CollectGarbage(NEW_SPACE);
    VerifyTrackedInOldSpace(tracker, *buf);
    raw_ab = *buf;
  }
  heap->CollectGarbage(NEW_SPACE);
  VerifyTrackedInOldSpace(tracker, raw_ab);
  heap->CollectGarbage(OLD_SPACE);
  VerifyUntracked(tracker, raw_ab);
}

TEST(ArrayBuffer_IterateNotYetDiscoveredEntries) {
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  Tracker* tracker = heap->array_buffer_tracker();

  v8::HandleScope handle_scope(isolate);
  Local<v8::ArrayBuffer> ab1 = v8::ArrayBuffer::New(isolate, 100);
  Handle<JSArrayBuffer> buf1 = v8::Utils::OpenHandle(*ab1);
  VerifyTrackedInNewSpace(tracker, *buf1);
  heap->CollectGarbage(NEW_SPACE);
  heap->CollectGarbage(NEW_SPACE);
  VerifyTrackedInOldSpace(tracker, *buf1);

  Local<v8::ArrayBuffer> ab2 = v8::ArrayBuffer::New(isolate, 100);
  Handle<JSArrayBuffer> buf2 = v8::Utils::OpenHandle(*ab2);
  Page* interesting_page = Page::FromAddress(buf2->address());
  bool found_ab1 = false;
  bool found_ab2 = false;
  tracker->IterateNotYetDiscoveredEntries(
      Tracker::kNewSpace, reinterpret_cast<Tracker::Key>(interesting_page),
      reinterpret_cast<Tracker::Key>(interesting_page + Page::kPageSize),
      [buf1, buf2, &found_ab1, &found_ab2](Tracker::Key key) {
        if (key == buf1->address()) {
          found_ab1 = true;
        }
        if (key == buf2->address()) {
          found_ab2 = true;
        }
        return Tracker::kKeepEntry;
      });
  CHECK(!found_ab1);
  CHECK(found_ab2);
}

TEST(ArrayBuffer_Compaction) {
  FLAG_manual_evacuation_candidates_selection = true;
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  AbandonCurrentlyFreeMemory(heap->old_space());
  Tracker* tracker = heap->array_buffer_tracker();

  v8::HandleScope handle_scope(isolate);
  Local<v8::ArrayBuffer> ab1 = v8::ArrayBuffer::New(isolate, 100);
  Handle<JSArrayBuffer> buf1 = v8::Utils::OpenHandle(*ab1);
  VerifyTrackedInNewSpace(tracker, *buf1);
  heap->CollectGarbage(NEW_SPACE);
  heap->CollectGarbage(NEW_SPACE);

  Page* page_before_gc = Page::FromAddress(buf1->address());
  page_before_gc->SetFlag(MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);
  VerifyTrackedInOldSpace(tracker, *buf1);

  heap->CollectAllGarbage();

  Page* page_after_gc = Page::FromAddress(buf1->address());
  VerifyTrackedInOldSpace(tracker, *buf1);

  CHECK_NE(page_before_gc, page_after_gc);
}

}  // namespace internal
}  // namespace v8
