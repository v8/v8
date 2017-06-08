// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

BUILTIN(MapGetSize) {
  HandleScope scope(isolate);
  const char* const kMethodName = "get Map.prototype.size";
  CHECK_RECEIVER(JSMap, map, kMethodName);
  Handle<OrderedHashMap> table(OrderedHashMap::cast(map->table()));

  Handle<Object> size =
      isolate->factory()->NewNumberFromInt(table->NumberOfElements());
  return *size;
}

BUILTIN(MapClear) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Map.prototype.clear";
  CHECK_RECEIVER(JSMap, map, kMethodName);
  JSMap::Clear(map);
  return isolate->heap()->undefined_value();
}

BUILTIN(MapForEach) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Map.prototype.forEach";
  CHECK_RECEIVER(JSMap, map, kMethodName);

  Handle<Object> callback_fn = args.atOrUndefined(isolate, 1);
  if (!callback_fn->IsCallable()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kCalledNonCallable, callback_fn));
  }

  Handle<Object> receiver = args.atOrUndefined(isolate, 2);
  Handle<OrderedHashMap> table(OrderedHashMap::cast(map->table()));
  Handle<JSMapIterator> iterator = isolate->factory()->NewJSMapIterator();
  iterator->set_table(*table);
  iterator->set_index(Smi::kZero);
  iterator->set_kind(Smi::FromInt(JSMapIterator::kKindEntries));

  while (iterator->HasMore()) {
    Handle<Object> key(iterator->CurrentKey(), isolate);
    Handle<Object> value(iterator->CurrentValue(), isolate);
    Handle<Object> argv[] = {value, key, map};
    RETURN_FAILURE_ON_EXCEPTION(
        isolate,
        Execution::Call(isolate, callback_fn, receiver, arraysize(argv), argv));
    iterator->MoveNext();
  }

  return isolate->heap()->undefined_value();
}

BUILTIN(SetGetSize) {
  HandleScope scope(isolate);
  const char* const kMethodName = "get Set.prototype.size";
  CHECK_RECEIVER(JSSet, set, kMethodName);
  Handle<OrderedHashSet> table(OrderedHashSet::cast(set->table()));

  Handle<Object> size =
      isolate->factory()->NewNumberFromInt(table->NumberOfElements());
  return *size;
}

BUILTIN(SetClear) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Set.prototype.clear";
  CHECK_RECEIVER(JSSet, set, kMethodName);
  JSSet::Clear(set);
  return isolate->heap()->undefined_value();
}

BUILTIN(SetForEach) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Set.prototype.forEach";
  CHECK_RECEIVER(JSSet, set, kMethodName);

  Handle<Object> callback_fn = args.atOrUndefined(isolate, 1);
  if (!callback_fn->IsCallable()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kCalledNonCallable, callback_fn));
  }

  Handle<Object> receiver = args.atOrUndefined(isolate, 2);
  Handle<OrderedHashSet> table(OrderedHashSet::cast(set->table()));
  Handle<JSSetIterator> iterator = isolate->factory()->NewJSSetIterator();
  iterator->set_table(*table);
  iterator->set_index(Smi::kZero);
  iterator->set_kind(Smi::FromInt(JSSetIterator::kKindValues));

  while (iterator->HasMore()) {
    Handle<Object> key(iterator->CurrentKey(), isolate);
    Handle<Object> argv[] = {key, key, set};
    RETURN_FAILURE_ON_EXCEPTION(
        isolate,
        Execution::Call(isolate, callback_fn, receiver, arraysize(argv), argv));
    iterator->MoveNext();
  }

  return isolate->heap()->undefined_value();
}

}  // namespace internal
}  // namespace v8
