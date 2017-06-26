// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_WORKLIST_
#define V8_HEAP_WORKLIST_

#include <cstddef>
#include <vector>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

class HeapObject;

// A concurrent worklist based on segments. Each tasks gets private
// push and pop segments. Empty pop segments are swapped with their
// corresponding push segments. Full push segments are published to a global
// pool of segments and replaced with empty segments.
//
// Work stealing is best effort, i.e., there is no way to inform other tasks
// of the need of items.
class Worklist {
 public:
  static const int kMaxNumTasks = 8;
  static const int kSegmentCapacity = 64;

  Worklist() {
    for (int i = 0; i < kMaxNumTasks; i++) {
      private_push_segment_[i] = new Segment();
      private_pop_segment_[i] = new Segment();
    }
  }

  ~Worklist() {
    CHECK(IsGlobalEmpty());
    for (int i = 0; i < kMaxNumTasks; i++) {
      DCHECK_NOT_NULL(private_push_segment_[i]);
      DCHECK_NOT_NULL(private_pop_segment_[i]);
      delete private_push_segment_[i];
      delete private_pop_segment_[i];
    }
  }

  bool Push(int task_id, HeapObject* object) {
    DCHECK_LT(task_id, kMaxNumTasks);
    DCHECK_NOT_NULL(private_push_segment_[task_id]);
    if (!private_push_segment_[task_id]->Push(object)) {
      PublishPushSegmentToGlobal(task_id);
      bool success = private_push_segment_[task_id]->Push(object);
      USE(success);
      DCHECK(success);
    }
    return true;
  }

  bool Pop(int task_id, HeapObject** object) {
    DCHECK_LT(task_id, kMaxNumTasks);
    DCHECK_NOT_NULL(private_pop_segment_[task_id]);
    if (!private_pop_segment_[task_id]->Pop(object)) {
      if (!private_push_segment_[task_id]->IsEmpty()) {
        Segment* tmp = private_pop_segment_[task_id];
        private_pop_segment_[task_id] = private_push_segment_[task_id];
        private_push_segment_[task_id] = tmp;
      } else if (!StealPopSegmentFromGlobal(task_id)) {
        return false;
      }
      bool success = private_pop_segment_[task_id]->Pop(object);
      USE(success);
      DCHECK(success);
    }
    return true;
  }

  bool IsLocalEmpty(int task_id) {
    return private_pop_segment_[task_id]->IsEmpty() &&
           private_push_segment_[task_id]->IsEmpty();
  }

  bool IsGlobalPoolEmpty() {
    base::LockGuard<base::Mutex> guard(&lock_);
    return global_pool_.empty();
  }

  bool IsGlobalEmpty() {
    for (int i = 0; i < kMaxNumTasks; i++) {
      if (!IsLocalEmpty(i)) return false;
    }
    return global_pool_.empty();
  }

  size_t LocalSize(int task_id) {
    return private_pop_segment_[task_id]->Size() +
           private_push_segment_[task_id]->Size();
  }

  // Clears all segments. Frees the global segment pool.
  // This function assumes that other tasks are not running.
  void Clear() {
    for (int i = 0; i < kMaxNumTasks; i++) {
      private_pop_segment_[i]->Clear();
      private_push_segment_[i]->Clear();
    }
    for (Segment* segment : global_pool_) {
      delete segment;
    }
    global_pool_.clear();
  }

  // Calls the specified callback on each element of the deques and replaces
  // the element with the result of the callback. If the callback returns
  // nullptr then the element is removed from the worklist.
  // The callback must accept HeapObject* and return HeapObject*.
  // This function assumes that other tasks are not running.
  template <typename Callback>
  void Update(Callback callback) {
    for (int i = 0; i < kMaxNumTasks; i++) {
      private_pop_segment_[i]->Update(callback);
      private_push_segment_[i]->Update(callback);
    }
    for (size_t i = 0; i < global_pool_.size(); i++) {
      Segment* segment = global_pool_[i];
      segment->Update(callback);
      if (segment->IsEmpty()) {
        global_pool_[i] = global_pool_.back();
        global_pool_.pop_back();
        delete segment;
        --i;
      }
    }
  }

  void FlushToGlobal(int task_id) {
    PublishPushSegmentToGlobal(task_id);
    PublishPopSegmentToGlobal(task_id);
  }

 private:
  FRIEND_TEST(Worklist, SegmentCreate);
  FRIEND_TEST(Worklist, SegmentPush);
  FRIEND_TEST(Worklist, SegmentPushPop);
  FRIEND_TEST(Worklist, SegmentIsEmpty);
  FRIEND_TEST(Worklist, SegmentIsFull);
  FRIEND_TEST(Worklist, SegmentClear);
  FRIEND_TEST(Worklist, SegmentFullPushFails);
  FRIEND_TEST(Worklist, SegmentEmptyPopFails);
  FRIEND_TEST(Worklist, SegmentUpdateNull);
  FRIEND_TEST(Worklist, SegmentUpdate);

  class Segment {
   public:
    static const int kCapacity = kSegmentCapacity;

    Segment() : index_(0) {}

    bool Push(HeapObject* object) {
      if (IsFull()) return false;
      objects_[index_++] = object;
      return true;
    }

    bool Pop(HeapObject** object) {
      if (IsEmpty()) return false;
      *object = objects_[--index_];
      return true;
    }

    size_t Size() { return index_; }
    bool IsEmpty() { return index_ == 0; }
    bool IsFull() { return index_ == kCapacity; }
    void Clear() { index_ = 0; }

    template <typename Callback>
    void Update(Callback callback) {
      size_t new_index = 0;
      for (size_t i = 0; i < index_; i++) {
        HeapObject* object = callback(objects_[i]);
        if (object) {
          objects_[new_index++] = object;
        }
      }
      index_ = new_index;
    }

   private:
    size_t index_;
    HeapObject* objects_[kCapacity];
  };

  // Do not inline the following functions as this would mean that vector fast
  // paths are inlined into all callers. This is mainly an issue when used
  // within visitors that have lots of dispatches.

  V8_NOINLINE void PublishPushSegmentToGlobal(int task_id) {
    base::LockGuard<base::Mutex> guard(&lock_);
    if (!private_push_segment_[task_id]->IsEmpty()) {
      global_pool_.push_back(private_push_segment_[task_id]);
      private_push_segment_[task_id] = new Segment();
    }
  }

  V8_NOINLINE void PublishPopSegmentToGlobal(int task_id) {
    base::LockGuard<base::Mutex> guard(&lock_);
    if (!private_pop_segment_[task_id]->IsEmpty()) {
      global_pool_.push_back(private_pop_segment_[task_id]);
      private_pop_segment_[task_id] = new Segment();
    }
  }

  V8_NOINLINE bool StealPopSegmentFromGlobal(int task_id) {
    base::LockGuard<base::Mutex> guard(&lock_);
    if (global_pool_.empty()) return false;
    delete private_pop_segment_[task_id];
    private_pop_segment_[task_id] = global_pool_.back();
    global_pool_.pop_back();
    return true;
  }

  base::Mutex lock_;
  Segment* private_pop_segment_[kMaxNumTasks];
  Segment* private_push_segment_[kMaxNumTasks];
  std::vector<Segment*> global_pool_;
};

class WorklistView {
 public:
  WorklistView(Worklist* worklist, int task_id)
      : worklist_(worklist), task_id_(task_id) {}

  // Pushes an object onto the worklist.
  bool Push(HeapObject* object) { return worklist_->Push(task_id_, object); }

  // Pops an object from the worklist.
  bool Pop(HeapObject** object) { return worklist_->Pop(task_id_, object); }

  // Returns true if the local portion of the worklist is empty.
  bool IsLocalEmpty() { return worklist_->IsLocalEmpty(task_id_); }

  // Returns true if the worklist is empty. Can only be used from the main
  // thread without concurrent access.
  bool IsGlobalEmpty() { return worklist_->IsGlobalEmpty(); }

 private:
  Worklist* worklist_;
  int task_id_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_WORKSTEALING_BAG_
