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
  enum OddballType : uint8_t {
    kNone,     // Not an Oddball.
    kBoolean,  // True or False.
    kUndefined,
    kNull,
    kHole,
    kOther,  // Oddball, but none of the above.
    kAny     // Any Oddball.
  };
  enum Flag : uint8_t { kUndetectable = 1 << 0, kCallable = 1 << 1 };

  typedef base::Flags<Flag> Flags;

  HeapReferenceType(InstanceType instance_type, Flags flags,
                    OddballType oddball_type)
      : instance_type_(instance_type),
        oddball_type_(oddball_type),
        flags_(flags) {
    DCHECK_EQ(instance_type == ODDBALL_TYPE, oddball_type != kNone);
  }

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
  V(Context)                     \
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
class HeapReference;

class ObjectReference {
 public:
  explicit ObjectReference(Handle<Object> object) : object_(object) {}

  Handle<Object> object() const { return object_; }
  bool IsSmi() const;
  int AsSmi() const;
  HeapReference AsHeapReference() const;

 private:
  Handle<Object> object_;
};

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
  Handle<HeapObject> object_;
};

class V8_EXPORT_PRIVATE JSHeapBroker : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  JSHeapBroker(Isolate* isolate);

  HeapReferenceType HeapReferenceTypeFromMap(Handle<Map> map) const {
    return HeapReferenceTypeFromMap(*map);
  }

  HeapReference HeapReferenceForObject(Handle<Object> object) const;

  static base::Optional<int> TryGetSmi(Handle<Object> object);

  Isolate* isolate() const { return isolate_; }

 private:
  friend class HeapReference;
  HeapReferenceType HeapReferenceTypeFromMap(Map* map) const;

  Isolate* const isolate_;
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

class ContextHeapReference : public HeapReference {
 public:
  explicit ContextHeapReference(Handle<HeapObject> object)
      : HeapReference(object) {}
  base::Optional<ContextHeapReference> previous(
      const JSHeapBroker* broker) const;
  base::Optional<ObjectReference> get(const JSHeapBroker* broker,
                                      int index) const;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_HEAP_BROKER_H_
