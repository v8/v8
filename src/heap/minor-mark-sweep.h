// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MINOR_MARK_SWEEP_H_
#define V8_HEAP_MINOR_MARK_SWEEP_H_

#include <atomic>
#include <memory>
#include <vector>

#include "src/common/globals.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/heap.h"
#include "src/heap/index-generator.h"
#include "src/heap/marking-state.h"
#include "src/heap/marking-visitor.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/parallel-work-item.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/slot-set.h"
#include "src/heap/sweeper.h"

namespace v8 {
namespace internal {

class YoungGenerationMainMarkingVisitor final
    : public YoungGenerationMarkingVisitorBase<
          YoungGenerationMainMarkingVisitor> {
 public:
  YoungGenerationMainMarkingVisitor(
      Isolate* isolate, MarkingWorklists::Local* worklists_local,
      EphemeronRememberedSet::TableList::Local* ephemeron_table_list_local);

  ~YoungGenerationMainMarkingVisitor() override;

  YoungGenerationMainMarkingVisitor(const YoungGenerationMainMarkingVisitor&) =
      delete;
  YoungGenerationMainMarkingVisitor& operator=(
      const YoungGenerationMainMarkingVisitor&) = delete;

  template <typename TSlot>
  V8_INLINE void VisitPointersImpl(HeapObject host, TSlot start, TSlot end);

  V8_INLINE void IncrementLiveBytesCached(MemoryChunk* chunk, intptr_t by);

  template <typename TSlot>
  V8_INLINE bool VisitObjectViaSlotInRemeberedSet(TSlot slot);

  V8_INLINE bool ShortCutStrings(HeapObjectSlot slot, HeapObject* heap_object);

 private:
  PretenuringHandler::PretenuringFeedbackMap local_pretenuring_feedback_;
  const bool shortcut_strings_;

  static constexpr size_t kNumEntries = 128;
  static constexpr size_t kEntriesMask = kNumEntries - 1;
  // Fixed-size hashmap that caches live bytes. Hashmap entries are evicted to
  // the global counters on collision.
  std::array<std::pair<MemoryChunk*, size_t>, kNumEntries> live_bytes_data_;

  friend class YoungGenerationMarkingVisitorBase<
      YoungGenerationMainMarkingVisitor>;
};

class YoungGenerationRememberedSetsMarkingWorklist {
 private:
  class MarkingItem;

 public:
  class Local {
   public:
    explicit Local(YoungGenerationRememberedSetsMarkingWorklist* handler)
        : handler_(handler) {}

    template <typename Visitor>
    bool ProcessNextItem(Visitor* visitor) {
      return handler_->ProcessNextItem(visitor, index_);
    }

   private:
    YoungGenerationRememberedSetsMarkingWorklist* const handler_;
    base::Optional<size_t> index_;
  };

  static std::vector<MarkingItem> CollectItems(Heap* heap);

  explicit YoungGenerationRememberedSetsMarkingWorklist(Heap* heap);
  ~YoungGenerationRememberedSetsMarkingWorklist();

  size_t RemainingRememberedSetsMarkingIteams() const {
    return remaining_remembered_sets_marking_items_.load(
        std::memory_order_relaxed);
  }

  void TearDown();

 private:
  class MarkingItem : public ParallelWorkItem {
   public:
    enum class SlotsType { kRegularSlots, kTypedSlots };

    MarkingItem(MemoryChunk* chunk, SlotsType slots_type, SlotSet* slot_set,
                SlotSet* background_slot_set)
        : chunk_(chunk),
          slots_type_(slots_type),
          slot_set_(slot_set),
          background_slot_set_(background_slot_set) {}
    MarkingItem(MemoryChunk* chunk, SlotsType slots_type,
                TypedSlotSet* typed_slot_set)
        : chunk_(chunk),
          slots_type_(slots_type),
          typed_slot_set_(typed_slot_set) {}
    ~MarkingItem() = default;

    template <typename Visitor>
    void Process(Visitor* visitor);
    void MergeAndDeleteRememberedSets();

    void DeleteSetsOnTearDown();

   private:
    inline Heap* heap() { return chunk_->heap(); }

    template <typename Visitor>
    void MarkUntypedPointers(Visitor* visitor);
    template <typename Visitor>
    void MarkTypedPointers(Visitor* visitor);
    template <typename Visitor, typename TSlot>
    V8_INLINE SlotCallbackResult CheckAndMarkObject(Visitor* visitor,
                                                    TSlot slot);

    V8_INLINE void CheckOldToNewSlotForSharedUntyped(MemoryChunk* chunk,
                                                     Address slot_address,
                                                     MaybeObject object);
    V8_INLINE void CheckOldToNewSlotForSharedTyped(MemoryChunk* chunk,
                                                   SlotType slot_type,
                                                   Address slot_address,
                                                   MaybeObject new_target);

    MemoryChunk* const chunk_;
    const SlotsType slots_type_;
    union {
      SlotSet* slot_set_;
      TypedSlotSet* typed_slot_set_;
    };
    SlotSet* background_slot_set_ = nullptr;
  };

  template <typename Visitor>
  bool ProcessNextItem(Visitor* visitor, base::Optional<size_t>& index);

  std::vector<MarkingItem> remembered_sets_marking_items_;
  std::atomic_size_t remaining_remembered_sets_marking_items_;
  IndexGenerator remembered_sets_marking_index_generator_;
};

class YoungGenerationRootMarkingVisitor final : public RootVisitor {
 public:
  explicit YoungGenerationRootMarkingVisitor(
      YoungGenerationMainMarkingVisitor* main_marking_visitor);
  ~YoungGenerationRootMarkingVisitor();

  V8_INLINE void VisitRootPointer(Root root, const char* description,
                                  FullObjectSlot p) final;

  V8_INLINE void VisitRootPointers(Root root, const char* description,
                                   FullObjectSlot start,
                                   FullObjectSlot end) final;

  GarbageCollector collector() const override {
    return GarbageCollector::MINOR_MARK_SWEEPER;
  }

 private:
  template <typename TSlot>
  void VisitPointersImpl(Root root, TSlot start, TSlot end);

  YoungGenerationMainMarkingVisitor* const main_marking_visitor_;
};

// Collector for young-generation only.
class MinorMarkSweepCollector final {
 public:
  static constexpr size_t kMaxParallelTasks = 8;

  explicit MinorMarkSweepCollector(Heap* heap);
  ~MinorMarkSweepCollector();

  void TearDown();
  void CollectGarbage();
  void StartMarking();

  EphemeronRememberedSet::TableList* ephemeron_table_list() const {
    return ephemeron_table_list_.get();
  }

  MarkingWorklists* marking_worklists() { return marking_worklists_.get(); }

  MarkingWorklists::Local* local_marking_worklists() const {
    return local_marking_worklists_.get();
  }

  YoungGenerationRememberedSetsMarkingWorklist*
  remembered_sets_marking_handler() {
    DCHECK_NOT_NULL(remembered_sets_marking_handler_);
    return remembered_sets_marking_handler_.get();
  }

  YoungGenerationMainMarkingVisitor* main_marking_visitor() {
    return main_marking_visitor_.get();
  }

 private:
  using ResizeNewSpaceMode = Heap::ResizeNewSpaceMode;

  class RootMarkingVisitor;

  Sweeper* sweeper() { return sweeper_; }

  void MarkLiveObjects();
  void MarkRoots(YoungGenerationRootMarkingVisitor& root_visitor,
                 bool was_marked_incrementally);
  void DoParallelMarking();
  void DrainMarkingWorklist();
  void MarkRootsFromTracedHandles(
      YoungGenerationRootMarkingVisitor& root_visitor);
  void MarkRootsFromConservativeStack(
      YoungGenerationRootMarkingVisitor& root_visitor);

  void TraceFragmentation();
  void ClearNonLiveReferences();
  void FinishConcurrentMarking();
  // Perform Wrapper Tracing if in use.
  void PerformWrapperTracing();

  void Sweep();
  // 'StartSweepNewSpace' and 'SweepNewLargeSpace' return true if any pages were
  // promoted.
  bool StartSweepNewSpace();
  bool SweepNewLargeSpace();

  void Finish();

  Heap* const heap_;

  std::unique_ptr<MarkingWorklists> marking_worklists_;
  std::unique_ptr<MarkingWorklists::Local> local_marking_worklists_;

  std::unique_ptr<EphemeronRememberedSet::TableList> ephemeron_table_list_;
  std::unique_ptr<EphemeronRememberedSet::TableList::Local>
      local_ephemeron_table_list_;
  std::unique_ptr<YoungGenerationMainMarkingVisitor> main_marking_visitor_;

  MarkingState* const marking_state_;
  NonAtomicMarkingState* const non_atomic_marking_state_;
  Sweeper* const sweeper_;

  std::unique_ptr<YoungGenerationRememberedSetsMarkingWorklist>
      remembered_sets_marking_handler_;

  ResizeNewSpaceMode resize_new_space_ = ResizeNewSpaceMode::kNone;

  friend class IncrementalMarking;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MINOR_MARK_SWEEP_H_
