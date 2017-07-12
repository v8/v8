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

// A concurrent worklist based on segments. Each tasks gets private
// push and pop segments. Empty pop segments are swapped with their
// corresponding push segments. Full push segments are published to a global
// pool of segments and replaced with empty segments.
//
// Work stealing is best effort, i.e., there is no way to inform other tasks
// of the need of items.
template <typename EntryType, int SEGMENT_SIZE>
class Worklist {
 public:
  class View {
   public:
    View(Worklist<EntryType, SEGMENT_SIZE>* worklist, int task_id)
        : worklist_(worklist), task_id_(task_id) {}

    // Pushes an entry onto the worklist.
    bool Push(EntryType entry) { return worklist_->Push(task_id_, entry); }

    // Pops an entry from the worklist.
    bool Pop(EntryType* entry) { return worklist_->Pop(task_id_, entry); }

    // Returns true if the local portion of the worklist is empty.
    bool IsLocalEmpty() { return worklist_->IsLocalEmpty(task_id_); }

    // Returns true if the worklist is empty. Can only be used from the main
    // thread without concurrent access.
    bool IsGlobalEmpty() { return worklist_->IsGlobalEmpty(); }

    size_t LocalPushSegmentSize() {
      return worklist_->LocalPushSegmentSize(task_id_);
    }

   private:
    Worklist<EntryType, SEGMENT_SIZE>* worklist_;
    int task_id_;
  };

  static const int kMaxNumTasks = 8;
  static const int kSegmentCapacity = SEGMENT_SIZE;

  Worklist() : Worklist(kMaxNumTasks) {}

  explicit Worklist(int num_tasks) : num_tasks_(num_tasks) {
    for (int i = 0; i < num_tasks_; i++) {
      private_push_segment(i) = new Segment();
      private_pop_segment(i) = new Segment();
    }
  }

  ~Worklist() {
    CHECK(IsGlobalEmpty());
    for (int i = 0; i < num_tasks_; i++) {
      DCHECK_NOT_NULL(private_push_segment(i));
      DCHECK_NOT_NULL(private_pop_segment(i));
      delete private_push_segment(i);
      delete private_pop_segment(i);
    }
  }

  bool Push(int task_id, EntryType entry) {
    DCHECK_LT(task_id, num_tasks_);
    DCHECK_NOT_NULL(private_push_segment(task_id));
    if (!private_push_segment(task_id)->Push(entry)) {
      PublishPushSegmentToGlobal(task_id);
      bool success = private_push_segment(task_id)->Push(entry);
      USE(success);
      DCHECK(success);
    }
    return true;
  }

  bool Pop(int task_id, EntryType* entry) {
    DCHECK_LT(task_id, num_tasks_);
    DCHECK_NOT_NULL(private_pop_segment(task_id));
    if (!private_pop_segment(task_id)->Pop(entry)) {
      if (!private_push_segment(task_id)->IsEmpty()) {
        Segment* tmp = private_pop_segment(task_id);
        private_pop_segment(task_id) = private_push_segment(task_id);
        private_push_segment(task_id) = tmp;
      } else if (!StealPopSegmentFromGlobal(task_id)) {
        return false;
      }
      bool success = private_pop_segment(task_id)->Pop(entry);
      USE(success);
      DCHECK(success);
    }
    return true;
  }

  size_t LocalPushSegmentSize(int task_id) {
    return private_push_segment(task_id)->Size();
  }

  bool IsLocalEmpty(int task_id) {
    return private_pop_segment(task_id)->IsEmpty() &&
           private_push_segment(task_id)->IsEmpty();
  }

  bool IsGlobalPoolEmpty() {
    base::LockGuard<base::Mutex> guard(&lock_);
    return global_pool_.empty();
  }

  bool IsGlobalEmpty() {
    for (int i = 0; i < num_tasks_; i++) {
      if (!IsLocalEmpty(i)) return false;
    }
    return global_pool_.empty();
  }

  size_t LocalSize(int task_id) {
    return private_pop_segment(task_id)->Size() +
           private_push_segment(task_id)->Size();
  }

  // Clears all segments. Frees the global segment pool.
  // This function assumes that other tasks are not running.
  void Clear() {
    for (int i = 0; i < num_tasks_; i++) {
      private_pop_segment(i)->Clear();
      private_push_segment(i)->Clear();
    }
    for (Segment* segment : global_pool_) {
      delete segment;
    }
    global_pool_.clear();
  }

  // Calls the specified callback on each element of the deques and replaces
  // the element with the result of the callback.
  // The signature of the callback is
  //   bool Callback(EntryType old, EntryType* new).
  // If the callback returns |false| then the element is removed from the
  // worklist. Otherwise the |new| entry is updated.
  // This function assumes that other tasks are not running.
  template <typename Callback>
  void Update(Callback callback) {
    for (int i = 0; i < num_tasks_; i++) {
      private_pop_segment(i)->Update(callback);
      private_push_segment(i)->Update(callback);
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

  template <typename Callback>
  void IterateGlobalPool(Callback callback) {
    base::LockGuard<base::Mutex> guard(&lock_);
    for (size_t i = 0; i < global_pool_.size(); i++) {
      global_pool_[i]->Iterate(callback);
    }
  }

  void FlushToGlobal(int task_id) {
    PublishPushSegmentToGlobal(task_id);
    PublishPopSegmentToGlobal(task_id);
  }

 private:
  FRIEND_TEST(WorkListTest, SegmentCreate);
  FRIEND_TEST(WorkListTest, SegmentPush);
  FRIEND_TEST(WorkListTest, SegmentPushPop);
  FRIEND_TEST(WorkListTest, SegmentIsEmpty);
  FRIEND_TEST(WorkListTest, SegmentIsFull);
  FRIEND_TEST(WorkListTest, SegmentClear);
  FRIEND_TEST(WorkListTest, SegmentFullPushFails);
  FRIEND_TEST(WorkListTest, SegmentEmptyPopFails);
  FRIEND_TEST(WorkListTest, SegmentUpdateFalse);
  FRIEND_TEST(WorkListTest, SegmentUpdate);

  class Segment {
   public:
    static const int kCapacity = kSegmentCapacity;

    Segment() : index_(0) {}

    bool Push(EntryType entry) {
      if (IsFull()) return false;
      entries_[index_++] = entry;
      return true;
    }

    bool Pop(EntryType* entry) {
      if (IsEmpty()) return false;
      *entry = entries_[--index_];
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
        if (callback(entries_[i], &entries_[new_index])) {
          new_index++;
        }
      }
      index_ = new_index;
    }

    template <typename Callback>
    void Iterate(Callback callback) {
      for (size_t i = 0; i < index_; i++) {
        callback(entries_[i]);
      }
    }

   private:
    size_t index_;
    EntryType entries_[kCapacity];
  };

  struct PrivateSegmentHolder {
    Segment* private_push_segment;
    Segment* private_pop_segment;
    char cache_line_padding[64];
  };

  V8_INLINE Segment*& private_push_segment(int task_id) {
    return private_segments_[task_id].private_push_segment;
  }

  V8_INLINE Segment*& private_pop_segment(int task_id) {
    return private_segments_[task_id].private_pop_segment;
  }

  // Do not inline the following functions as this would mean that vector fast
  // paths are inlined into all callers. This is mainly an issue when used
  // within visitors that have lots of dispatches.

  V8_NOINLINE void PublishPushSegmentToGlobal(int task_id) {
    base::LockGuard<base::Mutex> guard(&lock_);
    if (!private_push_segment(task_id)->IsEmpty()) {
      global_pool_.push_back(private_push_segment(task_id));
      private_push_segment(task_id) = new Segment();
    }
  }

  V8_NOINLINE void PublishPopSegmentToGlobal(int task_id) {
    base::LockGuard<base::Mutex> guard(&lock_);
    if (!private_pop_segment(task_id)->IsEmpty()) {
      global_pool_.push_back(private_pop_segment(task_id));
      private_pop_segment(task_id) = new Segment();
    }
  }

  V8_NOINLINE bool StealPopSegmentFromGlobal(int task_id) {
    base::LockGuard<base::Mutex> guard(&lock_);
    if (global_pool_.empty()) return false;
    delete private_pop_segment(task_id);
    private_pop_segment(task_id) = global_pool_.back();
    global_pool_.pop_back();
    return true;
  }

  PrivateSegmentHolder private_segments_[kMaxNumTasks];
  base::Mutex lock_;
  std::vector<Segment*> global_pool_;
  int num_tasks_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_WORKSTEALING_BAG_
