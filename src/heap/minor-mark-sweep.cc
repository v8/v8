// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/minor-mark-sweep.h"

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

#include "src/base/logging.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/compilation-cache.h"
#include "src/common/globals.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles.h"
#include "src/heap/array-buffer-sweeper.h"
#include "src/heap/concurrent-marking.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap.h"
#include "src/heap/large-spaces.h"
#include "src/heap/mark-sweep-utilities.h"
#include "src/heap/marking-barrier.h"
#include "src/heap/marking-visitor-inl.h"
#include "src/heap/marking-visitor-utility-inl.h"
#include "src/heap/marking-visitor.h"
#include "src/heap/marking-worklist-inl.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/memory-chunk.h"
#include "src/heap/minor-mark-sweep-inl.h"
#include "src/heap/new-spaces.h"
#include "src/heap/object-stats.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/read-only-spaces.h"
#include "src/heap/remembered-set.h"
#include "src/heap/safepoint.h"
#include "src/heap/slot-set.h"
#include "src/heap/sweeper.h"
#include "src/heap/traced-handles-marking-visitor.h"
#include "src/heap/weak-object-worklists.h"
#include "src/init/v8.h"
#include "src/objects/js-collection-inl.h"
#include "src/objects/objects.h"
#include "src/objects/string-forwarding-table-inl.h"
#include "src/objects/visitors.h"
#include "src/snapshot/shared-heap-serializer.h"
#include "src/tasks/cancelable-task.h"
#include "src/utils/utils-inl.h"

namespace v8 {
namespace internal {

// ==================================================================
// Verifiers
// ==================================================================

#ifdef VERIFY_HEAP
namespace {

class YoungGenerationMarkingVerifier : public MarkingVerifierBase {
 public:
  explicit YoungGenerationMarkingVerifier(Heap* heap)
      : MarkingVerifierBase(heap),
        marking_state_(heap->non_atomic_marking_state()) {}

  const MarkingBitmap* bitmap(const MemoryChunk* chunk) override {
    return chunk->marking_bitmap();
  }

  bool IsMarked(HeapObject object) override {
    return marking_state_->IsMarked(object);
  }

  void Run() override {
    VerifyRoots();
    VerifyMarking(heap_->new_space());
  }

  GarbageCollector collector() const override {
    return GarbageCollector::MINOR_MARK_SWEEPER;
  }

 protected:
  void VerifyMap(Map map) override { VerifyHeapObjectImpl(map); }

  void VerifyPointers(ObjectSlot start, ObjectSlot end) override {
    VerifyPointersImpl(start, end);
  }

  void VerifyPointers(MaybeObjectSlot start, MaybeObjectSlot end) override {
    VerifyPointersImpl(start, end);
  }
  void VerifyCodePointer(InstructionStreamSlot slot) override {
    // Code slots never appear in new space because
    // Code objects, the only object that can contain code pointers, are
    // always allocated in the old space.
    UNREACHABLE();
  }

  void VisitCodeTarget(InstructionStream host, RelocInfo* rinfo) override {
    InstructionStream target =
        InstructionStream::FromTargetAddress(rinfo->target_address());
    VerifyHeapObjectImpl(target);
  }
  void VisitEmbeddedPointer(InstructionStream host, RelocInfo* rinfo) override {
    VerifyHeapObjectImpl(rinfo->target_object(cage_base()));
  }
  void VerifyRootPointers(FullObjectSlot start, FullObjectSlot end) override {
    VerifyPointersImpl(start, end);
  }

 private:
  V8_INLINE void VerifyHeapObjectImpl(HeapObject heap_object) {
    CHECK_IMPLIES(Heap::InYoungGeneration(heap_object), IsMarked(heap_object));
  }

  template <typename TSlot>
  V8_INLINE void VerifyPointersImpl(TSlot start, TSlot end) {
    PtrComprCageBase cage_base =
        GetPtrComprCageBaseFromOnHeapAddress(start.address());
    for (TSlot slot = start; slot < end; ++slot) {
      typename TSlot::TObject object = slot.load(cage_base);
      HeapObject heap_object;
      // Minor MS treats weak references as strong.
      if (object.GetHeapObject(&heap_object)) {
        VerifyHeapObjectImpl(heap_object);
      }
    }
  }

  NonAtomicMarkingState* const marking_state_;
};

}  // namespace
#endif  // VERIFY_HEAP

// =============================================================================
// MinorMarkSweepCollector
// =============================================================================

YoungGenerationMainMarkingVisitor::YoungGenerationMainMarkingVisitor(
    Isolate* isolate, MarkingWorklists::Local* worklists_local,
    EphemeronRememberedSet::TableList::Local* ephemeron_table_list_local)
    : YoungGenerationMarkingVisitorBase<YoungGenerationMainMarkingVisitor>(
          isolate, worklists_local, ephemeron_table_list_local,
          &local_pretenuring_feedback_),
      local_pretenuring_feedback_(PretenuringHandler::kInitialFeedbackCapacity),
      shortcut_strings_(isolate->heap()->CanShortcutStringsDuringGC(
          GarbageCollector::MINOR_MARK_SWEEPER)) {}

YoungGenerationMainMarkingVisitor::~YoungGenerationMainMarkingVisitor() {
  // The visitor should only be destroyed on the main thread since
  // `MergeAllocationSitePretenuringFeedback` should not be called concurrently.
  pretenuring_handler()->MergeAllocationSitePretenuringFeedback(
      local_pretenuring_feedback_);
  local_pretenuring_feedback_.clear();

  for (auto& pair : live_bytes_data_) {
    if (pair.first) {
      pair.first->IncrementLiveBytesAtomically(pair.second);
    }
  }
}

class YoungGenerationMarkingTask final {
 public:
  YoungGenerationMarkingTask(
      Isolate* isolate, Heap* heap, MarkingWorklists* global_worklists,
      EphemeronRememberedSet::TableList* ephemeron_table_list);
  ~YoungGenerationMarkingTask();

  YoungGenerationMarkingTask(const YoungGenerationMarkingTask&) = delete;
  YoungGenerationMarkingTask& operator=(const YoungGenerationMarkingTask&) =
      delete;

  void DrainMarkingWorklist();

  YoungGenerationMainMarkingVisitor* visitor() { return &visitor_; }

 private:
  MarkingWorklists::Local marking_worklists_local_;
  EphemeronRememberedSet::TableList::Local ephemeron_table_list_local_;
  YoungGenerationMainMarkingVisitor visitor_;
};

YoungGenerationMarkingTask::YoungGenerationMarkingTask(
    Isolate* isolate, Heap* heap, MarkingWorklists* global_worklists,
    EphemeronRememberedSet::TableList* ephemeron_table_list)
    : marking_worklists_local_(
          global_worklists,
          heap->cpp_heap()
              ? CppHeap::From(heap->cpp_heap())->CreateCppMarkingState()
              : MarkingWorklists::Local::kNoCppMarkingState),
      ephemeron_table_list_local_(*ephemeron_table_list),
      visitor_(isolate, &marking_worklists_local_,
               &ephemeron_table_list_local_) {}

YoungGenerationMarkingTask::~YoungGenerationMarkingTask() {
  // The list is not empty, as it is not processed in `DrainMarkingWorklist()`.
  ephemeron_table_list_local_.Publish();
}

void YoungGenerationMarkingTask::DrainMarkingWorklist() {
  HeapObject heap_object;
  while (marking_worklists_local_.Pop(&heap_object)) {
    // Maps won't change in the atomic pause, so the map can be read without
    // atomics.
    Map map = Map::cast(*heap_object->map_slot());
    // kDataOnly objects are filtered on push.
    DCHECK_EQ(Map::ObjectFieldsFrom(map->visitor_id()),
              ObjectFields::kMaybePointers);
    const auto visited_size = visitor_.Visit(map, heap_object);
    if (visited_size) {
      visitor_.IncrementLiveBytesCached(
          MemoryChunk::FromHeapObject(heap_object),
          ALIGN_TO_ALLOCATION_ALIGNMENT(visited_size));
    }
  }
  // Publish wrapper objects to the cppgc marking state, if registered.
  marking_worklists_local_.PublishWrapper();
}

class YoungGenerationMarkingJob : public v8::JobTask {
 public:
  YoungGenerationMarkingJob(
      Isolate* isolate, Heap* heap, MarkingWorklists* global_worklists,
      const std::vector<std::unique_ptr<YoungGenerationMarkingTask>>& tasks);

  void Run(JobDelegate* delegate) override;
  size_t GetMaxConcurrency(size_t worker_count) const override;

  uint64_t trace_id() const { return trace_id_; }

 private:
  void ProcessItems(JobDelegate* delegate);

  Isolate* isolate_;
  Heap* heap_;
  MarkingWorklists* global_worklists_;
  const std::vector<std::unique_ptr<YoungGenerationMarkingTask>>& tasks_;
  YoungGenerationRememberedSetsMarkingWorklist* const
      remembered_sets_marking_handler_;
  const uint64_t trace_id_;
};

YoungGenerationMarkingJob::YoungGenerationMarkingJob(
    Isolate* isolate, Heap* heap, MarkingWorklists* global_worklists,
    const std::vector<std::unique_ptr<YoungGenerationMarkingTask>>& tasks)
    : isolate_(isolate),
      heap_(heap),
      global_worklists_(global_worklists),
      tasks_(tasks),
      remembered_sets_marking_handler_(heap_->minor_mark_sweep_collector()
                                           ->remembered_sets_marking_handler()),
      trace_id_(reinterpret_cast<uint64_t>(this) ^
                heap_->tracer()->CurrentEpoch(
                    GCTracer::Scope::MINOR_MS_MARK_PARALLEL)) {}

void YoungGenerationMarkingJob::Run(JobDelegate* delegate) {
  if (delegate->IsJoiningThread()) {
    TRACE_GC_WITH_FLOW(heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK_PARALLEL,
                       trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
    ProcessItems(delegate);
  } else {
    TRACE_GC_EPOCH_WITH_FLOW(
        heap_->tracer(), GCTracer::Scope::MINOR_MS_BACKGROUND_MARKING,
        ThreadKind::kBackground, trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
    ProcessItems(delegate);
  }
}

size_t YoungGenerationMarkingJob::GetMaxConcurrency(size_t worker_count) const {
  // Pages are not private to markers but we can still use them to estimate
  // the amount of marking that is required.
  const int kPagesPerTask = 2;
  size_t items =
      remembered_sets_marking_handler_->RemainingRememberedSetsMarkingIteams();
  size_t num_tasks = std::max((items + 1) / kPagesPerTask,
                              global_worklists_->shared()->Size() +
                                  global_worklists_->on_hold()->Size());

  if (!v8_flags.parallel_marking) {
    num_tasks = std::min<size_t>(1, num_tasks);
  }
  return std::min<size_t>(num_tasks,
                          MinorMarkSweepCollector::kMaxParallelTasks);
}

void YoungGenerationMarkingJob::ProcessItems(JobDelegate* delegate) {
  double marking_time = 0.0;
  {
    TimedScope scope(&marking_time);
    const int task_id = delegate->GetTaskId();
    DCHECK_LT(task_id, tasks_.size());
    YoungGenerationMarkingTask* task = tasks_[task_id].get();
    YoungGenerationRememberedSetsMarkingWorklist::Local remembered_sets(
        remembered_sets_marking_handler_);
    while (remembered_sets.ProcessNextItem(task->visitor())) {
      task->DrainMarkingWorklist();
    }
    task->DrainMarkingWorklist();
  }
  if (v8_flags.trace_minor_ms_parallel_marking) {
    PrintIsolate(isolate_, "marking[%p]: time=%f\n", static_cast<void*>(this),
                 marking_time);
  }
}

namespace {
int EstimateMaxNumberOfRemeberedSets(Heap* heap) {
  return 2 * (heap->old_space()->CountTotalPages() +
              heap->lo_space()->PageCount()) +
         3 * (heap->code_space()->CountTotalPages() +
              heap->code_lo_space()->PageCount());
}
}  // namespace

// static
std::vector<YoungGenerationRememberedSetsMarkingWorklist::MarkingItem>
YoungGenerationRememberedSetsMarkingWorklist::CollectItems(Heap* heap) {
  std::vector<MarkingItem> items;
  int max_remembered_set_count = EstimateMaxNumberOfRemeberedSets(heap);
  items.reserve(max_remembered_set_count);
  CodePageHeaderModificationScope rwx_write_scope(
      "Extracting of slot sets requires write access to Code page "
      "header");
  OldGenerationMemoryChunkIterator::ForAll(heap, [&items](MemoryChunk* chunk) {
    SlotSet* slot_set = chunk->ExtractSlotSet<OLD_TO_NEW>();
    SlotSet* background_slot_set =
        chunk->ExtractSlotSet<OLD_TO_NEW_BACKGROUND>();
    if (slot_set || background_slot_set) {
      items.emplace_back(chunk, MarkingItem::SlotsType::kRegularSlots, slot_set,
                         background_slot_set);
    }
    if (TypedSlotSet* typed_slot_set =
            chunk->ExtractTypedSlotSet<OLD_TO_NEW>()) {
      DCHECK(chunk->owner_identity() == CODE_SPACE ||
             chunk->owner_identity() == CODE_LO_SPACE);
      items.emplace_back(chunk, MarkingItem::SlotsType::kTypedSlots,
                         typed_slot_set);
    }
  });
  DCHECK_LE(items.size(), max_remembered_set_count);
  return items;
}

void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::
    MergeAndDeleteRememberedSets() {
  DCHECK(IsAcquired());
  if (slots_type_ == SlotsType::kRegularSlots) {
    if (slot_set_)
      RememberedSet<OLD_TO_NEW>::MergeAndDelete(chunk_, std::move(*slot_set_));
    if (background_slot_set_)
      RememberedSet<OLD_TO_NEW_BACKGROUND>::MergeAndDelete(
          chunk_, std::move(*background_slot_set_));
  } else {
    DCHECK_EQ(slots_type_, SlotsType::kTypedSlots);
    DCHECK_NULL(background_slot_set_);
    if (typed_slot_set_)
      RememberedSet<OLD_TO_NEW>::MergeAndDeleteTyped(
          chunk_, std::move(*typed_slot_set_));
  }
}

void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::
    DeleteSetsOnTearDown() {
  if (slots_type_ == SlotsType::kRegularSlots) {
    if (slot_set_) SlotSet::Delete(slot_set_, chunk_->buckets());
    if (background_slot_set_)
      SlotSet::Delete(background_slot_set_, chunk_->buckets());

  } else {
    DCHECK_EQ(slots_type_, SlotsType::kTypedSlots);
    DCHECK_NULL(background_slot_set_);
    if (typed_slot_set_) delete typed_slot_set_;
  }
}

YoungGenerationRememberedSetsMarkingWorklist::
    YoungGenerationRememberedSetsMarkingWorklist(Heap* heap)
    : remembered_sets_marking_items_(CollectItems(heap)),
      remaining_remembered_sets_marking_items_(
          remembered_sets_marking_items_.size()),
      remembered_sets_marking_index_generator_(
          remembered_sets_marking_items_.size()) {}

YoungGenerationRememberedSetsMarkingWorklist::
    ~YoungGenerationRememberedSetsMarkingWorklist() {
  CodePageHeaderModificationScope rwx_write_scope(
      "Merging slot sets back to pages requires write access to Code page "
      "header");
  for (MarkingItem item : remembered_sets_marking_items_) {
    item.MergeAndDeleteRememberedSets();
  }
}

void YoungGenerationRememberedSetsMarkingWorklist::TearDown() {
  for (MarkingItem& item : remembered_sets_marking_items_) {
    item.DeleteSetsOnTearDown();
  }
  remembered_sets_marking_items_.clear();
  remaining_remembered_sets_marking_items_.store(0, std::memory_order_relaxed);
}

YoungGenerationRootMarkingVisitor::YoungGenerationRootMarkingVisitor(
    YoungGenerationMainMarkingVisitor* main_marking_visitor)
    : main_marking_visitor_(main_marking_visitor) {}

YoungGenerationRootMarkingVisitor::~YoungGenerationRootMarkingVisitor() =
    default;

// static
constexpr size_t MinorMarkSweepCollector::kMaxParallelTasks;

MinorMarkSweepCollector::MinorMarkSweepCollector(Heap* heap)
    : heap_(heap),
      marking_state_(heap_->marking_state()),
      non_atomic_marking_state_(heap_->non_atomic_marking_state()),
      sweeper_(heap_->sweeper()) {}

void MinorMarkSweepCollector::PerformWrapperTracing() {
  auto* cpp_heap = CppHeap::From(heap_->cpp_heap_);
  if (!cpp_heap) return;

  TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK_EMBEDDER_TRACING);
  cpp_heap->AdvanceTracing(v8::base::TimeDelta::Max());
}

MinorMarkSweepCollector::~MinorMarkSweepCollector() = default;

void MinorMarkSweepCollector::TearDown() {
  if (heap_->incremental_marking()->IsMinorMarking()) {
    DCHECK(heap_->concurrent_marking()->IsStopped());
    remembered_sets_marking_handler_->TearDown();
    local_marking_worklists_->Publish();
    local_ephemeron_table_list_->Publish();
    heap_->main_thread_local_heap_->marking_barrier()->PublishIfNeeded();
    // Marking barriers of LocalHeaps will be published in their destructors.
    marking_worklists_->Clear();
    ephemeron_table_list_->Clear();
  }
}

void MinorMarkSweepCollector::FinishConcurrentMarking() {
  if (v8_flags.concurrent_minor_ms_marking) {
    DCHECK_IMPLIES(!heap_->concurrent_marking()->IsStopped(),
                   heap_->concurrent_marking()->garbage_collector() ==
                       GarbageCollector::MINOR_MARK_SWEEPER);
    heap_->concurrent_marking()->Join();
    heap_->concurrent_marking()->FlushMemoryChunkData(
        non_atomic_marking_state_);
    // Concurrent marking may have pushed a few objects to OnHold after the last
    // time it was merged.
    local_marking_worklists_->MergeOnHold();
  }
  if (auto* cpp_heap = CppHeap::From(heap_->cpp_heap_)) {
    cpp_heap->FinishConcurrentMarkingIfNeeded();
  }
}

void MinorMarkSweepCollector::StartMarking() {
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) {
    for (Page* page : *heap_->new_space()) {
      CHECK(page->marking_bitmap()->IsClean());
    }
  }
#endif  // VERIFY_HEAP

  auto* cpp_heap = CppHeap::From(heap_->cpp_heap_);
  if (cpp_heap && cpp_heap->generational_gc_supported()) {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK_EMBEDDER_PROLOGUE);
    // InitializeTracing should be called before visitor initialization in
    // StartMarking.
    cpp_heap->InitializeTracing(CppHeap::CollectionType::kMinor);
  }
  DCHECK_NULL(ephemeron_table_list_);
  ephemeron_table_list_ = std::make_unique<EphemeronRememberedSet::TableList>();
  local_ephemeron_table_list_ =
      std::make_unique<EphemeronRememberedSet::TableList::Local>(
          *ephemeron_table_list_.get());
  DCHECK_NULL(marking_worklists_);
  marking_worklists_ = std::make_unique<MarkingWorklists>();
  DCHECK_NULL(local_marking_worklists_);
  local_marking_worklists_ = std::make_unique<MarkingWorklists::Local>(
      marking_worklists_.get(),
      cpp_heap ? cpp_heap->CreateCppMarkingStateForMutatorThread()
               : MarkingWorklists::Local::kNoCppMarkingState);
  DCHECK_NULL(main_marking_visitor_);
  main_marking_visitor_ = std::make_unique<YoungGenerationMainMarkingVisitor>(
      heap_->isolate(), local_marking_worklists_.get(),
      local_ephemeron_table_list_.get());
  DCHECK_NULL(remembered_sets_marking_handler_);
  remembered_sets_marking_handler_ =
      std::make_unique<YoungGenerationRememberedSetsMarkingWorklist>(heap_);
  if (cpp_heap && cpp_heap->generational_gc_supported()) {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK_EMBEDDER_PROLOGUE);
    // StartTracing immediately starts marking which requires V8 worklists to
    // be set up.
    cpp_heap->StartTracing();
  }
}

void MinorMarkSweepCollector::Finish() {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_FINISH);

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_FINISH_ENSURE_CAPACITY);
    switch (resize_new_space_) {
      case ResizeNewSpaceMode::kShrink:
        heap_->ReduceNewSpaceSize();
        break;
      case ResizeNewSpaceMode::kGrow:
        heap_->ExpandNewSpaceSize();
        break;
      case ResizeNewSpaceMode::kNone:
        break;
    }
    resize_new_space_ = ResizeNewSpaceMode::kNone;

    if (!heap_->new_space()->EnsureCurrentCapacity()) {
      heap_->FatalProcessOutOfMemory("NewSpace::EnsureCurrentCapacity");
    }
  }

  heap_->new_space()->GarbageCollectionEpilogue();
}

void MinorMarkSweepCollector::CollectGarbage() {
  DCHECK(!heap_->mark_compact_collector()->in_use());
  DCHECK_NOT_NULL(heap_->new_space());
  DCHECK(!heap_->array_buffer_sweeper()->sweeping_in_progress());
  DCHECK(!sweeper()->AreMinorSweeperTasksRunning());
  DCHECK(sweeper()->IsSweepingDoneForSpace(NEW_SPACE));

  heap_->new_space()->FreeLinearAllocationArea();
  heap_->new_lo_space()->ResetPendingObject();

  MarkLiveObjects();
  ClearNonLiveReferences();
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK_VERIFY);
    YoungGenerationMarkingVerifier verifier(heap_);
    verifier.Run();
  }
#endif  // VERIFY_HEAP

  Sweep();
  Finish();

  auto* isolate = heap_->isolate();
  isolate->global_handles()->UpdateListOfYoungNodes();
  isolate->traced_handles()->UpdateListOfYoungNodes();
}

namespace {

class YoungStringForwardingTableCleaner final
    : public StringForwardingTableCleanerBase {
 public:
  explicit YoungStringForwardingTableCleaner(Heap* heap)
      : StringForwardingTableCleanerBase(heap) {}

  // For Minor MS we don't mark forward objects, because they are always
  // in old generation (and thus considered live).
  // We only need to delete non-live young objects.
  void ProcessYoungObjects() {
    DCHECK(v8_flags.always_use_string_forwarding_table);
    StringForwardingTable* forwarding_table =
        isolate_->string_forwarding_table();
    forwarding_table->IterateElements(
        [&](StringForwardingTable::Record* record) {
          ClearNonLiveYoungObjects(record);
        });
  }

 private:
  void ClearNonLiveYoungObjects(StringForwardingTable::Record* record) {
    Object original = record->OriginalStringObject(isolate_);
    if (!original.IsHeapObject()) {
      DCHECK_EQ(original, StringForwardingTable::deleted_element());
      return;
    }
    String original_string = String::cast(original);
    if (!Heap::InYoungGeneration(original_string)) return;
    if (!marking_state_->IsMarked(original_string)) {
      DisposeExternalResource(record);
      record->set_original_string(StringForwardingTable::deleted_element());
    }
  }
};

bool IsUnmarkedObjectInYoungGeneration(Heap* heap, FullObjectSlot p) {
  DCHECK_IMPLIES(Heap::InYoungGeneration(*p), Heap::InToPage(*p));
  return Heap::InYoungGeneration(*p) &&
         !heap->non_atomic_marking_state()->IsMarked(HeapObject::cast(*p));
}

}  // namespace

void MinorMarkSweepCollector::ClearNonLiveReferences() {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_CLEAR);

  if (V8_UNLIKELY(v8_flags.always_use_string_forwarding_table)) {
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MINOR_MS_CLEAR_STRING_FORWARDING_TABLE);
    // Clear non-live objects in the string fowarding table.
    YoungStringForwardingTableCleaner forwarding_table_cleaner(heap_);
    forwarding_table_cleaner.ProcessYoungObjects();
  }

  Heap::ExternalStringTable& external_string_table =
      heap_->external_string_table_;
  if (external_string_table.HasYoung()) {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_CLEAR_STRING_TABLE);
    // Internalized strings are always stored in old space, so there is no
    // need to clean them here.
    ExternalStringTableCleanerVisitor<
        ExternalStringTableCleaningMode::kYoungOnly>
        external_visitor(heap_);
    external_string_table.IterateYoung(&external_visitor);
    external_string_table.CleanUpYoung();
  }

  Isolate* isolate = heap_->isolate();
  if (isolate->global_handles()->HasYoung() ||
      isolate->traced_handles()->HasYoung()) {
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MINOR_MS_CLEAR_WEAK_GLOBAL_HANDLES);
    isolate->global_handles()->ProcessWeakYoungObjects(
        nullptr, &IsUnmarkedObjectInYoungGeneration);
    if (auto* cpp_heap = CppHeap::From(heap_->cpp_heap_);
        cpp_heap && cpp_heap->generational_gc_supported()) {
      isolate->traced_handles()->ResetYoungDeadNodes(
          &IsUnmarkedObjectInYoungGeneration);
    } else {
      isolate->traced_handles()->ProcessYoungObjects(
          nullptr, &IsUnmarkedObjectInYoungGeneration);
    }
  }

  // Clear ephemeron entries from EphemeronHashTables in the young generation
  // whenever the entry has a dead young generation key.
  //
  // Worklist is collected during marking.
  EphemeronHashTable table;
  while (local_ephemeron_table_list_->Pop(&table)) {
    for (InternalIndex i : table->IterateEntries()) {
      // Keys in EphemeronHashTables must be heap objects.
      HeapObjectSlot key_slot(
          table->RawFieldOfElementAt(EphemeronHashTable::EntryToIndex(i)));
      HeapObject key = key_slot.ToHeapObject();
      if (Heap::InYoungGeneration(key) &&
          non_atomic_marking_state_->IsUnmarked(key)) {
        table->RemoveEntry(i);
      }
    }
  }
  local_ephemeron_table_list_.reset();
  ephemeron_table_list_.reset();

  // Clear ephemeron entries from EphemeronHashTables in the old generation
  // whenever the entry has a dead young generation key.
  //
  // Does not need to be iterated as roots but is maintained in the GC to avoid
  // treating keys as strong. The set is populated from the write barrier and
  // the sweeper during promoted pages iteration.
  auto* table_map = heap_->ephemeron_remembered_set()->tables();
  for (auto it = table_map->begin(); it != table_map->end();) {
    EphemeronHashTable table = it->first;
    auto& indices = it->second;
    for (auto iti = indices.begin(); iti != indices.end();) {
      // Keys in EphemeronHashTables must be heap objects.
      HeapObjectSlot key_slot(table->RawFieldOfElementAt(
          EphemeronHashTable::EntryToIndex(InternalIndex(*iti))));
      HeapObject key = key_slot.ToHeapObject();
      // There may be old generation entries left in the remembered set as
      // MinorMS only promotes pages after clearing non-live references.
      if (!Heap::InYoungGeneration(key)) {
        iti = indices.erase(iti);
      } else if (non_atomic_marking_state_->IsUnmarked(key)) {
        table->RemoveEntry(InternalIndex(*iti));
        iti = indices.erase(iti);
      } else {
        ++iti;
      }
    }

    if (indices.size() == 0) {
      it = table_map->erase(it);
    } else {
      ++it;
    }
  }
}

namespace {
void VisitObjectWithEmbedderFields(JSObject object,
                                   MarkingWorklists::Local& worklist) {
  DCHECK(object->MayHaveEmbedderFields());
  DCHECK(!Heap::InYoungGeneration(object));

  MarkingWorklists::Local::WrapperSnapshot wrapper_snapshot;
  const bool valid_snapshot =
      worklist.ExtractWrapper(object->map(), object, wrapper_snapshot);
  DCHECK(valid_snapshot);
  USE(valid_snapshot);
  worklist.PushExtractedWrapper(wrapper_snapshot);
}
}  // namespace

void MinorMarkSweepCollector::MarkRootsFromTracedHandles(
    YoungGenerationRootMarkingVisitor& root_visitor) {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK_TRACED_HANDLES);
  if (auto* cpp_heap = CppHeap::From(heap_->cpp_heap_);
      cpp_heap && cpp_heap->generational_gc_supported()) {
    // Visit the Oilpan-to-V8 remembered set.
    heap_->isolate()->traced_handles()->IterateAndMarkYoungRootsWithOldHosts(
        &root_visitor);
    // Visit the V8-to-Oilpan remembered set.
    cpp_heap->VisitCrossHeapRememberedSetIfNeeded([this](JSObject obj) {
      VisitObjectWithEmbedderFields(obj, *local_marking_worklists_);
    });
  } else {
    // Otherwise, visit all young roots.
    heap_->isolate()->traced_handles()->IterateYoungRoots(&root_visitor);
  }
}

void MinorMarkSweepCollector::MarkRoots(
    YoungGenerationRootMarkingVisitor& root_visitor,
    bool was_marked_incrementally) {
  Isolate* isolate = heap_->isolate();

  // Seed the root set.
  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK_SEED);
    isolate->traced_handles()->ComputeWeaknessForYoungObjects(
        &JSObject::IsUnmodifiedApiObject);
    // MinorMS treats all weak roots except for global handles as strong.
    // That is why we don't set skip_weak = true here and instead visit
    // global handles separately.
    heap_->IterateRoots(
        &root_visitor,
        base::EnumSet<SkipRoot>{
            SkipRoot::kExternalStringTable, SkipRoot::kGlobalHandles,
            SkipRoot::kTracedHandles, SkipRoot::kOldGeneration,
            SkipRoot::kReadOnlyBuiltins, SkipRoot::kConservativeStack});
    isolate->global_handles()->IterateYoungStrongAndDependentRoots(
        &root_visitor);
    MarkRootsFromTracedHandles(root_visitor);
  }
}

void MinorMarkSweepCollector::DoParallelMarking() {
  DCHECK(!v8_flags.concurrent_minor_ms_marking);

  // Add tasks and run in parallel.
  std::vector<std::unique_ptr<YoungGenerationMarkingTask>> tasks;
  for (size_t i = 0; i < (v8_flags.parallel_marking ? kMaxParallelTasks : 1);
       ++i) {
    tasks.emplace_back(std::make_unique<YoungGenerationMarkingTask>(
        heap_->isolate(), heap_, marking_worklists_.get(),
        ephemeron_table_list_.get()));
  }

  auto job = std::make_unique<YoungGenerationMarkingJob>(
      heap_->isolate(), heap_, marking_worklists_.get(), tasks);
  TRACE_GC_NOTE_WITH_FLOW("Minor parallel marking started", job->trace_id(),
                          TRACE_EVENT_FLAG_FLOW_OUT);
  V8::GetCurrentPlatform()
      ->CreateJob(v8::TaskPriority::kUserBlocking, std::move(job))
      ->Join();

  // If unified young generation is in progress, the parallel marker may add
  // more entries into local_marking_worklists_.
  DCHECK_IMPLIES(!v8_flags.cppgc_young_generation,
                 local_marking_worklists_->IsEmpty());
}

void MinorMarkSweepCollector::MarkRootsFromConservativeStack(
    YoungGenerationRootMarkingVisitor& root_visitor) {
  heap_->IterateConservativeStackRoots(&root_visitor,
                                       Heap::ScanStackMode::kComplete,
                                       Heap::IterateRootsMode::kMainIsolate);
}

void MinorMarkSweepCollector::MarkLiveObjects() {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK);

  const bool was_marked_incrementally =
      !heap_->incremental_marking()->IsStopped();
  if (!was_marked_incrementally) {
    StartMarking();
  } else {
    auto* incremental_marking = heap_->incremental_marking();
    TRACE_GC_WITH_FLOW(
        heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK_FINISH_INCREMENTAL,
        incremental_marking->current_trace_id(), TRACE_EVENT_FLAG_FLOW_IN);
    DCHECK(incremental_marking->IsMinorMarking());
    DCHECK(v8_flags.concurrent_minor_ms_marking);
    incremental_marking->Stop();
    MarkingBarrier::PublishYoung(heap_);
  }

  DCHECK_NOT_NULL(marking_worklists_);
  DCHECK_NOT_NULL(local_marking_worklists_);
  DCHECK_NOT_NULL(main_marking_visitor_);

  YoungGenerationRootMarkingVisitor root_visitor(main_marking_visitor_.get());

  MarkRoots(root_visitor, was_marked_incrementally);

  // CppGC starts parallel marking tasks that will trace TracedReferences.
  if (heap_->cpp_heap_) {
    CppHeap::From(heap_->cpp_heap_)
        ->EnterFinalPause(heap_->embedder_stack_state_);
  }

  {
    // Mark the transitive closure in parallel.
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK_CLOSURE_PARALLEL);
    local_marking_worklists_->Publish();
    if (!v8_flags.concurrent_minor_ms_marking) {
      DoParallelMarking();
    } else {
      if (v8_flags.parallel_marking) {
        heap_->concurrent_marking()->RescheduleJobIfNeeded(
            GarbageCollector::MINOR_MARK_SWEEPER, TaskPriority::kUserBlocking);
      }
    }
    FinishConcurrentMarking();
  }

  {
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MINOR_MS_MARK_CONSERVATIVE_STACK);
    if (!v8_flags.parallel_marking && !v8_flags.concurrent_marking) {
      // Drain the worklist to populate the markbits before conservatively
      // scanning the stack.
      DrainMarkingWorklist();
    }
    MarkRootsFromConservativeStack(root_visitor);
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_MARK_CLOSURE);
    DrainMarkingWorklist();
  }

  if (was_marked_incrementally) {
    // Disable the marking barrier after concurrent/parallel marking has
    // finished as it will reset page flags.
    Sweeper::PauseMajorSweepingScope pause_sweeping_scope(heap_->sweeper());
    MarkingBarrier::DeactivateYoung(heap_);
  }

  main_marking_visitor_.reset();
  local_marking_worklists_.reset();
  marking_worklists_.reset();
  remembered_sets_marking_handler_.reset();

  if (v8_flags.minor_ms_trace_fragmentation) {
    TraceFragmentation();
  }
}

void MinorMarkSweepCollector::DrainMarkingWorklist() {
  PtrComprCageBase cage_base(heap_->isolate());
  YoungGenerationRememberedSetsMarkingWorklist::Local remembered_sets(
      remembered_sets_marking_handler_.get());
  do {
    PerformWrapperTracing();

    HeapObject heap_object;
    while (local_marking_worklists_->Pop(&heap_object)) {
      DCHECK(!heap_object.IsFreeSpaceOrFiller(cage_base));
      DCHECK(heap_object.IsHeapObject());
      DCHECK(heap_->Contains(heap_object));
      DCHECK(!non_atomic_marking_state_->IsUnmarked(heap_object));
      // Maps won't change in the atomic pause, so the map can be read without
      // atomics.
      Map map = Map::cast(*heap_object->map_slot());
      const auto visited_size = main_marking_visitor_->Visit(map, heap_object);
      // kDataOnly objects are filtered on push.
      DCHECK_IMPLIES(!v8_flags.concurrent_minor_ms_marking,
                     Map::ObjectFieldsFrom(map->visitor_id()) ==
                         ObjectFields::kMaybePointers);
      if (visited_size) {
        main_marking_visitor_->IncrementLiveBytesCached(
            MemoryChunk::FromHeapObject(heap_object),
            ALIGN_TO_ALLOCATION_ALIGNMENT(visited_size));
      }
    }
  } while (remembered_sets.ProcessNextItem(main_marking_visitor_.get()) ||
           !IsCppHeapMarkingFinished(heap_, local_marking_worklists_.get()));
  DCHECK(local_marking_worklists_->IsEmpty());
}

void MinorMarkSweepCollector::TraceFragmentation() {
  NewSpace* new_space = heap_->new_space();
  PtrComprCageBase cage_base(heap_->isolate());
  const std::array<size_t, 4> free_size_class_limits = {0, 1024, 2048, 4096};
  size_t free_bytes_of_class[free_size_class_limits.size()] = {0};
  size_t live_bytes = 0;
  size_t allocatable_bytes = 0;
  for (Page* p :
       PageRange(new_space->first_allocatable_address(), new_space->top())) {
    Address free_start = p->area_start();
    for (auto [object, size] : LiveObjectRange(p)) {
      Address free_end = object.address();
      if (free_end != free_start) {
        size_t free_bytes = free_end - free_start;
        int free_bytes_index = 0;
        for (auto free_size_class_limit : free_size_class_limits) {
          if (free_bytes >= free_size_class_limit) {
            free_bytes_of_class[free_bytes_index] += free_bytes;
          }
          free_bytes_index++;
        }
      }
      live_bytes += size;
      free_start = free_end + size;
    }
    size_t area_end =
        p->Contains(new_space->top()) ? new_space->top() : p->area_end();
    if (free_start != area_end) {
      size_t free_bytes = area_end - free_start;
      int free_bytes_index = 0;
      for (auto free_size_class_limit : free_size_class_limits) {
        if (free_bytes >= free_size_class_limit) {
          free_bytes_of_class[free_bytes_index] += free_bytes;
        }
        free_bytes_index++;
      }
    }
    allocatable_bytes += area_end - p->area_start();
    CHECK_EQ(allocatable_bytes, live_bytes + free_bytes_of_class[0]);
  }
  PrintIsolate(heap_->isolate(),
               "Minor Mark-Compact Fragmentation: allocatable_bytes=%zu "
               "live_bytes=%zu "
               "free_bytes=%zu free_bytes_1K=%zu free_bytes_2K=%zu "
               "free_bytes_4K=%zu\n",
               allocatable_bytes, live_bytes, free_bytes_of_class[0],
               free_bytes_of_class[1], free_bytes_of_class[2],
               free_bytes_of_class[3]);
}

namespace {

// NewSpacePages with more live bytes than this threshold qualify for fast
// evacuation.
intptr_t NewSpacePageEvacuationThreshold() {
  return v8_flags.minor_ms_page_promotion_threshold *
         MemoryChunkLayout::AllocatableMemoryInDataPage() / 100;
}

bool ShouldMovePage(Page* p, intptr_t live_bytes, intptr_t wasted_bytes) {
  DCHECK(v8_flags.page_promotion);
  Heap* heap = p->heap();
  DCHECK(!p->NeverEvacuate());
  const bool should_move_page =
      ((live_bytes + wasted_bytes) > NewSpacePageEvacuationThreshold() ||
       (p->AllocatedLabSize() == 0)) &&
      (heap->new_space()->IsPromotionCandidate(p)) &&
      heap->CanExpandOldGeneration(live_bytes);
  if (v8_flags.trace_page_promotions) {
    PrintIsolate(
        heap->isolate(),
        "[Page Promotion] %p: collector=mmc, should move: %d"
        ", live bytes = %zu, wasted bytes = %zu, promotion threshold = %zu"
        ", allocated labs size = %zu\n",
        p, should_move_page, live_bytes, wasted_bytes,
        NewSpacePageEvacuationThreshold(), p->AllocatedLabSize());
  }
  if (!should_move_page &&
      (p->AgeInNewSpace() == v8_flags.minor_ms_max_page_age)) {
    // Don't allocate on old pages so that recently allocated objects on the
    // page get a chance to die young. The page will be force promoted on the
    // next GC because `AllocatedLabSize` will be 0.
    p->SetFlag(Page::NEVER_ALLOCATE_ON_PAGE);
  }
  return should_move_page;
}

}  // namespace

bool MinorMarkSweepCollector::StartSweepNewSpace() {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_SWEEP_NEW);
  PagedSpaceForNewSpace* paged_space = heap_->paged_new_space()->paged_space();
  paged_space->ClearAllocatorState();

  int will_be_swept = 0;
  bool has_promoted_pages = false;

  DCHECK_EQ(Heap::ResizeNewSpaceMode::kNone, resize_new_space_);
  resize_new_space_ = heap_->ShouldResizeNewSpace();
  if (resize_new_space_ == Heap::ResizeNewSpaceMode::kShrink) {
    paged_space->StartShrinking();
  }

  for (auto it = paged_space->begin(); it != paged_space->end();) {
    Page* p = *(it++);
    DCHECK(p->SweepingDone());

    intptr_t live_bytes_on_page = p->live_bytes();
    if (live_bytes_on_page == 0) {
      if (paged_space->ShouldReleaseEmptyPage()) {
        paged_space->ReleasePage(p);
      } else {
        sweeper()->SweepEmptyNewSpacePage(p);
      }
      continue;
    }

    if (ShouldMovePage(p, live_bytes_on_page, p->wasted_memory())) {
      heap_->new_space()->PromotePageToOldSpace(p);
      has_promoted_pages = true;
      sweeper()->AddPromotedPage(p);
    } else {
      // Page is not promoted. Sweep it instead.
      sweeper()->AddNewSpacePage(p);
      will_be_swept++;
    }
  }

  if (v8_flags.gc_verbose) {
    PrintIsolate(heap_->isolate(),
                 "sweeping: space=%s initialized_for_sweeping=%d",
                 ToString(paged_space->identity()), will_be_swept);
  }

  return has_promoted_pages;
}

bool MinorMarkSweepCollector::SweepNewLargeSpace() {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MINOR_MS_SWEEP_NEW_LO);
  NewLargeObjectSpace* new_lo_space = heap_->new_lo_space();
  DCHECK_NOT_NULL(new_lo_space);
  DCHECK_EQ(kNullAddress, heap_->new_lo_space()->pending_object());

  bool has_promoted_pages = false;

  OldLargeObjectSpace* old_lo_space = heap_->lo_space();

  for (auto it = new_lo_space->begin(); it != new_lo_space->end();) {
    LargePage* current = *it;
    it++;
    HeapObject object = current->GetObject();
    if (!non_atomic_marking_state_->IsMarked(object)) {
      // Object is dead and page can be released.
      new_lo_space->RemovePage(current);
      heap_->memory_allocator()->Free(MemoryAllocator::FreeMode::kConcurrently,
                                      current);
      continue;
    }
    current->ClearFlag(MemoryChunk::TO_PAGE);
    current->SetFlag(MemoryChunk::FROM_PAGE);
    current->ProgressBar().ResetIfEnabled();
    old_lo_space->PromoteNewLargeObject(current);
    has_promoted_pages = true;
    sweeper()->AddPromotedPage(current);
  }
  new_lo_space->set_objects_size(0);

  return has_promoted_pages;
}

void MinorMarkSweepCollector::Sweep() {
  DCHECK(!sweeper()->AreMinorSweeperTasksRunning());
  sweeper_->InitializeMinorSweeping();

  TRACE_GC_WITH_FLOW(
      heap_->tracer(), GCTracer::Scope::MINOR_MS_SWEEP,
      sweeper_->GetTraceIdForFlowEvent(GCTracer::Scope::MINOR_MS_SWEEP),
      TRACE_EVENT_FLAG_FLOW_OUT);

  bool has_promoted_pages = false;
  if (StartSweepNewSpace()) has_promoted_pages = true;
  if (SweepNewLargeSpace()) has_promoted_pages = true;

  if (v8_flags.verify_heap && has_promoted_pages) {
    // Update the external string table in preparation for heap verification.
    // Otherwise, updating the table will happen during the next full GC.
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MINOR_MS_SWEEP_UPDATE_STRING_TABLE);
    heap_->UpdateYoungReferencesInExternalStringTable(
        [](Heap* heap, FullObjectSlot p) {
          DCHECK(!Tagged<HeapObject>::cast(*p)
                      ->map_word(kRelaxedLoad)
                      .IsForwardingAddress());
          return Tagged<String>::cast(*p);
        });
  }

  sweeper_->StartMinorSweeping();

#ifdef DEBUG
  VerifyRememberedSetsAfterEvacuation(heap_,
                                      GarbageCollector::MINOR_MARK_SWEEPER);
  heap_->VerifyCountersBeforeConcurrentSweeping(
      GarbageCollector::MINOR_MARK_SWEEPER);
#endif

  sweeper()->StartMinorSweeperTasks();
  DCHECK_EQ(0, heap_->new_lo_space()->Size());
  heap_->array_buffer_sweeper()->RequestSweep(
      ArrayBufferSweeper::SweepingType::kYoung,
      (heap_->new_space()->Size() == 0)
          ? ArrayBufferSweeper::TreatAllYoungAsPromoted::kYes
          : ArrayBufferSweeper::TreatAllYoungAsPromoted::kNo);
}

}  // namespace internal
}  // namespace v8
