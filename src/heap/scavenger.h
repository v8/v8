// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SCAVENGER_H_
#define V8_HEAP_SCAVENGER_H_

#include <atomic>

#include "src/base/platform/condition-variable.h"
#include "src/heap/base/worklist.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/evacuation-allocator.h"
#include "src/heap/heap-visitor.h"
#include "src/heap/index-generator.h"
#include "src/heap/memory-chunk.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/parallel-work-item.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/slot-set.h"

namespace v8 {
namespace internal {

class RootScavengeVisitor;
class Scavenger;
class ScavengeVisitor;

enum class CopyAndForwardResult {
  SUCCESS_YOUNG_GENERATION,
  SUCCESS_OLD_GENERATION,
  FAILURE
};

using SurvivingNewLargeObjectsMap =
    std::unordered_map<Tagged<HeapObject>, Tagged<Map>, Object::Hasher>;
using SurvivingNewLargeObjectMapEntry =
    std::pair<Tagged<HeapObject>, Tagged<Map>>;

class ScavengerCollector;

class Scavenger {
 public:
  static constexpr int kCopiedListSegmentSize = 256;
  static constexpr int kPinnedListSegmentSize = 64;
  static constexpr int kPromotedListSegmentSize = 256;

  using CopiedList =
      ::heap::base::Worklist<Tagged<HeapObject>, kCopiedListSegmentSize>;

  using ObjectAndMap = std::pair<Tagged<HeapObject>, Tagged<Map>>;
  using PinnedList =
      ::heap::base::Worklist<ObjectAndMap, kPinnedListSegmentSize>;

  struct PromotedListEntry {
    Tagged<HeapObject> heap_object;
    Tagged<Map> map;
    SafeHeapObjectSize size;
  };
  using PromotedList =
      ::heap::base::Worklist<PromotedListEntry, kPromotedListSegmentSize>;

  using EmptyChunksList = ::heap::base::Worklist<MutablePageMetadata*, 64>;

  Scavenger(ScavengerCollector* collector, Heap* heap, bool is_logging,
            EmptyChunksList* empty_chunks, CopiedList* copied_list,
            PinnedList* pinned_list, PromotedList* promoted_list,
            EphemeronRememberedSet::TableList* ephemeron_table_list);

  // Entry point for scavenging an old generation page. For scavenging single
  // objects see RootScavengingVisitor and ScavengeVisitor below.
  void ScavengePage(MutablePageMetadata* page);

  // Processes remaining work (=objects) after single objects have been
  // manually scavenged using ScavengeObject or CheckAndScavengeObject.
  void Process(JobDelegate* delegate = nullptr);

  // Finalize the Scavenger. Needs to be called from the main thread.
  void Finalize();
  void Publish();

  void AddEphemeronHashTable(Tagged<EphemeronHashTable> table);

  // Returns true if the object is a large young object, and false otherwise.
  bool PromoteIfLargeObject(Tagged<HeapObject> object);

  void PinAndPushObject(MutablePageMetadata* metadata,
                        Tagged<HeapObject> object, MapWord map_word);
  void VisitPinnedObjects();

  size_t bytes_copied() const { return copied_size_; }
  size_t bytes_promoted() const { return promoted_size_; }

 private:
  enum PromotionHeapChoice { kPromoteIntoLocalHeap, kPromoteIntoSharedHeap };

  // Number of objects to process before interrupting for potentially waking
  // up other tasks.
  static const int kInterruptThreshold = 128;

  inline Heap* heap() { return heap_; }

  inline void SynchronizePageAccess(Tagged<MaybeObject> object) const;

  void AddPageToSweeperIfNecessary(MutablePageMetadata* page);

  // Potentially scavenges an object referenced from |slot| if it is
  // indeed a HeapObject and resides in from space.
  template <typename TSlot>
  inline SlotCallbackResult CheckAndScavengeObject(Heap* heap, TSlot slot);

  template <typename TSlot>
  inline void CheckOldToNewSlotForSharedUntyped(MemoryChunk* chunk,
                                                MutablePageMetadata* page,
                                                TSlot slot);
  inline void CheckOldToNewSlotForSharedTyped(MemoryChunk* chunk,
                                              MutablePageMetadata* page,
                                              SlotType slot_type,
                                              Address slot_address,
                                              Tagged<MaybeObject> new_target);

  // Scavenges an object |object| referenced from slot |p|. |object| is required
  // to be in from space.
  template <typename THeapObjectSlot>
  inline SlotCallbackResult ScavengeObject(THeapObjectSlot p,
                                           Tagged<HeapObject> object);

  // Copies |source| to |target| and sets the forwarding pointer in |source|.
  V8_INLINE bool MigrateObject(Tagged<Map> map, Tagged<HeapObject> source,
                               Tagged<HeapObject> target,
                               SafeHeapObjectSize size,
                               PromotionHeapChoice promotion_heap_choice);

  V8_INLINE SlotCallbackResult
  RememberedSetEntryNeeded(CopyAndForwardResult result);

  template <typename THeapObjectSlot>
  V8_INLINE CopyAndForwardResult SemiSpaceCopyObject(
      Tagged<Map> map, THeapObjectSlot slot, Tagged<HeapObject> object,
      SafeHeapObjectSize object_size, ObjectFields object_fields);

  template <typename THeapObjectSlot,
            PromotionHeapChoice promotion_heap_choice = kPromoteIntoLocalHeap>
  V8_INLINE CopyAndForwardResult PromoteObject(Tagged<Map> map,
                                               THeapObjectSlot slot,
                                               Tagged<HeapObject> object,
                                               SafeHeapObjectSize object_size,
                                               ObjectFields object_fields);

  template <typename THeapObjectSlot>
  V8_INLINE SlotCallbackResult EvacuateObject(THeapObjectSlot slot,
                                              Tagged<Map> map,
                                              Tagged<HeapObject> source);

  V8_INLINE bool HandleLargeObject(Tagged<Map> map, Tagged<HeapObject> object,
                                   SafeHeapObjectSize object_size,
                                   ObjectFields object_fields);

  // Different cases for object evacuation.
  template <typename THeapObjectSlot,
            PromotionHeapChoice promotion_heap_choice = kPromoteIntoLocalHeap>
  V8_INLINE SlotCallbackResult EvacuateObjectDefault(
      Tagged<Map> map, THeapObjectSlot slot, Tagged<HeapObject> object,
      SafeHeapObjectSize object_size, ObjectFields object_fields);

  template <typename THeapObjectSlot>
  inline SlotCallbackResult EvacuateThinString(Tagged<Map> map,
                                               THeapObjectSlot slot,
                                               Tagged<ThinString> object,
                                               SafeHeapObjectSize object_size);

  template <typename THeapObjectSlot>
  inline SlotCallbackResult EvacuateShortcutCandidate(
      Tagged<Map> map, THeapObjectSlot slot, Tagged<ConsString> object,
      SafeHeapObjectSize object_size);

  template <typename THeapObjectSlot>
  inline SlotCallbackResult EvacuateInPlaceInternalizableString(
      Tagged<Map> map, THeapObjectSlot slot, Tagged<String> string,
      SafeHeapObjectSize object_size, ObjectFields object_fields);

  void IterateAndScavengePromotedObject(Tagged<HeapObject> target,
                                        Tagged<Map> map,
                                        SafeHeapObjectSize object_size);
  void RememberPromotedEphemeron(Tagged<EphemeronHashTable> table, int index);

  V8_INLINE bool ShouldEagerlyProcessPromotedList() const;

  void PushPinnedObject(Tagged<HeapObject> object, Tagged<Map> map,
                        SafeHeapObjectSize object_size);
  void PushPinnedPromotedObject(Tagged<HeapObject> object, Tagged<Map> map,
                                SafeHeapObjectSize object_size);

  ScavengerCollector* const collector_;
  Heap* const heap_;
  EmptyChunksList::Local local_empty_chunks_;
  CopiedList::Local local_copied_list_;
  PinnedList::Local local_pinned_list_;
  PromotedList::Local local_promoted_list_;
  EphemeronRememberedSet::TableList::Local local_ephemeron_table_list_;
  PretenuringHandler::PretenuringFeedbackMap local_pretenuring_feedback_;
  EphemeronRememberedSet::TableMap local_ephemeron_remembered_set_;
  SurvivingNewLargeObjectsMap local_surviving_new_large_objects_;
  size_t copied_size_{0};
  size_t promoted_size_{0};
  EvacuationAllocator allocator_;

  const bool is_logging_;
  const bool shared_string_table_;
  const bool mark_shared_heap_;
  const bool shortcut_strings_;

  friend class IterateAndScavengePromotedObjectsVisitor;
  friend class RootScavengeVisitor;
  friend class ScavengeVisitor;
};

// Helper class for turning the scavenger into an object visitor that is also
// filtering out non-HeapObjects and objects which do not reside in new space.
class RootScavengeVisitor final : public RootVisitor {
 public:
  explicit RootScavengeVisitor(Scavenger& scavenger);
  ~RootScavengeVisitor() final;

  void VisitRootPointer(Root root, const char* description,
                        FullObjectSlot p) final;
  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) final;

 private:
  void ScavengePointer(FullObjectSlot p);

  Scavenger& scavenger_;
};

class ScavengerCollector {
 public:
  struct PinnedObjectEntry {
    Address address;
    MapWord map_word;
    SafeHeapObjectSize size;
    MutablePageMetadata* metadata;
  };
  using PinnedObjects = std::vector<PinnedObjectEntry>;

  static const int kMaxScavengerTasks = 8;
  static const int kMainThreadId = 0;

  explicit ScavengerCollector(Heap* heap);

  void CollectGarbage();

  void CompleteSweepingQuarantinedPagesIfNeeded();

 private:
  class JobTask : public v8::JobTask {
   public:
    JobTask(ScavengerCollector* collector,
            std::vector<std::unique_ptr<Scavenger>>* scavengers,
            std::vector<std::pair<ParallelWorkItem, MutablePageMetadata*>>
                old_to_new_chunks,
            const Scavenger::CopiedList& copied_list,
            const Scavenger::PinnedList& pinned_list,
            const Scavenger::PromotedList& promoted_list);

    void Run(JobDelegate* delegate) override;
    size_t GetMaxConcurrency(size_t worker_count) const override;

    uint64_t trace_id() const { return trace_id_; }

   private:
    void ProcessItems(JobDelegate* delegate, Scavenger* scavenger);
    void ConcurrentScavengePages(Scavenger* scavenger);
    void VisitPinnedObjects(Scavenger* scavenger);

    ScavengerCollector* collector_;

    std::vector<std::unique_ptr<Scavenger>>* scavengers_;
    std::vector<std::pair<ParallelWorkItem, MutablePageMetadata*>>
        old_to_new_chunks_;
    std::atomic<size_t> remaining_memory_chunks_{0};
    IndexGenerator generator_;

    const Scavenger::CopiedList& copied_list_;
    const Scavenger::PinnedList& pinned_list_;
    const Scavenger::PromotedList& promoted_list_;

    const uint64_t trace_id_;
  };

  // Quarantined pages must be swept before the next GC. If the next GC uses
  // conservative scanning and encounters a stale object left over from a
  // previous GC, this can result in memory corruptions.
  class QuarantinedPageSweeper {
   public:
    explicit QuarantinedPageSweeper(Heap* heap) : heap_(heap) {}
    ~QuarantinedPageSweeper() {
      if (IsSweeping()) {
        FinishSweeping();
      }
    }

    void StartSweeping(const PinnedObjects&& pinned_objects);
    void FinishSweeping();
    bool IsSweeping() const {
      DCHECK_IMPLIES(job_handle_, job_handle_->IsValid());
      return job_handle_.get();
    }

   private:
    class JobTask : public v8::JobTask {
     public:
      JobTask(Heap* heap, const PinnedObjects&& pinned_objects);
      ~JobTask() {
        DCHECK(is_done_.load(std::memory_order_relaxed));
        DCHECK(pinned_object_per_page_.empty());
        DCHECK(pinned_objects_.empty());
      }

      void Run(JobDelegate* delegate) override;

      size_t GetMaxConcurrency(size_t worker_count) const override {
        return is_done_.load(std::memory_order_relaxed) ? 0 : 1;
      }

      uint64_t trace_id() const { return trace_id_; }

     private:
      using ObjectsAndSizes = std::vector<std::pair<Address, size_t>>;
      using PinnedObjectPerPage =
          std::unordered_map<MemoryChunk*, ObjectsAndSizes,
                             base::hash<MemoryChunk*>>;
      using FreeSpaceHandler =
          std::function<void(Heap*, Address, size_t, bool)>;
      static void CreateFillerFreeSpaceHandler(Heap* heap, Address address,
                                               size_t size, bool should_zap);
      static void AddToFreeListFreeSpaceHandler(Heap* heap, Address address,
                                                size_t size, bool should_zap);

      size_t SweepPage(FreeSpaceHandler free_space_handler, MemoryChunk* chunk,
                       PageMetadata* page,
                       ObjectsAndSizes& pinned_objects_on_page);
      void CreateFillerFreeHandler(Address address, size_t size);

      Heap* const heap_;
      const uint64_t trace_id_;
      const bool should_zap_;
      PinnedObjects pinned_objects_;

      std::atomic_bool is_done_{false};
      PinnedObjectPerPage pinned_object_per_page_;
      PinnedObjectPerPage::iterator next_page_iterator_;
    };

    Heap* const heap_;
    std::unique_ptr<JobHandle> job_handle_;
  };

  void MergeSurvivingNewLargeObjects(
      const SurvivingNewLargeObjectsMap& objects);

  int NumberOfScavengeTasks();

  void ProcessWeakReferences(
      EphemeronRememberedSet::TableList* ephemeron_table_list);
  void ClearYoungEphemerons(
      EphemeronRememberedSet::TableList* ephemeron_table_list);
  void ClearOldEphemerons();
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
  QuarantinedPageSweeper quarantined_page_sweeper_;

  friend class Scavenger;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SCAVENGER_H_
