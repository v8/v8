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

enum class OddballType : uint8_t {
  kNone,     // Not an Oddball.
  kBoolean,  // True or False.
  kUndefined,
  kNull,
  kHole,
  kOther,  // Oddball, but none of the above.
  kAny     // Any Oddball.
};

class HeapObjectType {
 public:
  enum Flag : uint8_t { kUndetectable = 1 << 0, kCallable = 1 << 1 };

  typedef base::Flags<Flag> Flags;

  HeapObjectType(InstanceType instance_type, Flags flags,
                 OddballType oddball_type)
      : instance_type_(instance_type),
        oddball_type_(oddball_type),
        flags_(flags) {
    DCHECK_EQ(instance_type == ODDBALL_TYPE,
              oddball_type != OddballType::kNone);
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
  V(HeapNumber)                  \
  V(HeapObject)                  \
  V(JSFunction)                  \
  V(Name)                        \
  V(NativeContext)               \
  V(ScriptContextTable)

#define HEAP_BROKER_KIND_LIST(V) \
  HEAP_BROKER_DATA_LIST(V)       \
  V(InternalizedString)          \
  V(String)

#define FORWARD_DECL(Name) class Name##Ref;
HEAP_BROKER_DATA_LIST(FORWARD_DECL)
#undef FORWARD_DECL

class JSHeapBroker;
class HeapObjectRef;

class ObjectRef {
 public:
  explicit ObjectRef(Handle<Object> object) : object_(object) {}

  template <typename T>
  Handle<T> object() const {
    AllowHandleDereference handle_dereference;
    return Handle<T>::cast(object_);
  }

  OddballType oddball_type(const JSHeapBroker* broker) const;

  bool IsSmi() const;
  int AsSmi() const;

#define HEAP_IS_METHOD_DECL(Name) bool Is##Name() const;
  HEAP_BROKER_KIND_LIST(HEAP_IS_METHOD_DECL)
#undef HEAP_IS_METHOD_DECL

#define HEAP_AS_METHOD_DECL(Name) Name##Ref As##Name() const;
  HEAP_BROKER_DATA_LIST(HEAP_AS_METHOD_DECL)
#undef HEAP_AS_METHOD_DECL

 private:
  Handle<Object> object_;
};

class HeapObjectRef : public ObjectRef {
 public:
  explicit HeapObjectRef(Handle<Object> object);
  HeapObjectType type(const JSHeapBroker* broker) const;

 private:
  friend class JSHeapBroker;
};

class V8_EXPORT_PRIVATE JSHeapBroker : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  JSHeapBroker(Isolate* isolate);

  HeapObjectType HeapObjectTypeFromMap(Handle<Map> map) const {
    AllowHandleDereference handle_dereference;
    return HeapObjectTypeFromMap(*map);
  }

  static base::Optional<int> TryGetSmi(Handle<Object> object);

  Isolate* isolate() const { return isolate_; }

 private:
  friend class HeapObjectRef;
  HeapObjectType HeapObjectTypeFromMap(Map* map) const;

  Isolate* const isolate_;
};

class JSFunctionRef : public HeapObjectRef {
 public:
  explicit JSFunctionRef(Handle<Object> object);
  bool HasBuiltinFunctionId() const;
  BuiltinFunctionId GetBuiltinFunctionId() const;
};

class HeapNumberRef : public HeapObjectRef {
 public:
  explicit HeapNumberRef(Handle<Object> object);
  double value() const;
};

class ContextRef : public HeapObjectRef {
 public:
  explicit ContextRef(Handle<Object> object);
  base::Optional<ContextRef> previous(const JSHeapBroker* broker) const;
  ObjectRef get(const JSHeapBroker* broker, int index) const;
};

class NativeContextRef : public ContextRef {
 public:
  explicit NativeContextRef(Handle<Object> object);
  ScriptContextTableRef script_context_table(const JSHeapBroker* broker) const;
};

class NameRef : public HeapObjectRef {
 public:
  explicit NameRef(Handle<Object> object);
};

class ScriptContextTableRef : public HeapObjectRef {
 public:
  explicit ScriptContextTableRef(Handle<Object> object);

  struct LookupResult {
    ContextRef context;
    bool immutable;
    int index;
  };

  base::Optional<LookupResult> lookup(const NameRef& name) const;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_HEAP_BROKER_H_
