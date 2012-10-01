// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_OBJECTS_VISITING_INL_H_
#define V8_OBJECTS_VISITING_INL_H_


namespace v8 {
namespace internal {

template<typename StaticVisitor>
void StaticNewSpaceVisitor<StaticVisitor>::Initialize() {
  table_.Register(kVisitShortcutCandidate,
                  &FixedBodyVisitor<StaticVisitor,
                  ConsString::BodyDescriptor,
                  int>::Visit);

  table_.Register(kVisitConsString,
                  &FixedBodyVisitor<StaticVisitor,
                  ConsString::BodyDescriptor,
                  int>::Visit);

  table_.Register(kVisitSlicedString,
                  &FixedBodyVisitor<StaticVisitor,
                  SlicedString::BodyDescriptor,
                  int>::Visit);

  table_.Register(kVisitFixedArray,
                  &FlexibleBodyVisitor<StaticVisitor,
                  FixedArray::BodyDescriptor,
                  int>::Visit);

  table_.Register(kVisitFixedDoubleArray, &VisitFixedDoubleArray);

  table_.Register(kVisitNativeContext,
                  &FixedBodyVisitor<StaticVisitor,
                  Context::ScavengeBodyDescriptor,
                  int>::Visit);

  table_.Register(kVisitByteArray, &VisitByteArray);

  table_.Register(kVisitSharedFunctionInfo,
                  &FixedBodyVisitor<StaticVisitor,
                  SharedFunctionInfo::BodyDescriptor,
                  int>::Visit);

  table_.Register(kVisitSeqAsciiString, &VisitSeqAsciiString);

  table_.Register(kVisitSeqTwoByteString, &VisitSeqTwoByteString);

  table_.Register(kVisitJSFunction, &VisitJSFunction);

  table_.Register(kVisitFreeSpace, &VisitFreeSpace);

  table_.Register(kVisitJSWeakMap, &JSObjectVisitor::Visit);

  table_.Register(kVisitJSRegExp, &JSObjectVisitor::Visit);

  table_.template RegisterSpecializations<DataObjectVisitor,
                                          kVisitDataObject,
                                          kVisitDataObjectGeneric>();

  table_.template RegisterSpecializations<JSObjectVisitor,
                                          kVisitJSObject,
                                          kVisitJSObjectGeneric>();
  table_.template RegisterSpecializations<StructVisitor,
                                          kVisitStruct,
                                          kVisitStructGeneric>();
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::Initialize() {
  table_.Register(kVisitShortcutCandidate,
                  &FixedBodyVisitor<StaticVisitor,
                  ConsString::BodyDescriptor,
                  void>::Visit);

  table_.Register(kVisitConsString,
                  &FixedBodyVisitor<StaticVisitor,
                  ConsString::BodyDescriptor,
                  void>::Visit);

  table_.Register(kVisitSlicedString,
                  &FixedBodyVisitor<StaticVisitor,
                  SlicedString::BodyDescriptor,
                  void>::Visit);

  table_.Register(kVisitFixedArray,
                  &FlexibleBodyVisitor<StaticVisitor,
                  FixedArray::BodyDescriptor,
                  void>::Visit);

  table_.Register(kVisitFixedDoubleArray, &DataObjectVisitor::Visit);

  table_.Register(kVisitNativeContext, &VisitNativeContext);

  table_.Register(kVisitByteArray, &DataObjectVisitor::Visit);

  table_.Register(kVisitFreeSpace, &DataObjectVisitor::Visit);

  table_.Register(kVisitSeqAsciiString, &DataObjectVisitor::Visit);

  table_.Register(kVisitSeqTwoByteString, &DataObjectVisitor::Visit);

  table_.Register(kVisitJSWeakMap, &StaticVisitor::VisitJSWeakMap);

  table_.Register(kVisitOddball,
                  &FixedBodyVisitor<StaticVisitor,
                  Oddball::BodyDescriptor,
                  void>::Visit);

  table_.Register(kVisitMap, &VisitMap);

  table_.Register(kVisitCode, &VisitCode);

  // Registration for kVisitSharedFunctionInfo is done by StaticVisitor.

  // Registration for kVisitJSFunction is done by StaticVisitor.

  // Registration for kVisitJSRegExp is done by StaticVisitor.

  table_.Register(kVisitPropertyCell,
                  &FixedBodyVisitor<StaticVisitor,
                  JSGlobalPropertyCell::BodyDescriptor,
                  void>::Visit);

  table_.template RegisterSpecializations<DataObjectVisitor,
                                          kVisitDataObject,
                                          kVisitDataObjectGeneric>();

  table_.template RegisterSpecializations<JSObjectVisitor,
                                          kVisitJSObject,
                                          kVisitJSObjectGeneric>();

  table_.template RegisterSpecializations<StructObjectVisitor,
                                          kVisitStruct,
                                          kVisitStructGeneric>();
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitCodeEntry(
    Heap* heap, Address entry_address) {
  Code* code = Code::cast(Code::GetObjectFromEntryAddress(entry_address));
  heap->mark_compact_collector()->RecordCodeEntrySlot(entry_address, code);
  StaticVisitor::MarkObject(heap, code);
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitEmbeddedPointer(
    Heap* heap, RelocInfo* rinfo) {
  ASSERT(rinfo->rmode() == RelocInfo::EMBEDDED_OBJECT);
  ASSERT(!rinfo->target_object()->IsConsString());
  HeapObject* object = HeapObject::cast(rinfo->target_object());
  heap->mark_compact_collector()->RecordRelocSlot(rinfo, object);
  StaticVisitor::MarkObject(heap, object);
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitGlobalPropertyCell(
    Heap* heap, RelocInfo* rinfo) {
  ASSERT(rinfo->rmode() == RelocInfo::GLOBAL_PROPERTY_CELL);
  JSGlobalPropertyCell* cell = rinfo->target_cell();
  StaticVisitor::MarkObject(heap, cell);
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitDebugTarget(
    Heap* heap, RelocInfo* rinfo) {
  ASSERT((RelocInfo::IsJSReturn(rinfo->rmode()) &&
          rinfo->IsPatchedReturnSequence()) ||
         (RelocInfo::IsDebugBreakSlot(rinfo->rmode()) &&
          rinfo->IsPatchedDebugBreakSlotSequence()));
  Code* target = Code::GetCodeFromTargetAddress(rinfo->call_address());
  heap->mark_compact_collector()->RecordRelocSlot(rinfo, target);
  StaticVisitor::MarkObject(heap, target);
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitCodeTarget(
    Heap* heap, RelocInfo* rinfo) {
  ASSERT(RelocInfo::IsCodeTarget(rinfo->rmode()));
  Code* target = Code::GetCodeFromTargetAddress(rinfo->target_address());
  // Monomorphic ICs are preserved when possible, but need to be flushed
  // when they might be keeping a Context alive, or when the heap is about
  // to be serialized.
  if (FLAG_cleanup_code_caches_at_gc && target->is_inline_cache_stub()
      && (target->ic_state() == MEGAMORPHIC || Serializer::enabled() ||
          heap->isolate()->context_exit_happened() ||
          target->ic_age() != heap->global_ic_age())) {
    IC::Clear(rinfo->pc());
    target = Code::GetCodeFromTargetAddress(rinfo->target_address());
  }
  heap->mark_compact_collector()->RecordRelocSlot(rinfo, target);
  StaticVisitor::MarkObject(heap, target);
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitNativeContext(
    Map* map, HeapObject* object) {
  FixedBodyVisitor<StaticVisitor,
                   Context::MarkCompactBodyDescriptor,
                   void>::Visit(map, object);

  MarkCompactCollector* collector = map->GetHeap()->mark_compact_collector();
  for (int idx = Context::FIRST_WEAK_SLOT;
       idx < Context::NATIVE_CONTEXT_SLOTS;
       ++idx) {
    Object** slot =
        HeapObject::RawField(object, FixedArray::OffsetOfElementAt(idx));
    collector->RecordSlot(slot, slot, *slot);
  }
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitMap(
    Map* map, HeapObject* object) {
  Heap* heap = map->GetHeap();
  Map* map_object = Map::cast(object);

  // Clears the cache of ICs related to this map.
  if (FLAG_cleanup_code_caches_at_gc) {
    map_object->ClearCodeCache(heap);
  }

  // When map collection is enabled we have to mark through map's
  // transitions and back pointers in a special way to make these links
  // weak.  Only maps for subclasses of JSReceiver can have transitions.
  STATIC_ASSERT(LAST_TYPE == LAST_JS_RECEIVER_TYPE);
  if (FLAG_collect_maps &&
      map_object->instance_type() >= FIRST_JS_RECEIVER_TYPE) {
    MarkMapContents(heap, map_object);
  } else {
    Object** start_slot =
        HeapObject::RawField(object, Map::kPointerFieldsBeginOffset);
    Object** end_slot =
        HeapObject::RawField(object, Map::kPointerFieldsEndOffset);
    StaticVisitor::VisitPointers(heap, start_slot, start_slot, end_slot);
  }
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitCode(
    Map* map, HeapObject* object) {
  Heap* heap = map->GetHeap();
  Code* code = Code::cast(object);
  if (FLAG_cleanup_code_caches_at_gc) {
    code->ClearTypeFeedbackCells(heap);
  }
  code->CodeIterateBody<StaticVisitor>(heap);
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::VisitJSRegExp(
    Map* map, HeapObject* object) {
  int last_property_offset =
      JSRegExp::kSize + kPointerSize * map->inobject_properties();
  Object** start_slot =
      HeapObject::RawField(object, JSRegExp::kPropertiesOffset);
  Object** end_slot =
      HeapObject::RawField(object, last_property_offset);
  StaticVisitor::VisitPointers(
      map->GetHeap(), start_slot, start_slot, end_slot);
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::MarkMapContents(
    Heap* heap, Map* map) {
  // Make sure that the back pointer stored either in the map itself or
  // inside its transitions array is marked. Skip recording the back
  // pointer slot since map space is not compacted.
  StaticVisitor::MarkObject(heap, HeapObject::cast(map->GetBackPointer()));

  // Treat pointers in the transitions array as weak and also mark that
  // array to prevent visiting it later. Skip recording the transition
  // array slot, since it will be implicitly recorded when the pointer
  // fields of this map are visited.
  TransitionArray* transitions = map->unchecked_transition_array();
  if (transitions->IsTransitionArray()) {
    MarkTransitionArray(heap, transitions);
  } else {
    // Already marked by marking map->GetBackPointer() above.
    ASSERT(transitions->IsMap() || transitions->IsUndefined());
  }

  // Mark the pointer fields of the Map. Since the transitions array has
  // been marked already, it is fine that one of these fields contains a
  // pointer to it.
  Object** start_slot =
      HeapObject::RawField(map, Map::kPointerFieldsBeginOffset);
  Object** end_slot =
      HeapObject::RawField(map, Map::kPointerFieldsEndOffset);
  StaticVisitor::VisitPointers(heap, start_slot, start_slot, end_slot);
}


template<typename StaticVisitor>
void StaticMarkingVisitor<StaticVisitor>::MarkTransitionArray(
    Heap* heap, TransitionArray* transitions) {
  if (!StaticVisitor::MarkObjectWithoutPush(heap, transitions)) return;

  // Skip recording the descriptors_pointer slot since the cell space
  // is not compacted and descriptors are referenced through a cell.
  StaticVisitor::MarkObject(heap, transitions->descriptors_pointer());

  // Simple transitions do not have keys nor prototype transitions.
  if (transitions->IsSimpleTransition()) return;

  if (transitions->HasPrototypeTransitions()) {
    // Mark prototype transitions array but do not push it onto marking
    // stack, this will make references from it weak. We will clean dead
    // prototype transitions in ClearNonLiveTransitions.
    Object** slot = transitions->GetPrototypeTransitionsSlot();
    HeapObject* obj = HeapObject::cast(*slot);
    heap->mark_compact_collector()->RecordSlot(slot, slot, obj);
    StaticVisitor::MarkObjectWithoutPush(heap, obj);
  }

  for (int i = 0; i < transitions->number_of_transitions(); ++i) {
    StaticVisitor::VisitPointer(heap, transitions->GetKeySlot(i));
  }
}


void Code::CodeIterateBody(ObjectVisitor* v) {
  int mode_mask = RelocInfo::kCodeTargetMask |
                  RelocInfo::ModeMask(RelocInfo::EMBEDDED_OBJECT) |
                  RelocInfo::ModeMask(RelocInfo::GLOBAL_PROPERTY_CELL) |
                  RelocInfo::ModeMask(RelocInfo::EXTERNAL_REFERENCE) |
                  RelocInfo::ModeMask(RelocInfo::JS_RETURN) |
                  RelocInfo::ModeMask(RelocInfo::DEBUG_BREAK_SLOT) |
                  RelocInfo::ModeMask(RelocInfo::RUNTIME_ENTRY);

  // There are two places where we iterate code bodies: here and the
  // templated CodeIterateBody (below).  They should be kept in sync.
  IteratePointer(v, kRelocationInfoOffset);
  IteratePointer(v, kHandlerTableOffset);
  IteratePointer(v, kDeoptimizationDataOffset);
  IteratePointer(v, kTypeFeedbackInfoOffset);

  RelocIterator it(this, mode_mask);
  for (; !it.done(); it.next()) {
    it.rinfo()->Visit(v);
  }
}


template<typename StaticVisitor>
void Code::CodeIterateBody(Heap* heap) {
  int mode_mask = RelocInfo::kCodeTargetMask |
                  RelocInfo::ModeMask(RelocInfo::EMBEDDED_OBJECT) |
                  RelocInfo::ModeMask(RelocInfo::GLOBAL_PROPERTY_CELL) |
                  RelocInfo::ModeMask(RelocInfo::EXTERNAL_REFERENCE) |
                  RelocInfo::ModeMask(RelocInfo::JS_RETURN) |
                  RelocInfo::ModeMask(RelocInfo::DEBUG_BREAK_SLOT) |
                  RelocInfo::ModeMask(RelocInfo::RUNTIME_ENTRY);

  // There are two places where we iterate code bodies: here and the
  // non-templated CodeIterateBody (above).  They should be kept in sync.
  StaticVisitor::VisitPointer(
      heap,
      reinterpret_cast<Object**>(this->address() + kRelocationInfoOffset));
  StaticVisitor::VisitPointer(
      heap,
      reinterpret_cast<Object**>(this->address() + kHandlerTableOffset));
  StaticVisitor::VisitPointer(
      heap,
      reinterpret_cast<Object**>(this->address() + kDeoptimizationDataOffset));
  StaticVisitor::VisitPointer(
      heap,
      reinterpret_cast<Object**>(this->address() + kTypeFeedbackInfoOffset));

  RelocIterator it(this, mode_mask);
  for (; !it.done(); it.next()) {
    it.rinfo()->template Visit<StaticVisitor>(heap);
  }
}


} }  // namespace v8::internal

#endif  // V8_OBJECTS_VISITING_INL_H_
