// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/read-only-promotion.h"

#include <unordered_set>

#include "src/common/assert-scope.h"
#include "src/execution/isolate.h"
#include "src/heap/combined-heap.h"
#include "src/heap/heap.h"
#include "src/objects/heap-object-inl.h"

namespace v8 {
namespace internal {
namespace {

// Convenience aliases:
using HeapObjectSet =
    std::unordered_set<HeapObject, Object::Hasher, Object::KeyEqualSafe>;
using HeapObjectMap = std::unordered_map<HeapObject, HeapObject, Object::Hasher,
                                         Object::KeyEqualSafe>;
bool Contains(const HeapObjectSet& s, HeapObject o) { return s.count(o) != 0; }

class Committee final {
 public:
  static std::vector<HeapObject> DeterminePromotees(
      Isolate* isolate, const DisallowGarbageCollection& no_gc,
      const SafepointScope& safepoint_scope) {
    return Committee(isolate).DeterminePromotees(safepoint_scope);
  }

 private:
  explicit Committee(Isolate* isolate) : isolate_(isolate) {}

  std::vector<HeapObject> DeterminePromotees(
      const SafepointScope& safepoint_scope) {
    DCHECK(promo_accepted_.empty());
    DCHECK(promo_rejected_.empty());

    HeapObjectIterator it(
        isolate_->heap(), safepoint_scope,
        HeapObjectIterator::HeapObjectsFiltering::kFilterUnreachable);
    for (HeapObject o = it.Next(); !o.is_null(); o = it.Next()) {
      DCHECK(!o.InReadOnlySpace());

      // Note that cycles prevent us from promoting/rejecting each subgraph as
      // we visit it, since locally we cannot determine whether the deferred
      // decision on the 'cycle object' will be 'promote' or 'reject'. This
      // could be solved if necessary (with more complex code), but for now
      // there are no performance issues.
      HeapObjectSet accepted_subgraph;  // Either all are accepted or none.
      HeapObjectSet visited;            // Cycle detection.
      if (!EvaluateSubgraph(o, &accepted_subgraph, &visited)) continue;

      if (V8_UNLIKELY(v8_flags.trace_read_only_promotion)) {
        LogAcceptedPromotionSet(accepted_subgraph);
      }
      promo_accepted_.insert(accepted_subgraph.begin(),
                             accepted_subgraph.end());
    }

    // Return promotees as a sorted list. Note that sorting uses object
    // addresses; the list order is deterministic only if heap layout
    // itself is deterministic (see v8_flags.predictable).
    std::vector<HeapObject> promotees{promo_accepted_.begin(),
                                      promo_accepted_.end()};
    std::sort(promotees.begin(), promotees.end(), Object::Comparer());

    return promotees;
  }

  // Returns `false` if the subgraph rooted at `o` is rejected.
  // Returns `true` if it is accepted, or if we've reached a cycle and `o`
  // will be processed further up the callchain.
  bool EvaluateSubgraph(HeapObject o, HeapObjectSet* accepted_subgraph,
                        HeapObjectSet* visited) {
    if (o.InReadOnlySpace()) return true;
    if (Contains(promo_rejected_, o)) return false;
    if (Contains(promo_accepted_, o)) return true;
    if (Contains(*visited, o)) return true;
    visited->insert(o);
    if (!IsPromoCandidate(isolate_, o)) {
      const auto& [it, inserted] = promo_rejected_.insert(o);
      if (V8_UNLIKELY(v8_flags.trace_read_only_promotion) && inserted) {
        LogRejectedPromotionForFailedPredicate(o);
      }
      return false;
    }
    // Recurse into outgoing pointers.
    CandidateVisitor v(this, accepted_subgraph, visited);
    o.Iterate(isolate_, &v);
    if (!v.all_slots_are_promo_candidates()) {
      const auto& [it, inserted] = promo_rejected_.insert(o);
      if (V8_UNLIKELY(v8_flags.trace_read_only_promotion) && inserted) {
        LogRejectedPromotionForInvalidSubgraph(o,
                                               v.first_rejected_slot_offset());
      }
      return false;
    }

    accepted_subgraph->insert(o);
    return true;
  }

#define PROMO_CANDIDATE_TYPE_LIST(V) \
  V(AccessorInfo)                    \
  V(CallHandlerInfo)                 \
  V(Code)                            \
  V(FunctionTemplateInfo)            \
  V(FunctionTemplateRareData)        \
  V(ScopeInfo)                       \
  V(SharedFunctionInfo)
  // TODO(jgruber): Don't forget to extend ReadOnlyPromotionImpl::Verify when
  // adding new object types here.

  static bool IsPromoCandidate(Isolate* isolate, HeapObject o) {
    const InstanceType itype = o.map(isolate).instance_type();
#define V(TYPE)                                            \
  if (InstanceTypeChecker::Is##TYPE(itype)) {              \
    return IsPromoCandidate##TYPE(isolate, TYPE::cast(o)); \
    /* NOLINTNEXTLINE(readability/braces) */               \
  } else
    PROMO_CANDIDATE_TYPE_LIST(V)
    /* if { ... } else */ {
      return false;
    }
#undef V
    UNREACHABLE();
  }
#undef PROMO_CANDIDATE_TYPE_LIST

  // TODO(jgruber): The ExternalPointerTable doesn't support host objects in RO
  // space yet. Design and implement support.
  static constexpr bool kSupportsReadOnlyExternalPointers =
      !V8_ENABLE_SANDBOX_BOOL;
  static bool IsPromoCandidateAccessorInfo(Isolate* isolate, AccessorInfo o) {
    return kSupportsReadOnlyExternalPointers;
  }
  static bool IsPromoCandidateCallHandlerInfo(Isolate* isolate,
                                              CallHandlerInfo o) {
    return kSupportsReadOnlyExternalPointers;
  }
  static bool IsPromoCandidateCode(Isolate* isolate, Code o) {
#if !defined(V8_SHORT_BUILTIN_CALLS) || \
    defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)
    // Builtins have a single unique shared entry point per process. The
    // embedded builtins region may be remapped into the process-wide code
    // range, but that happens before RO space is deserialized. Their Code
    // objects can be shared in RO space.
    static_assert(Builtins::kCodeObjectsAreInROSpace);
    return o.is_builtin();
#else
    // Builtins may be remapped more than once per process and thus their Code
    // objects cannot be shared.
    static_assert(!Builtins::kCodeObjectsAreInROSpace);
    return false;
#endif
  }
  static bool IsPromoCandidateFunctionTemplateInfo(Isolate* isolate,
                                                   FunctionTemplateInfo o) {
    // TODO(jgruber): Enable once we have a solution for the mutable
    // shared_function_info field.
    return false;
  }
  static bool IsPromoCandidateFunctionTemplateRareData(
      Isolate* isolate, FunctionTemplateRareData o) {
    return true;
  }
  static bool IsPromoCandidateScopeInfo(Isolate* isolate, ScopeInfo o) {
    return true;
  }
  static bool IsPromoCandidateSharedFunctionInfo(Isolate* isolate,
                                                 SharedFunctionInfo o) {
    // Only internal builtin SFIs are guaranteed to remain immutable.
    if (o.has_script(kAcquireLoad)) return false;
    // kIllegal is used for js_global_object_function, which is created during
    // bootstrapping but never rooted. We currently assumed that all objects in
    // the snapshot are live. But RO space is 1) not GC'd and 2) serialized
    // verbatim, preserving dead objects. As a workaround, exclude this builtin
    // id from RO allocation.
    // TODO(jgruber): A better solution. Remove the liveness assumption (see
    // test-heap-profiler.cc)? Overwrite dead RO objects with fillers
    // pre-serialization? Implement a RO GC pass pre-serialization?
    return o.HasBuiltinId() && o.builtin_id() != Builtin::kIllegal;
  }

  // Recurses into all tagged slots of an object and tracks whether predicates
  // failed on any part of the subgraph.
  class CandidateVisitor : public ObjectVisitor {
   public:
    CandidateVisitor(Committee* committee, HeapObjectSet* accepted_subgraph,
                     HeapObjectSet* visited)
        : committee_(committee),
          accepted_subgraph_(accepted_subgraph),
          visited_(visited) {}

    int first_rejected_slot_offset() const {
      return first_rejected_slot_offset_;
    }
    bool all_slots_are_promo_candidates() const {
      return first_rejected_slot_offset_ == -1;
    }

    void VisitPointers(HeapObject host, MaybeObjectSlot start,
                       MaybeObjectSlot end) final {
      if (!all_slots_are_promo_candidates()) return;
      for (MaybeObjectSlot slot = start; slot < end; slot++) {
        MaybeObject maybe_object = slot.load(committee_->isolate_);
        HeapObject heap_object;
        if (!maybe_object.GetHeapObject(&heap_object)) continue;
        if (!committee_->EvaluateSubgraph(heap_object, accepted_subgraph_,
                                          visited_)) {
          first_rejected_slot_offset_ =
              static_cast<int>(slot.address() - host.address());
          DCHECK_GE(first_rejected_slot_offset_, 0);
          return;
        }
      }
    }
    void VisitPointers(HeapObject host, ObjectSlot start,
                       ObjectSlot end) final {
      VisitPointers(host, MaybeObjectSlot(start), MaybeObjectSlot(end));
    }
    void VisitInstructionStreamPointer(Code host,
                                       InstructionStreamSlot slot) final {
      DCHECK(host.is_builtin());
    }
    void VisitMapPointer(HeapObject host) final {
      MaybeObjectSlot slot = host.RawMaybeWeakField(HeapObject::kMapOffset);
      VisitPointers(host, slot, slot + 1);
    }

   private:
    Committee* const committee_;
    HeapObjectSet* const accepted_subgraph_;
    HeapObjectSet* const visited_;
    int first_rejected_slot_offset_ = -1;
  };

  static void LogAcceptedPromotionSet(const HeapObjectSet& os) {
    std::cout << "ro-promotion: accepted set {";
    for (HeapObject o : os) {
      std::cout << reinterpret_cast<void*>(o.ptr()) << ", ";
    }
    std::cout << "}\n";
  }

  static void LogRejectedPromotionForFailedPredicate(HeapObject o) {
    std::cout << "ro-promotion: rejected due to failed predicate "
              << reinterpret_cast<void*>(o.ptr()) << "\n";
  }

  static void LogRejectedPromotionForInvalidSubgraph(
      HeapObject o, int first_rejected_slot_offset) {
    std::cout << "ro-promotion: rejected due to rejected subgraph "
              << reinterpret_cast<void*>(o.ptr()) << " at slot offset "
              << first_rejected_slot_offset << "\n";
  }

  Isolate* const isolate_;
  HeapObjectSet promo_accepted_;
  HeapObjectSet promo_rejected_;
};

class ReadOnlyPromotionImpl final : public AllStatic {
 public:
  static void CopyToReadOnlyHeap(Isolate* isolate,
                                 const std::vector<HeapObject>& promotees,
                                 HeapObjectMap* moves) {
    ReadOnlySpace* rospace = isolate->heap()->read_only_space();
    for (HeapObject src : promotees) {
      const int size = src.Size(isolate);
      Tagged<HeapObject> dst =
          rospace->AllocateRaw(size, kTaggedAligned).ToObjectChecked();
      Heap::CopyBlock(dst.address(), src.address(), size);
      moves->emplace(src, dst);
    }
  }

  static void UpdatePointers(Isolate* isolate,
                             const SafepointScope& safepoint_scope,
                             const HeapObjectMap& moves) {
    Heap* heap = isolate->heap();
    UpdatePointersVisitor v(isolate, &moves);

    // Iterate all roots.
    heap->IterateRoots(&v, base::EnumSet<SkipRoot>{
                               SkipRoot::kUnserializable,
                               SkipRoot::kWeak,
                           });

    // Iterate all objects on the mutable heap.
    HeapObjectIterator it(
        heap, safepoint_scope,
        HeapObjectIterator::HeapObjectsFiltering::kFilterUnreachable);
    for (HeapObject o = it.Next(); !o.is_null(); o = it.Next()) {
      o.Iterate(isolate, &v);
    }

    // Iterate all objects we just copied into RO space.
    for (auto [src, dst] : moves) {
      dst.Iterate(isolate, &v);
    }
  }

  static void Verify(Isolate* isolate, const SafepointScope& safepoint_scope) {
#ifdef DEBUG
    // Verify that certain objects were promoted as expected.
    //
    // Known objects.
    Heap* heap = isolate->heap();
    CHECK(heap->promise_all_resolve_element_shared_fun().InReadOnlySpace());
    // TODO(jgruber): Extend here with more objects as they are added to
    // the promotion algorithm.

    // Builtin Code objects.
    if (Builtins::kCodeObjectsAreInROSpace) {
      Builtins* builtins = isolate->builtins();
      for (int i = 0; i < Builtins::kBuiltinCount; i++) {
        CHECK(builtins->code(static_cast<Builtin>(i)).InReadOnlySpace());
      }
    }
#endif  // DEBUG
  }

 private:
  class UpdatePointersVisitor final : public ObjectVisitor, public RootVisitor {
   public:
    UpdatePointersVisitor(Isolate* isolate, const HeapObjectMap* moves)
        : isolate_(isolate), moves_(moves) {}

    // The RootVisitor interface.
    void VisitRootPointers(Root root, const char* description,
                           FullObjectSlot start, FullObjectSlot end) final {
      for (FullObjectSlot slot = start; slot < end; slot++) {
        ProcessSlot(root, slot);
      }
    }

    // The ObjectVisitor interface.
    void VisitPointers(HeapObject host, MaybeObjectSlot start,
                       MaybeObjectSlot end) final {
      for (MaybeObjectSlot slot = start; slot < end; slot++) {
        ProcessSlot(host, slot);
      }
    }
    void VisitPointers(HeapObject host, ObjectSlot start,
                       ObjectSlot end) final {
      VisitPointers(host, MaybeObjectSlot(start), MaybeObjectSlot(end));
    }
    void VisitInstructionStreamPointer(Code host,
                                       InstructionStreamSlot slot) final {
      // InstructionStream objects never move to RO space.
    }
    void VisitMapPointer(HeapObject host) final {
      ProcessSlot(host, host.RawMaybeWeakField(HeapObject::kMapOffset));
    }
    void VisitIndirectPointer(HeapObject host, IndirectPointerSlot slot,
                              IndirectPointerMode mode) final {}
    void VisitIndirectPointerTableEntry(HeapObject host,
                                        IndirectPointerSlot slot) final {
#ifdef V8_CODE_POINTER_SANDBOXING
      // When an object owning an indirect pointer table entry is relocated, it
      // needs to update the entry to point to its new location. Currently, only
      // Code objects are referenced through indirect pointers, and they use the
      // code pointer table.
      CHECK(IsCode(host));

      // Due to the way we handle baseline code during serialization, we may
      // encounter such Code objects during iteration. Do a lookup through
      // moves_ to make sure we only update CPT entries for moved objects.
      IndirectPointerHandle handle = slot.Relaxed_LoadHandle();
      CodePointerTable* cpt = GetProcessWideCodePointerTable();
      Code maybe_dead_code(Code::cast(Object(cpt->GetCodeObject(handle))));
      auto it = moves_->find(maybe_dead_code);
      if (it == moves_->end()) return;
      DCHECK_EQ(it->second, host);

      // The old Code object (in mutable space) is dead. To preserve the 1:1
      // relation between Code objects and CPT entries, overwrite it immediately
      // with the filler object.
      Code dead_code = maybe_dead_code;
      CHECK(IsCode(dead_code));
      CHECK(host.InReadOnlySpace());
      CHECK(!dead_code.InReadOnlySpace());
      isolate_->heap()->CreateFillerObjectAt(dead_code.address(), Code::kSize);

      // Update the CPT entry to point at the moved RO Code object.
      cpt->SetCodeObject(handle, host.ptr());
#else
      UNREACHABLE();
#endif
    }

   private:
    void ProcessSlot(Root root, FullObjectSlot slot) {
      Object old_slot_value_obj = slot.load(isolate_);
      if (!IsHeapObject(old_slot_value_obj)) return;
      HeapObject old_slot_value = HeapObject::cast(old_slot_value_obj);
      auto it = moves_->find(old_slot_value);
      if (it == moves_->end()) return;
      HeapObject new_slot_value = it->second;
      slot.store(new_slot_value);
      if (V8_UNLIKELY(v8_flags.trace_read_only_promotion_verbose)) {
        LogUpdatedPointer(root, slot, old_slot_value, new_slot_value);
      }
    }
    void ProcessSlot(HeapObject host, MaybeObjectSlot slot) {
      HeapObject old_slot_value;
      if (!slot.load(isolate_).GetHeapObject(&old_slot_value)) return;
      auto it = moves_->find(old_slot_value);
      if (it == moves_->end()) return;
      HeapObject new_slot_value = it->second;
      slot.store(MaybeObject::FromObject(new_slot_value));
      if (V8_UNLIKELY(v8_flags.trace_read_only_promotion_verbose)) {
        LogUpdatedPointer(host, slot, old_slot_value, new_slot_value);
      }
    }

    void LogUpdatedPointer(Root root, FullObjectSlot slot,
                           HeapObject old_slot_value,
                           HeapObject new_slot_value) {
      std::cout << "ro-promotion: updated pointer {root "
                << static_cast<int>(root) << " slot "
                << reinterpret_cast<void*>(slot.address()) << " from "
                << reinterpret_cast<void*>(old_slot_value.ptr()) << " to "
                << reinterpret_cast<void*>(new_slot_value.ptr()) << "}\n";
    }
    void LogUpdatedPointer(HeapObject host, MaybeObjectSlot slot,
                           HeapObject old_slot_value,
                           HeapObject new_slot_value) {
      std::cout << "ro-promotion: updated pointer {host "
                << reinterpret_cast<void*>(host.address()) << " slot "
                << reinterpret_cast<void*>(slot.address()) << " from "
                << reinterpret_cast<void*>(old_slot_value.ptr()) << " to "
                << reinterpret_cast<void*>(new_slot_value.ptr()) << "}\n";
    }

    Isolate* const isolate_;
    const HeapObjectMap* moves_;
  };
};

}  // namespace

// static
void ReadOnlyPromotion::Promote(Isolate* isolate,
                                const SafepointScope& safepoint_scope) {
  DisallowGarbageCollection no_gc;
  // Visit the mutable heap and determine the set of objects that can be
  // promoted to RO space.
  std::vector<HeapObject> promotees =
      Committee::DeterminePromotees(isolate, no_gc, safepoint_scope);
  // Physically copy promotee objects to RO space and track all object moves.
  HeapObjectMap moves;
  ReadOnlyPromotionImpl::CopyToReadOnlyHeap(isolate, promotees, &moves);
  // Update all references to moved objects to point at their new location in
  // RO space.
  ReadOnlyPromotionImpl::UpdatePointers(isolate, safepoint_scope, moves);
  ReadOnlyPromotionImpl::Verify(isolate, safepoint_scope);
}

}  // namespace internal
}  // namespace v8
