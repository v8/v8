// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_WORKSTEALING_BAG_
#define V8_HEAP_WORKSTEALING_BAG_

#include <cstddef>
#include <vector>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

class HeapObject;

// A concurrent work stealing bag based on segments. Each tasks gets private
// push and pop segments. Empty pop segments are swapped with their
// corresponding push segments. Full push segments are published to a global
// pool of segments and replaced with empty segments.
//
// Work stealing is best effort, i.e., there is no way to inform other tasks
// of the need of items.
class WorkStealingBag {
 public:
  static const int kMaxNumTasks = 8;
  static const int kSegmentCapacity = 64;

  WorkStealingBag() {
    for (int i = 0; i < kMaxNumTasks; i++) {
      private_push_segment_[i] = new Segment();
      private_pop_segment_[i] = new Segment();
    }
  }

  ~WorkStealingBag() {
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
      base::LockGuard<base::Mutex> guard(&lock_);
      global_pool_.push_back(private_push_segment_[task_id]);
      private_push_segment_[task_id] = new Segment();
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
      } else {
        base::LockGuard<base::Mutex> guard(&lock_);
        if (global_pool_.empty()) return false;
        delete private_pop_segment_[task_id];
        private_pop_segment_[task_id] = global_pool_.back();
        global_pool_.pop_back();
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

  bool IsGlobalEmpty() {
    for (int i = 0; i < kMaxNumTasks; i++) {
      if (!IsLocalEmpty(i)) return false;
    }
    return global_pool_.empty();
  }

 private:
  FRIEND_TEST(WorkStealingBag, SegmentCreate);
  FRIEND_TEST(WorkStealingBag, SegmentPush);
  FRIEND_TEST(WorkStealingBag, SegmentPushPop);
  FRIEND_TEST(WorkStealingBag, SegmentIsEmpty);
  FRIEND_TEST(WorkStealingBag, SegmentIsFull);
  FRIEND_TEST(WorkStealingBag, SegmentClear);
  FRIEND_TEST(WorkStealingBag, SegmentFullPushFails);
  FRIEND_TEST(WorkStealingBag, SegmentEmptyPopFails);

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

   private:
    size_t index_;
    HeapObject* objects_[kCapacity];
  };

  base::Mutex lock_;
  Segment* private_pop_segment_[kMaxNumTasks];
  Segment* private_push_segment_[kMaxNumTasks];
  std::vector<Segment*> global_pool_;
};

class LocalWorkStealingBag {
 public:
  LocalWorkStealingBag(WorkStealingBag* bag, int task_id)
      : bag_(bag), task_id_(task_id) {}

  // Pushes an object onto the bag.
  bool Push(HeapObject* object) { return bag_->Push(task_id_, object); }

  // Pops an object from the bag.
  bool Pop(HeapObject** object) { return bag_->Pop(task_id_, object); }

  // Returns true if the local portion of the bag is empty.
  bool IsLocalEmpty() { return bag_->IsLocalEmpty(task_id_); }

  // Returns true if the bag is empty. Can only be used from the main thread
  // without concurrent access.
  bool IsGlobalEmpty() { return bag_->IsGlobalEmpty(); }

 private:
  WorkStealingBag* bag_;
  int task_id_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_WORKSTEALING_BAG_
