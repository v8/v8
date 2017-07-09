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
  Handle<OrderedHashMap> table(OrderedHashMap::cast(map->table()), isolate);
  Handle<JSMapIterator> iterator = isolate->factory()->NewJSMapIterator(
      table, 0, JSMapIterator::kKindEntries);

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

BUILTIN(MapPrototypeEntries) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Map.prototype.entries";
  CHECK_RECEIVER(JSMap, map, kMethodName);
  return *isolate->factory()->NewJSMapIterator(
      handle(OrderedHashMap::cast(map->table()), isolate), 0,
      JSMapIterator::kKindEntries);
}

BUILTIN(MapPrototypeKeys) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Map.prototype.keys";
  CHECK_RECEIVER(JSMap, map, kMethodName);
  return *isolate->factory()->NewJSMapIterator(
      handle(OrderedHashMap::cast(map->table()), isolate), 0,
      JSMapIterator::kKindKeys);
}

BUILTIN(MapPrototypeValues) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Map.prototype.values";
  CHECK_RECEIVER(JSMap, map, kMethodName);
  return *isolate->factory()->NewJSMapIterator(
      handle(OrderedHashMap::cast(map->table()), isolate), 0,
      JSMapIterator::kKindValues);
}

BUILTIN(MapIteratorPrototypeNext) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Map Iterator.prototype.next";
  CHECK_RECEIVER(JSMapIterator, iterator, kMethodName);
  Handle<Object> value = isolate->factory()->undefined_value();
  bool done = true;
  if (iterator->HasMore()) {
    done = false;
    switch (Smi::cast(iterator->kind())->value()) {
      case JSMapIterator::kKindEntries:
        value = MakeEntryPair(isolate, handle(iterator->CurrentKey(), isolate),
                              handle(iterator->CurrentValue(), isolate));
        break;
      case JSMapIterator::kKindKeys:
        value = handle(iterator->CurrentKey(), isolate);
        break;
      case JSMapIterator::kKindValues:
        value = handle(iterator->CurrentValue(), isolate);
        break;
    }
    iterator->MoveNext();
  }
  return *isolate->factory()->NewJSIteratorResult(value, done);
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
  Handle<OrderedHashSet> table(OrderedHashSet::cast(set->table()), isolate);
  Handle<JSSetIterator> iterator = isolate->factory()->NewJSSetIterator(
      table, 0, JSSetIterator::kKindValues);

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

BUILTIN(SetPrototypeEntries) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Set.prototype.entries";
  CHECK_RECEIVER(JSSet, set, kMethodName);
  return *isolate->factory()->NewJSSetIterator(
      handle(OrderedHashSet::cast(set->table()), isolate), 0,
      JSSetIterator::kKindEntries);
}

BUILTIN(SetPrototypeValues) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Set.prototype.values";
  CHECK_RECEIVER(JSSet, set, kMethodName);
  return *isolate->factory()->NewJSSetIterator(
      handle(OrderedHashSet::cast(set->table()), isolate), 0,
      JSSetIterator::kKindValues);
}

BUILTIN(SetIteratorPrototypeNext) {
  HandleScope scope(isolate);
  const char* const kMethodName = "Set Iterator.prototype.next";
  CHECK_RECEIVER(JSSetIterator, iterator, kMethodName);
  Handle<Object> value = isolate->factory()->undefined_value();
  bool done = true;
  if (iterator->HasMore()) {
    value = handle(iterator->CurrentKey(), isolate);
    done = false;
    if (Smi::cast(iterator->kind())->value() == JSSetIterator::kKindEntries) {
      value = MakeEntryPair(isolate, value, value);
    }
    iterator->MoveNext();
  }
  return *isolate->factory()->NewJSIteratorResult(value, done);
}

}  // namespace internal
}  // namespace v8
