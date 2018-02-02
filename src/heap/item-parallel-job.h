// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_ITEM_PARALLEL_JOB_H_
#define V8_HEAP_ITEM_PARALLEL_JOB_H_

#include <vector>

#include "src/base/macros.h"
#include "src/base/platform/semaphore.h"
#include "src/cancelable-task.h"
#include "src/utils.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

class Isolate;

// This class manages background tasks that process a set of items in parallel.
// The first task added is executed on the same thread as |job.Run()| is called.
// All other tasks are scheduled in the background.
//
// - Items need to inherit from ItemParallelJob::Item.
// - Tasks need to inherit from ItemParallelJob::Task.
//
// Items need to be marked as finished after processing them. Task and Item
// ownership is transferred to the job.
class ItemParallelJob {
 public:
  class Task;

  class Item {
   public:
    Item() : state_(kAvailable) {}
    virtual ~Item() {}

    // Marks an item as being finished.
    void MarkFinished() { CHECK(state_.TrySetValue(kProcessing, kFinished)); }

   private:
    enum ProcessingState { kAvailable, kProcessing, kFinished };

    bool TryMarkingAsProcessing() {
      return state_.TrySetValue(kAvailable, kProcessing);
    }
    bool IsFinished() { return state_.Value() == kFinished; }

    base::AtomicValue<ProcessingState> state_;

    friend class ItemParallelJob;
    friend class ItemParallelJob::Task;

    DISALLOW_COPY_AND_ASSIGN(Item);
  };

  class Task : public CancelableTask {
   public:
    explicit Task(Isolate* isolate)
        : CancelableTask(isolate),
          items_(nullptr),
          cur_index_(0),
          items_considered_(0),
          on_finish_(nullptr) {}
    virtual ~Task() {}

    virtual void RunInParallel() = 0;

   protected:
    // Retrieves a new item that needs to be processed. Returns |nullptr| if
    // all items are processed. Upon returning an item, the task is required
    // to process the item and mark the item as finished after doing so.
    template <class ItemType>
    ItemType* GetItem() {
      while (items_considered_++ != items_->size()) {
        // Wrap around.
        if (cur_index_ == items_->size()) {
          cur_index_ = 0;
        }
        Item* item = (*items_)[cur_index_++];
        if (item->TryMarkingAsProcessing()) {
          return static_cast<ItemType*>(item);
        }
      }
      return nullptr;
    }

   private:
    // Sets up state required before invoking Run(). If
    // |start_index is >= items_.size()|, this task will not process work items
    // (some jobs have more tasks than work items in order to parallelize post-
    // processing, e.g. scavenging).
    void SetupInternal(base::Semaphore* on_finish, std::vector<Item*>* items,
                       size_t start_index) {
      on_finish_ = on_finish;
      items_ = items;
      if (start_index < items->size())
        cur_index_ = start_index;
      else
        items_considered_ = items_->size();
    }

    // We don't allow overriding this method any further.
    void RunInternal() final {
      RunInParallel();
      on_finish_->Signal();
    }

    std::vector<Item*>* items_;
    size_t cur_index_;
    size_t items_considered_;
    base::Semaphore* on_finish_;

    friend class ItemParallelJob;
    friend class Item;

    DISALLOW_COPY_AND_ASSIGN(Task);
  };

  ItemParallelJob(CancelableTaskManager* cancelable_task_manager,
                  base::Semaphore* pending_tasks)
      : cancelable_task_manager_(cancelable_task_manager),
        pending_tasks_(pending_tasks) {}

  ~ItemParallelJob() {
    for (size_t i = 0; i < items_.size(); i++) {
      Item* item = items_[i];
      CHECK(item->IsFinished());
      delete item;
    }
  }

  // Adds a task to the job. Transfers ownership to the job.
  void AddTask(Task* task) { tasks_.push_back(task); }

  // Adds an item to the job. Transfers ownership to the job.
  void AddItem(Item* item) { items_.push_back(item); }

  int NumberOfItems() const { return static_cast<int>(items_.size()); }
  int NumberOfTasks() const { return static_cast<int>(tasks_.size()); }

  void Run() {
    DCHECK_GT(tasks_.size(), 0);
    const size_t num_items = items_.size();
    const size_t num_tasks = tasks_.size();

    // Some jobs have more tasks than items (when the items are mere coarse
    // grain tasks that generate work dynamically for a second phase which all
    // tasks participate in). Some jobs even have 0 items to preprocess but
    // still have multiple tasks.
    // TODO(gab): Figure out a cleaner scheme for this.
    const size_t num_tasks_processing_items = Min(num_items, tasks_.size());

    // In the event of an uneven workload, distribute an extra item to the first
    // |items_remainder| tasks.
    const size_t items_remainder = num_tasks_processing_items > 0
                                       ? num_items % num_tasks_processing_items
                                       : 0;
    // Base |items_per_task|, will be bumped by 1 for the first
    // |items_remainder| tasks.
    const size_t items_per_task = num_tasks_processing_items > 0
                                      ? num_items / num_tasks_processing_items
                                      : 0;
    CancelableTaskManager::Id* task_ids =
        new CancelableTaskManager::Id[num_tasks];
    Task* main_task = nullptr;
    for (size_t i = 0, start_index = 0; i < num_tasks;
         i++, start_index += items_per_task + (i < items_remainder ? 1 : 0)) {
      Task* task = tasks_[i];

      // By definition there are less |items_remainder| to distribute then
      // there are tasks processing items so this cannot overflow while we are
      // assigning work items.
      DCHECK_IMPLIES(start_index >= num_items, i >= num_tasks_processing_items);

      task->SetupInternal(pending_tasks_, &items_, start_index);
      task_ids[i] = task->id();
      if (i > 0) {
        V8::GetCurrentPlatform()->CallOnBackgroundThread(
            task, v8::Platform::kShortRunningTask);
      } else {
        main_task = task;
      }
    }

    // Contribute on main thread.
    main_task->Run();
    delete main_task;

    // Wait for background tasks.
    for (size_t i = 0; i < num_tasks; i++) {
      if (cancelable_task_manager_->TryAbort(task_ids[i]) !=
          CancelableTaskManager::kTaskAborted) {
        pending_tasks_->Wait();
      }
    }
    delete[] task_ids;
  }

 private:
  std::vector<Item*> items_;
  std::vector<Task*> tasks_;
  CancelableTaskManager* cancelable_task_manager_;
  base::Semaphore* pending_tasks_;
  DISALLOW_COPY_AND_ASSIGN(ItemParallelJob);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ITEM_PARALLEL_JOB_H_
