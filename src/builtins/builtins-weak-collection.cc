// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"

#include "src/objects-inl.h"

namespace v8 {
namespace internal {

namespace {

// Returns the value keyed by |key|, or the hole if |key| is not present.
Object* WeakCollectionLookup(Handle<JSWeakCollection> collection,
                             Handle<JSReceiver> key) {
  return ObjectHashTable::cast(collection->table())->Lookup(key);
}

Object* WeakCollectionHas(Isolate* isolate, Handle<JSWeakCollection> collection,
                          Handle<JSReceiver> key) {
  return isolate->heap()->ToBoolean(
      !WeakCollectionLookup(collection, key)->IsTheHole(isolate));
}

}  // anonymous namespace

BUILTIN(WeakMapGet) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSWeakMap, map, "WeakMap.prototype.get");
  if (!args.atOrUndefined(isolate, 1)->IsJSReceiver()) {
    return isolate->heap()->undefined_value();
  }
  Handle<JSReceiver> key = args.at<JSReceiver>(1);
  Object* lookup = WeakCollectionLookup(map, key);
  return lookup->IsTheHole(isolate) ? isolate->heap()->undefined_value()
                                    : lookup;
}

BUILTIN(WeakMapHas) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSWeakMap, map, "WeakMap.prototype.has");
  if (!args.atOrUndefined(isolate, 1)->IsJSReceiver()) {
    return isolate->heap()->false_value();
  }
  Handle<JSReceiver> key = args.at<JSReceiver>(1);
  return WeakCollectionHas(isolate, map, key);
}

BUILTIN(WeakMapDelete) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSWeakMap, map, "WeakMap.prototype.delete");
  if (!args.atOrUndefined(isolate, 1)->IsJSReceiver()) {
    return isolate->heap()->false_value();
  }
  Handle<JSReceiver> key = args.at<JSReceiver>(1);
  return isolate->heap()->ToBoolean(JSWeakCollection::Delete(map, key));
}

BUILTIN(WeakMapSet) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSWeakMap, map, "WeakMap.prototype.set");
  if (!args.atOrUndefined(isolate, 1)->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kInvalidWeakMapKey));
  }
  Handle<JSReceiver> key = args.at<JSReceiver>(1);
  Handle<Object> value = args.atOrUndefined(isolate, 2);
  JSWeakCollection::Set(map, key, value);
  return *map;
}

BUILTIN(WeakSetAdd) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSWeakSet, set, "WeakSet.prototype.set");
  if (!args.atOrUndefined(isolate, 1)->IsJSReceiver()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kInvalidWeakSetValue));
  }
  Handle<JSReceiver> key = args.at<JSReceiver>(1);
  JSWeakCollection::Set(set, key, isolate->factory()->true_value());
  return *set;
}

BUILTIN(WeakSetDelete) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSWeakSet, set, "WeakSet.prototype.delete");
  if (!args.atOrUndefined(isolate, 1)->IsJSReceiver()) {
    return isolate->heap()->false_value();
  }
  Handle<JSReceiver> key = args.at<JSReceiver>(1);
  return isolate->heap()->ToBoolean(JSWeakCollection::Delete(set, key));
}

BUILTIN(WeakSetHas) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSWeakSet, set, "WeakSet.prototype.has");
  if (!args.atOrUndefined(isolate, 1)->IsJSReceiver()) {
    return isolate->heap()->false_value();
  }
  Handle<JSReceiver> key = args.at<JSReceiver>(1);
  return WeakCollectionHas(isolate, set, key);
}

}  // namespace internal
}  // namespace v8
