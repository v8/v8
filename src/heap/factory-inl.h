// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FACTORY_INL_H_
#define V8_HEAP_FACTORY_INL_H_

#include "src/heap/factory.h"

// Clients of this interface shouldn't depend on lots of heap internals.
// Do not include anything from src/heap here!
#include "src/execution/isolate-inl.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory-base-inl.h"
#include "src/objects/feedback-cell.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/string-inl.h"
#include "src/objects/string-table-inl.h"
#include "src/strings/string-hasher.h"

namespace v8 {
namespace internal {

#define ROOT_ACCESSOR(Type, name, CamelName)                                 \
  Handle<Type> Factory::name() {                                             \
    return Handle<Type>(&isolate()->roots_table()[RootIndex::k##CamelName]); \
  }
ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

bool Factory::CodeBuilder::CompiledWithConcurrentBaseline() const {
  return FLAG_concurrent_sparkplug && kind_ == CodeKind::BASELINE &&
         !local_isolate_->is_main_thread();
}

Handle<String> Factory::InternalizeString(Handle<String> string) {
  if (string->IsInternalizedString()) return string;
  return isolate()->string_table()->LookupString(isolate(), string);
}

Handle<Name> Factory::InternalizeName(Handle<Name> name) {
  if (name->IsUniqueName()) return name;
  return isolate()->string_table()->LookupString(isolate(),
                                                 Handle<String>::cast(name));
}

Handle<String> Factory::NewSubString(Handle<String> str, int begin, int end) {
  if (begin == 0 && end == str->length()) return str;
  return NewProperSubString(str, begin, end);
}

Handle<JSArray> Factory::NewJSArrayWithElements(Handle<FixedArrayBase> elements,
                                                ElementsKind elements_kind,
                                                AllocationType allocation) {
  return NewJSArrayWithElements(elements, elements_kind, elements->length(),
                                allocation);
}

Handle<JSObject> Factory::NewFastOrSlowJSObjectFromMap(
    Handle<Map> map, int number_of_slow_properties, AllocationType allocation,
    Handle<AllocationSite> allocation_site) {
  return map->is_dictionary_map()
             ? NewSlowJSObjectFromMap(map, number_of_slow_properties,
                                      allocation, allocation_site)
             : NewJSObjectFromMap(map, allocation, allocation_site);
}

Handle<Object> Factory::NewURIError() {
  return NewError(isolate()->uri_error_function(),
                  MessageTemplate::kURIMalformed);
}

ReadOnlyRoots Factory::read_only_roots() const {
  return ReadOnlyRoots(isolate());
}

Factory::CodeBuilder& Factory::CodeBuilder::set_interpreter_data(
    Handle<HeapObject> interpreter_data) {
  // This DCHECK requires this function to be in -inl.h.
  DCHECK(interpreter_data->IsInterpreterData() ||
         interpreter_data->IsBytecodeArray());
  interpreter_data_ = interpreter_data;
  return *this;
}

// static
void Factory::VerifyInit(Isolate* isolate, HeapObject heap_object) {
#ifdef MEMORY_SANITIZER
  // T::Init() must initialize all memory.
  __msan_check_mem_is_initialized(reinterpret_cast<void*>(heap_object.ptr()),
                                  heap_object.Size());
#endif  // MEMORY_SANITIZER
#if VERIFY_HEAP
  if (FLAG_verify_heap) {
    heap_object.HeapObjectVerify(isolate);
  }
#endif  // VERIFY_HEAP
}

template <typename T, typename... Params>
// static
Handle<T> Factory::InitializeAndVerify(
    Isolate* isolate, WriteBarrierMode write_barrier_mode_for_regular_writes,
    T raw, Params&&... params) {
  {
    DisallowGarbageCollection no_gc;
    T::Init(isolate, no_gc, write_barrier_mode_for_regular_writes, raw,
            std::forward<Params>(params)...);
    VerifyInit(isolate, raw);
  }
  Handle<T> result(raw, isolate);
  T::PostInit(isolate, result);
  return result;
}

template <typename... Params>
V8_INLINE Handle<FeedbackVector> Factory::NewFeedbackVector(
    Handle<SharedFunctionInfo> shared, Params&&... params) {
  const int length = shared->feedback_metadata().slot_count();
  DCHECK_LE(0, length);
  const int size = FeedbackVector::SizeFor(length);
  FeedbackVector raw_result = FeedbackVector::cast(AllocateRawWithImmortalMap(
      size, AllocationType::kOld, *feedback_vector_map()));
  return InitializeAndVerify(isolate(), WriteBarrierMode::UPDATE_WRITE_BARRIER,
                             raw_result, shared, length,
                             std::forward<Params>(params)...);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FACTORY_INL_H_
