// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SCAVENGER_H_
#define V8_HEAP_SCAVENGER_H_

#include <atomic>
#include <memory>

#include "src/heap/base/worklist.h"
#include "src/heap/ephemeron-remembered-set.h"

namespace v8::internal {

using SurvivingNewLargeObjectsMap =
    std::unordered_map<Tagged<HeapObject>, Tagged<Map>, Object::Hasher>;
static constexpr int kWeakObjectListSegmentSize = 64;
using JSWeakRefsList =
    ::heap::base::Worklist<Tagged<JSWeakRef>, kWeakObjectListSegmentSize>;
using WeakCellsList =
    ::heap::base::Worklist<Tagged<WeakCell>, kWeakObjectListSegmentSize>;

// A semi-space copying garbage collector.
class ScavengerCollector {
 public:
  // The maximum number of scavenger tasks including the main thread. The actual
  // number of tasks is determined at runtime.
  static constexpr int kMaxScavengerTasks = 8;

  explicit ScavengerCollector(Heap* heap);
  ~ScavengerCollector();

  // Performs synchronous parallel garbage collection based on semi-space
  // copying algorithm.
  void CollectGarbage();

  // Pages may be left in a quarantined state after garbage collection. Objects
  // on those pages are not actually moving and as such the page has to be
  // swept which generally happens concurrently. The call here finishes
  // sweeping, possibly synchronously sweeping such pages as well.
  void CompleteSweepingQuarantinedPagesIfNeeded();

 private:
  class JobTask;
  class QuarantinedPageSweeper;

  void MergeSurvivingNewLargeObjects(
      const SurvivingNewLargeObjectsMap& objects);

  int NumberOfScavengeTasks();

  void ProcessWeakReferences(
      EphemeronRememberedSet::TableList* ephemeron_table_list);
  void ClearYoungEphemerons(
      EphemeronRememberedSet::TableList* ephemeron_table_list);
  void ClearOldEphemerons();

  void ProcessWeakObjects(JSWeakRefsList&, WeakCellsList&);
  void ProcessJSWeakRefs(JSWeakRefsList&);
  void ProcessWeakCells(WeakCellsList&);

  void HandleSurvivingNewLargeObjects();

  void SweepArrayBufferExtensions();

  size_t FetchAndResetConcurrencyEstimate() {
    const size_t estimate =
        estimate_concurrency_.exchange(0, std::memory_order_relaxed);
    return estimate == 0 ? 1 : estimate;
  }

  Isolate* const isolate_;
  Heap* const heap_;
  SurvivingNewLargeObjectsMap surviving_new_large_objects_;
  std::atomic<size_t> estimate_concurrency_{0};
  std::unique_ptr<QuarantinedPageSweeper> quarantined_page_sweeper_;

  friend class Scavenger;
};

}  // namespace v8::internal

#endif  // V8_HEAP_SCAVENGER_H_
