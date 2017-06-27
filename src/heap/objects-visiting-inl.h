// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_VISITING_INL_H_
#define V8_OBJECTS_VISITING_INL_H_

#include "src/heap/array-buffer-tracker.h"
#include "src/heap/mark-compact.h"
#include "src/heap/objects-visiting.h"
#include "src/ic/ic-state.h"
#include "src/macro-assembler.h"
#include "src/objects-body-descriptors-inl.h"

namespace v8 {
namespace internal {

VisitorId StaticVisitorBase::GetVisitorId(Map* map) {
  return GetVisitorId(map->instance_type(), map->instance_size(),
                      FLAG_unbox_double_fields && !map->HasFastPointerLayout());
}

VisitorId StaticVisitorBase::GetVisitorId(int instance_type, int instance_size,
                                          bool has_unboxed_fields) {
  if (instance_type < FIRST_NONSTRING_TYPE) {
    switch (instance_type & kStringRepresentationMask) {
      case kSeqStringTag:
        if ((instance_type & kStringEncodingMask) == kOneByteStringTag) {
          return kVisitSeqOneByteString;
        } else {
          return kVisitSeqTwoByteString;
        }

      case kConsStringTag:
        if (IsShortcutCandidate(instance_type)) {
          return kVisitShortcutCandidate;
        } else {
          return kVisitConsString;
        }

      case kSlicedStringTag:
        return kVisitSlicedString;

      case kExternalStringTag:
        return kVisitDataObject;

      case kThinStringTag:
        return kVisitThinString;
    }
    UNREACHABLE();
  }

  switch (instance_type) {
    case BYTE_ARRAY_TYPE:
      return kVisitByteArray;

    case BYTECODE_ARRAY_TYPE:
      return kVisitBytecodeArray;

    case FREE_SPACE_TYPE:
      return kVisitFreeSpace;

    case FIXED_ARRAY_TYPE:
      return kVisitFixedArray;

    case FIXED_DOUBLE_ARRAY_TYPE:
      return kVisitFixedDoubleArray;

    case ODDBALL_TYPE:
      return kVisitOddball;

    case MAP_TYPE:
      return kVisitMap;

    case CODE_TYPE:
      return kVisitCode;

    case CELL_TYPE:
      return kVisitCell;

    case PROPERTY_CELL_TYPE:
      return kVisitPropertyCell;

    case WEAK_CELL_TYPE:
      return kVisitWeakCell;

    case TRANSITION_ARRAY_TYPE:
      return kVisitTransitionArray;

    case JS_WEAK_MAP_TYPE:
    case JS_WEAK_SET_TYPE:
      return kVisitJSWeakCollection;

    case JS_REGEXP_TYPE:
      return kVisitJSRegExp;

    case SHARED_FUNCTION_INFO_TYPE:
      return kVisitSharedFunctionInfo;

    case JS_PROXY_TYPE:
      return kVisitStruct;

    case SYMBOL_TYPE:
      return kVisitSymbol;

    case JS_ARRAY_BUFFER_TYPE:
      return kVisitJSArrayBuffer;

    case SMALL_ORDERED_HASH_MAP_TYPE:
      return kVisitSmallOrderedHashMap;

    case SMALL_ORDERED_HASH_SET_TYPE:
      return kVisitSmallOrderedHashSet;

    case JS_OBJECT_TYPE:
    case JS_ERROR_TYPE:
    case JS_ARGUMENTS_TYPE:
    case JS_ASYNC_FROM_SYNC_ITERATOR_TYPE:
    case JS_CONTEXT_EXTENSION_OBJECT_TYPE:
    case JS_GENERATOR_OBJECT_TYPE:
    case JS_ASYNC_GENERATOR_OBJECT_TYPE:
    case JS_MODULE_NAMESPACE_TYPE:
    case JS_VALUE_TYPE:
    case JS_DATE_TYPE:
    case JS_ARRAY_TYPE:
    case JS_GLOBAL_PROXY_TYPE:
    case JS_GLOBAL_OBJECT_TYPE:
    case JS_MESSAGE_OBJECT_TYPE:
    case JS_TYPED_ARRAY_TYPE:
    case JS_DATA_VIEW_TYPE:
    case JS_SET_TYPE:
    case JS_MAP_TYPE:
    case JS_SET_ITERATOR_TYPE:
    case JS_MAP_ITERATOR_TYPE:
    case JS_STRING_ITERATOR_TYPE:

    case JS_TYPED_ARRAY_KEY_ITERATOR_TYPE:
    case JS_FAST_ARRAY_KEY_ITERATOR_TYPE:
    case JS_GENERIC_ARRAY_KEY_ITERATOR_TYPE:
    case JS_UINT8_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_INT8_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_UINT16_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_INT16_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_UINT32_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_INT32_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_FLOAT32_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_FLOAT64_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_UINT8_CLAMPED_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_FAST_SMI_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_FAST_HOLEY_SMI_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_FAST_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_FAST_HOLEY_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_FAST_DOUBLE_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_FAST_HOLEY_DOUBLE_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_GENERIC_ARRAY_KEY_VALUE_ITERATOR_TYPE:
    case JS_UINT8_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_INT8_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_UINT16_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_INT16_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_UINT32_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_INT32_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_FLOAT32_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_FLOAT64_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_UINT8_CLAMPED_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_FAST_SMI_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_FAST_HOLEY_SMI_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_FAST_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_FAST_HOLEY_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_FAST_DOUBLE_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_FAST_HOLEY_DOUBLE_ARRAY_VALUE_ITERATOR_TYPE:
    case JS_GENERIC_ARRAY_VALUE_ITERATOR_TYPE:

    case JS_PROMISE_CAPABILITY_TYPE:
    case JS_PROMISE_TYPE:
    case JS_BOUND_FUNCTION_TYPE:
      return has_unboxed_fields ? kVisitJSObject : kVisitJSObjectFast;
    case JS_API_OBJECT_TYPE:
    case JS_SPECIAL_API_OBJECT_TYPE:
      return kVisitJSApiObject;

    case JS_FUNCTION_TYPE:
      return kVisitJSFunction;

    case FILLER_TYPE:
    case FOREIGN_TYPE:
    case HEAP_NUMBER_TYPE:
    case MUTABLE_HEAP_NUMBER_TYPE:
      return kVisitDataObject;

    case FIXED_UINT8_ARRAY_TYPE:
    case FIXED_INT8_ARRAY_TYPE:
    case FIXED_UINT16_ARRAY_TYPE:
    case FIXED_INT16_ARRAY_TYPE:
    case FIXED_UINT32_ARRAY_TYPE:
    case FIXED_INT32_ARRAY_TYPE:
    case FIXED_FLOAT32_ARRAY_TYPE:
    case FIXED_UINT8_CLAMPED_ARRAY_TYPE:
      return kVisitFixedTypedArrayBase;

    case FIXED_FLOAT64_ARRAY_TYPE:
      return kVisitFixedFloat64Array;

#define MAKE_STRUCT_CASE(NAME, Name, name) case NAME##_TYPE:
      STRUCT_LIST(MAKE_STRUCT_CASE)
#undef MAKE_STRUCT_CASE
      if (instance_type == ALLOCATION_SITE_TYPE) {
        return kVisitAllocationSite;
      }

      return kVisitStruct;

    default:
      UNREACHABLE();
  }
}

template <typename Callback>
Callback VisitorDispatchTable<Callback>::GetVisitor(Map* map) {
  return reinterpret_cast<Callback>(callbacks_[map->visitor_id()]);
}

template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::Initialize() {
  table_.Register(kVisitShortcutCandidate,
                  &FixedBodyVisitor<StaticVisitor, ConsString::BodyDescriptor,
                                    void>::Visit);

  table_.Register(kVisitConsString,
                  &FixedBodyVisitor<StaticVisitor, ConsString::BodyDescriptor,
                                    void>::Visit);

  table_.Register(kVisitThinString,
                  &FixedBodyVisitor<StaticVisitor, ThinString::BodyDescriptor,
                                    void>::Visit);

  table_.Register(kVisitSlicedString,
                  &FixedBodyVisitor<StaticVisitor, SlicedString::BodyDescriptor,
                                    void>::Visit);

  table_.Register(
      kVisitSymbol,
      &FixedBodyVisitor<StaticVisitor, Symbol::BodyDescriptor, void>::Visit);

  table_.Register(kVisitFixedArray, &FixedArrayVisitor::Visit);

  table_.Register(kVisitFixedDoubleArray, &DataObjectVisitor::Visit);

  table_.Register(
      kVisitFixedTypedArrayBase,
      &FlexibleBodyVisitor<StaticVisitor, FixedTypedArrayBase::BodyDescriptor,
                           void>::Visit);

  table_.Register(
      kVisitFixedFloat64Array,
      &FlexibleBodyVisitor<StaticVisitor, FixedTypedArrayBase::BodyDescriptor,
                           void>::Visit);

  table_.Register(kVisitNativeContext, &VisitNativeContext);

  table_.Register(
      kVisitAllocationSite,
      &FixedBodyVisitor<StaticVisitor, AllocationSite::BodyDescriptorWeak,
                        void>::Visit);

  table_.Register(kVisitByteArray, &DataObjectVisitor::Visit);

  table_.Register(kVisitBytecodeArray, &VisitBytecodeArray);

  table_.Register(kVisitFreeSpace, &DataObjectVisitor::Visit);

  table_.Register(kVisitSeqOneByteString, &DataObjectVisitor::Visit);

  table_.Register(kVisitSeqTwoByteString, &DataObjectVisitor::Visit);

  table_.Register(kVisitJSWeakCollection, &VisitWeakCollection);

  table_.Register(
      kVisitOddball,
      &FixedBodyVisitor<StaticVisitor, Oddball::BodyDescriptor, void>::Visit);

  table_.Register(kVisitMap, &VisitMap);

  table_.Register(kVisitCode, &VisitCode);

  table_.Register(kVisitSharedFunctionInfo, &VisitSharedFunctionInfo);

  table_.Register(kVisitJSFunction, &VisitJSFunction);

  table_.Register(
      kVisitJSArrayBuffer,
      &FlexibleBodyVisitor<StaticVisitor, JSArrayBuffer::BodyDescriptor,
                           void>::Visit);

  table_.Register(kVisitJSRegExp, &JSObjectVisitor::Visit);

  table_.Register(
      kVisitCell,
      &FixedBodyVisitor<StaticVisitor, Cell::BodyDescriptor, void>::Visit);

  table_.Register(kVisitPropertyCell,
                  &FixedBodyVisitor<StaticVisitor, PropertyCell::BodyDescriptor,
                                    void>::Visit);

  table_.Register(
      kVisitSmallOrderedHashMap,
      &FlexibleBodyVisitor<
          StaticVisitor,
          SmallOrderedHashTable<SmallOrderedHashMap>::BodyDescriptor,
          void>::Visit);

  table_.Register(
      kVisitSmallOrderedHashSet,
      &FlexibleBodyVisitor<
          StaticVisitor,
          SmallOrderedHashTable<SmallOrderedHashSet>::BodyDescriptor,
          void>::Visit);

  table_.Register(kVisitWeakCell, &VisitWeakCell);

  table_.Register(kVisitTransitionArray, &VisitTransitionArray);

  table_.Register(kVisitDataObject, &DataObjectVisitor::Visit);

  table_.Register(kVisitJSObjectFast, &JSObjectFastVisitor::Visit);
  table_.Register(kVisitJSObject, &JSObjectVisitor::Visit);

  table_.Register(kVisitJSApiObject, &JSApiObjectVisitor::Visit);

  table_.Register(kVisitStruct, &StructObjectVisitor::Visit);
}


template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitCodeEntry(
    Heap* heap, HeapObject* object, Address entry_address) {
  Code* code = Code::cast(Code::GetObjectFromEntryAddress(entry_address));
  heap->mark_compact_collector()->RecordCodeEntrySlot(object, entry_address,
                                                      code);
  StaticVisitor::MarkObject(heap, code);
}


template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitEmbeddedPointer(
    Heap* heap, RelocInfo* rinfo) {
  DCHECK(rinfo->rmode() == RelocInfo::EMBEDDED_OBJECT);
  HeapObject* object = HeapObject::cast(rinfo->target_object());
  Code* host = rinfo->host();
  heap->mark_compact_collector()->RecordRelocSlot(host, rinfo, object);
  // TODO(ulan): It could be better to record slots only for strongly embedded
  // objects here and record slots for weakly embedded object during clearing
  // of non-live references in mark-compact.
  if (!host->IsWeakObject(object)) {
    StaticVisitor::MarkObject(heap, object);
  }
}


template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitCell(Heap* heap,
                                                    RelocInfo* rinfo) {
  DCHECK(rinfo->rmode() == RelocInfo::CELL);
  Cell* cell = rinfo->target_cell();
  Code* host = rinfo->host();
  heap->mark_compact_collector()->RecordRelocSlot(host, rinfo, cell);
  if (!host->IsWeakObject(cell)) {
    StaticVisitor::MarkObject(heap, cell);
  }
}


template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitDebugTarget(Heap* heap,
                                                           RelocInfo* rinfo) {
  DCHECK(RelocInfo::IsDebugBreakSlot(rinfo->rmode()) &&
         rinfo->IsPatchedDebugBreakSlotSequence());
  Code* target = Code::GetCodeFromTargetAddress(rinfo->debug_call_address());
  Code* host = rinfo->host();
  heap->mark_compact_collector()->RecordRelocSlot(host, rinfo, target);
  StaticVisitor::MarkObject(heap, target);
}


template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitCodeTarget(Heap* heap,
                                                          RelocInfo* rinfo) {
  DCHECK(RelocInfo::IsCodeTarget(rinfo->rmode()));
  Code* target = Code::GetCodeFromTargetAddress(rinfo->target_address());
  Code* host = rinfo->host();
  heap->mark_compact_collector()->RecordRelocSlot(host, rinfo, target);
  StaticVisitor::MarkObject(heap, target);
}

template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitCodeAgeSequence(
    Heap* heap, RelocInfo* rinfo) {
  DCHECK(RelocInfo::IsCodeAgeSequence(rinfo->rmode()));
  Code* target = rinfo->code_age_stub();
  DCHECK(target != NULL);
  Code* host = rinfo->host();
  heap->mark_compact_collector()->RecordRelocSlot(host, rinfo, target);
  StaticVisitor::MarkObject(heap, target);
}

template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitBytecodeArray(
    Map* map, HeapObject* object) {
  FlexibleBodyVisitor<StaticVisitor, BytecodeArray::BodyDescriptor,
                      void>::Visit(map, object);
  BytecodeArray::cast(object)->MakeOlder();
}

template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitNativeContext(
    Map* map, HeapObject* object) {
  FixedBodyVisitor<StaticVisitor, Context::BodyDescriptorWeak, void>::Visit(
      map, object);
}


template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitMap(Map* map,
                                                   HeapObject* object) {
  Heap* heap = map->GetHeap();
  Map* map_object = Map::cast(object);

  // Clears the cache of ICs related to this map.
  if (FLAG_cleanup_code_caches_at_gc) {
    map_object->ClearCodeCache(heap);
  }

  // When map collection is enabled we have to mark through map's transitions
  // and back pointers in a special way to make these links weak.
  if (map_object->CanTransition()) {
    MarkMapContents(heap, map_object);
  } else {
    StaticVisitor::VisitPointers(
        heap, object,
        HeapObject::RawField(object, Map::kPointerFieldsBeginOffset),
        HeapObject::RawField(object, Map::kPointerFieldsEndOffset));
  }
}

template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitWeakCell(Map* map,
                                                        HeapObject* object) {
  Heap* heap = map->GetHeap();
  WeakCell* weak_cell = reinterpret_cast<WeakCell*>(object);
  // Enqueue weak cell in linked list of encountered weak collections.
  // We can ignore weak cells with cleared values because they will always
  // contain smi zero.
  if (weak_cell->next_cleared() && !weak_cell->cleared()) {
    HeapObject* value = HeapObject::cast(weak_cell->value());
    if (ObjectMarking::IsBlackOrGrey<IncrementalMarking::kAtomicity>(
            value, MarkingState::Internal(value))) {
      // Weak cells with live values are directly processed here to reduce
      // the processing time of weak cells during the main GC pause.
      Object** slot = HeapObject::RawField(weak_cell, WeakCell::kValueOffset);
      map->GetHeap()->mark_compact_collector()->RecordSlot(weak_cell, slot,
                                                           *slot);
    } else {
      // If we do not know about liveness of values of weak cells, we have to
      // process them when we know the liveness of the whole transitive
      // closure.
      weak_cell->set_next(heap->encountered_weak_cells(),
                          UPDATE_WEAK_WRITE_BARRIER);
      heap->set_encountered_weak_cells(weak_cell);
    }
  }
}


template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitTransitionArray(
    Map* map, HeapObject* object) {
  TransitionArray* array = TransitionArray::cast(object);
  Heap* heap = array->GetHeap();
  // Visit strong references.
  if (array->HasPrototypeTransitions()) {
    StaticVisitor::VisitPointer(heap, array,
                                array->GetPrototypeTransitionsSlot());
  }
  int num_transitions = TransitionArray::NumberOfTransitions(array);
  for (int i = 0; i < num_transitions; ++i) {
    StaticVisitor::VisitPointer(heap, array, array->GetKeySlot(i));
  }
  // Enqueue the array in linked list of encountered transition arrays if it is
  // not already in the list.
  if (array->next_link()->IsUndefined(heap->isolate())) {
    Heap* heap = map->GetHeap();
    array->set_next_link(heap->encountered_transition_arrays(),
                         UPDATE_WEAK_WRITE_BARRIER);
    heap->set_encountered_transition_arrays(array);
  }
}

template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitWeakCollection(
    Map* map, HeapObject* object) {
  typedef FlexibleBodyVisitor<StaticVisitor,
                              JSWeakCollection::BodyDescriptorWeak,
                              void> JSWeakCollectionBodyVisitor;
  Heap* heap = map->GetHeap();
  JSWeakCollection* weak_collection =
      reinterpret_cast<JSWeakCollection*>(object);

  // Enqueue weak collection in linked list of encountered weak collections.
  if (weak_collection->next() == heap->undefined_value()) {
    weak_collection->set_next(heap->encountered_weak_collections());
    heap->set_encountered_weak_collections(weak_collection);
  }

  // Skip visiting the backing hash table containing the mappings and the
  // pointer to the other enqueued weak collections, both are post-processed.
  JSWeakCollectionBodyVisitor::Visit(map, object);

  // Partially initialized weak collection is enqueued, but table is ignored.
  if (!weak_collection->table()->IsHashTable()) return;

  // Mark the backing hash table without pushing it on the marking stack.
  Object** slot = HeapObject::RawField(object, JSWeakCollection::kTableOffset);
  HeapObject* obj = HeapObject::cast(*slot);
  heap->mark_compact_collector()->RecordSlot(object, slot, obj);
  StaticVisitor::MarkObjectWithoutPush(heap, obj);
}


template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitCode(Map* map,
                                                    HeapObject* object) {
  typedef FlexibleBodyVisitor<StaticVisitor, Code::BodyDescriptor, void>
      CodeBodyVisitor;
  Heap* heap = map->GetHeap();
  Code* code = Code::cast(object);
  if (FLAG_age_code && !heap->isolate()->serializer_enabled()) {
    code->MakeOlder();
  }
  CodeBodyVisitor::Visit(map, object);
}


template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitSharedFunctionInfo(
    Map* map, HeapObject* object) {
  Heap* heap = map->GetHeap();
  SharedFunctionInfo* shared = SharedFunctionInfo::cast(object);
  if (shared->ic_age() != heap->global_ic_age()) {
    shared->ResetForNewContext(heap->global_ic_age());
  }
  FixedBodyVisitor<StaticVisitor, SharedFunctionInfo::BodyDescriptor,
                   void>::Visit(map, object);
}


template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitJSFunction(Map* map,
                                                          HeapObject* object) {
  FlexibleBodyVisitor<StaticVisitor, JSFunction::BodyDescriptorWeak,
                      void>::Visit(map, object);
}

template <typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::MarkMapContents(Heap* heap,
                                                          Map* map) {
  // Since descriptor arrays are potentially shared, ensure that only the
  // descriptors that belong to this map are marked. The first time a non-empty
  // descriptor array is marked, its header is also visited. The slot holding
  // the descriptor array will be implicitly recorded when the pointer fields of
  // this map are visited.  Prototype maps don't keep track of transitions, so
  // just mark the entire descriptor array.
  if (!map->is_prototype_map()) {
    DescriptorArray* descriptors = map->instance_descriptors();
    if (StaticVisitor::MarkObjectWithoutPush(heap, descriptors) &&
        descriptors->length() > 0) {
      StaticVisitor::VisitPointers(heap, descriptors,
                                   descriptors->GetFirstElementAddress(),
                                   descriptors->GetDescriptorEndSlot(0));
    }
    int start = 0;
    int end = map->NumberOfOwnDescriptors();
    if (start < end) {
      StaticVisitor::VisitPointers(heap, descriptors,
                                   descriptors->GetDescriptorStartSlot(start),
                                   descriptors->GetDescriptorEndSlot(end));
    }
  }

  // Mark the pointer fields of the Map. Since the transitions array has
  // been marked already, it is fine that one of these fields contains a
  // pointer to it.
  StaticVisitor::VisitPointers(
      heap, map, HeapObject::RawField(map, Map::kPointerFieldsBeginOffset),
      HeapObject::RawField(map, Map::kPointerFieldsEndOffset));
}


inline static bool HasSourceCode(Heap* heap, SharedFunctionInfo* info) {
  Object* undefined = heap->undefined_value();
  return (info->script() != undefined) &&
         (reinterpret_cast<Script*>(info->script())->source() != undefined);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::Visit(HeapObject* object) {
  return Visit(object->map(), object);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::Visit(Map* map,
                                                           HeapObject* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  switch (static_cast<VisitorId>(map->visitor_id())) {
#define CASE(type)   \
  case kVisit##type: \
    return visitor->Visit##type(map, type::cast(object));
    TYPED_VISITOR_ID_LIST(CASE)
#undef CASE
    case kVisitShortcutCandidate:
      return visitor->VisitShortcutCandidate(map, ConsString::cast(object));
    case kVisitNativeContext:
      return visitor->VisitNativeContext(map, Context::cast(object));
    case kVisitDataObject:
      return visitor->VisitDataObject(map, HeapObject::cast(object));
    case kVisitJSObjectFast:
      return visitor->VisitJSObjectFast(map, JSObject::cast(object));
    case kVisitJSApiObject:
      return visitor->VisitJSApiObject(map, JSObject::cast(object));
    case kVisitStruct:
      return visitor->VisitStruct(map, HeapObject::cast(object));
    case kVisitFreeSpace:
      return visitor->VisitFreeSpace(map, FreeSpace::cast(object));
    case kVisitorIdCount:
      UNREACHABLE();
  }
  UNREACHABLE();
  // Make the compiler happy.
  return ResultType();
}

template <typename ResultType, typename ConcreteVisitor>
void HeapVisitor<ResultType, ConcreteVisitor>::VisitMapPointer(
    HeapObject* host, HeapObject** map) {
  static_cast<ConcreteVisitor*>(this)->VisitPointer(
      host, reinterpret_cast<Object**>(map));
}

#define VISIT(type)                                                 \
  template <typename ResultType, typename ConcreteVisitor>          \
  ResultType HeapVisitor<ResultType, ConcreteVisitor>::Visit##type( \
      Map* map, type* object) {                                     \
    ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this); \
    if (!visitor->ShouldVisit(object)) return ResultType();         \
    int size = type::BodyDescriptor::SizeOf(map, object);           \
    if (visitor->ShouldVisitMapPointer())                           \
      visitor->VisitMapPointer(object, object->map_slot());         \
    type::BodyDescriptor::IterateBody(object, size, visitor);       \
    return static_cast<ResultType>(size);                           \
  }
TYPED_VISITOR_ID_LIST(VISIT)
#undef VISIT

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitShortcutCandidate(
    Map* map, ConsString* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  if (!visitor->ShouldVisit(object)) return ResultType();
  int size = ConsString::BodyDescriptor::SizeOf(map, object);
  if (visitor->ShouldVisitMapPointer())
    visitor->VisitMapPointer(object, object->map_slot());
  ConsString::BodyDescriptor::IterateBody(object, size, visitor);
  return static_cast<ResultType>(size);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitNativeContext(
    Map* map, Context* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  if (!visitor->ShouldVisit(object)) return ResultType();
  int size = Context::BodyDescriptor::SizeOf(map, object);
  if (visitor->ShouldVisitMapPointer())
    visitor->VisitMapPointer(object, object->map_slot());
  Context::BodyDescriptor::IterateBody(object, size, visitor);
  return static_cast<ResultType>(size);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitDataObject(
    Map* map, HeapObject* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  if (!visitor->ShouldVisit(object)) return ResultType();
  int size = map->instance_size();
  if (visitor->ShouldVisitMapPointer())
    visitor->VisitMapPointer(object, object->map_slot());
  return static_cast<ResultType>(size);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitJSObjectFast(
    Map* map, JSObject* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  if (!visitor->ShouldVisit(object)) return ResultType();
  int size = JSObject::FastBodyDescriptor::SizeOf(map, object);
  if (visitor->ShouldVisitMapPointer())
    visitor->VisitMapPointer(object, object->map_slot());
  JSObject::FastBodyDescriptor::IterateBody(object, size, visitor);
  return static_cast<ResultType>(size);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitJSApiObject(
    Map* map, JSObject* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  if (!visitor->ShouldVisit(object)) return ResultType();
  int size = JSObject::BodyDescriptor::SizeOf(map, object);
  if (visitor->ShouldVisitMapPointer())
    visitor->VisitMapPointer(object, object->map_slot());
  JSObject::BodyDescriptor::IterateBody(object, size, visitor);
  return static_cast<ResultType>(size);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitStruct(
    Map* map, HeapObject* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  if (!visitor->ShouldVisit(object)) return ResultType();
  int size = map->instance_size();
  if (visitor->ShouldVisitMapPointer())
    visitor->VisitMapPointer(object, object->map_slot());
  StructBodyDescriptor::IterateBody(object, size, visitor);
  return static_cast<ResultType>(size);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitFreeSpace(
    Map* map, FreeSpace* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  if (!visitor->ShouldVisit(object)) return ResultType();
  if (visitor->ShouldVisitMapPointer())
    visitor->VisitMapPointer(object, object->map_slot());
  return static_cast<ResultType>(FreeSpace::cast(object)->size());
}

template <typename ConcreteVisitor>
int NewSpaceVisitor<ConcreteVisitor>::VisitJSFunction(Map* map,
                                                      JSFunction* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  int size = JSFunction::BodyDescriptorWeak::SizeOf(map, object);
  JSFunction::BodyDescriptorWeak::IterateBody(object, size, visitor);
  return size;
}

template <typename ConcreteVisitor>
int NewSpaceVisitor<ConcreteVisitor>::VisitNativeContext(Map* map,
                                                         Context* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  int size = Context::BodyDescriptor::SizeOf(map, object);
  Context::BodyDescriptor::IterateBody(object, size, visitor);
  return size;
}

template <typename ConcreteVisitor>
int NewSpaceVisitor<ConcreteVisitor>::VisitJSApiObject(Map* map,
                                                       JSObject* object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  return visitor->VisitJSObject(map, object);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_VISITING_INL_H_
