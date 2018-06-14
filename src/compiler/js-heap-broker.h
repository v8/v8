// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_HEAP_BROKER_H_
#define V8_COMPILER_JS_HEAP_BROKER_H_

#include "src/base/compiler-specific.h"
#include "src/base/optional.h"
#include "src/globals.h"
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace compiler {

class HeapReferenceType {
 public:
  enum OddballType : uint8_t { kUnknown, kBoolean, kUndefined, kNull, kHole };
  enum Flag : uint8_t { kUndetectable = 1 << 0, kCallable = 1 << 1 };

  typedef base::Flags<Flag> Flags;

  HeapReferenceType(InstanceType instance_type, Flags flags,
                    OddballType oddball_type = kUnknown)
      : instance_type_(instance_type),
        oddball_type_(oddball_type),
        flags_(flags) {}

  OddballType oddball_type() const { return oddball_type_; }
  InstanceType instance_type() const { return instance_type_; }
  Flags flags() const { return flags_; }

  bool is_callable() const { return flags_ & kCallable; }
  bool is_undetectable() const { return flags_ & kUndetectable; }

 private:
  InstanceType const instance_type_;
  OddballType const oddball_type_;
  Flags const flags_;
};

#define HEAP_BROKER_DATA_LIST(V) \
  V(JSFunction)                  \
  V(Number)

#define HEAP_BROKER_KIND_LIST(V) \
  HEAP_BROKER_DATA_LIST(V)       \
  V(InternalizedString)          \
  V(String)

#define FORWARD_DECL(Name) class Name##HeapReference;
HEAP_BROKER_DATA_LIST(FORWARD_DECL)
#undef FORWARD_DECL

class JSHeapBroker;

class HeapReference {
 public:
  explicit HeapReference(Handle<HeapObject> object) : object_(object) {}

#define HEAP_IS_METHOD_DECL(Name) bool Is##Name() const;
  HEAP_BROKER_KIND_LIST(HEAP_IS_METHOD_DECL)
#undef HEAP_IS_METHOD_DECL

#define HEAP_AS_METHOD_DECL(Name) Name##HeapReference As##Name() const;
  HEAP_BROKER_DATA_LIST(HEAP_AS_METHOD_DECL)
#undef HEAP_AS_METHOD_DECL

  HeapReferenceType type(const JSHeapBroker* broker) const;
  Handle<HeapObject> object() const { return object_; }

 private:
  friend class JSHeapBroker;
  Handle<HeapObject> const object_;
};

class V8_EXPORT_PRIVATE JSHeapBroker : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  JSHeapBroker(Isolate* isolate);

  HeapReferenceType HeapReferenceTypeFromMap(Handle<Map> map) const {
    return HeapReferenceTypeFromMap(*map);
  }

  HeapReference HeapReferenceForObject(Handle<Object> object) const;

  static base::Optional<int> TryGetSmi(Handle<Object> object);

 private:
  friend class HeapReference;
  HeapReferenceType HeapReferenceTypeFromMap(Map* map) const;

  Isolate* const isolate_;
  Isolate* isolate() const { return isolate_; }
};

class JSFunctionHeapReference : public HeapReference {
 public:
  explicit JSFunctionHeapReference(Handle<HeapObject> object)
      : HeapReference(object) {}
  bool HasBuiltinFunctionId() const;
  BuiltinFunctionId GetBuiltinFunctionId() const;
};

class NumberHeapReference : public HeapReference {
 public:
  explicit NumberHeapReference(Handle<HeapObject> object)
      : HeapReference(object) {}
  double value() const;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_HEAP_BROKER_H_
