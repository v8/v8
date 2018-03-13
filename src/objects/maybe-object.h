// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MAYBE_OBJECT_H_
#define V8_OBJECTS_MAYBE_OBJECT_H_

#include "include/v8.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

class HeapObject;
class Smi;

// A MaybeObject is either a SMI, a strong reference to a HeapObject, a weak
// reference to a HeapObject, or a cleared weak reference. It's used for
// implementing in-place weak references (see design doc: goo.gl/j6SdcK )
class MaybeObject {
 public:
  bool IsSmi() const { return HAS_SMI_TAG(this); }
  inline bool IsSmi(Smi** value);

  bool IsClearedWeakHeapObject() {
    return Internals::IsClearedWeakHeapObject(this);
  }

  inline bool IsStrongOrWeakHeapObject();
  inline bool ToStrongOrWeakHeapObject(HeapObject** result);
  inline bool ToStrongOrWeakHeapObject(HeapObject** result,
                                       HeapObjectReferenceType* reference_type);
  inline bool IsStrongHeapObject();
  inline bool ToStrongHeapObject(HeapObject** result);
  inline bool IsWeakHeapObject();
  inline bool ToWeakHeapObject(HeapObject** result);

  inline HeapObject* GetHeapObject();

  static MaybeObject* FromSmi(Smi* smi) {
    DCHECK(HAS_SMI_TAG(smi));
    return reinterpret_cast<MaybeObject*>(smi);
  }

  static MaybeObject* FromObject(Object* object) {
    DCHECK(!Internals::HasWeakHeapObjectTag(object));
    return reinterpret_cast<MaybeObject*>(object);
  }

  static MaybeObject* MakeWeak(MaybeObject* object) {
    DCHECK(object->IsStrongOrWeakHeapObject());
    return Internals::AddWeakHeapObjectMask(object);
  }

#ifdef VERIFY_HEAP
  static void VerifyMaybeObjectPointer(MaybeObject* p);
#endif

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(MaybeObject);
};

// A HeapObjectReference is either a strong reference to a HeapObject, a weak
// reference to a HeapObject, or a cleared weak reference.
class HeapObjectReference : public MaybeObject {
 public:
  static HeapObjectReference* Strong(HeapObject* object) {
    DCHECK(!Internals::HasWeakHeapObjectTag(object));
    return reinterpret_cast<HeapObjectReference*>(object);
  }

  static HeapObjectReference* Weak(HeapObject* object) {
    DCHECK(!Internals::HasWeakHeapObjectTag(object));
    return Internals::AddWeakHeapObjectMask(object);
  }

  static HeapObjectReference* ClearedValue() {
    return reinterpret_cast<HeapObjectReference*>(kClearedWeakHeapObject);
  }

  static void Update(HeapObjectReference** slot, HeapObject* value) {
    DCHECK(!HAS_SMI_TAG(*slot));
    DCHECK(Internals::HasHeapObjectTag(value));

#ifdef DEBUG
    bool weak_before = Internals::HasWeakHeapObjectTag(*slot);
#endif

    *slot = reinterpret_cast<HeapObjectReference*>(
        reinterpret_cast<intptr_t>(value) |
        (reinterpret_cast<intptr_t>(*slot) & kWeakHeapObjectMask));

#ifdef DEBUG
    bool weak_after = Internals::HasWeakHeapObjectTag(*slot);
    DCHECK_EQ(weak_before, weak_after);
#endif
  }

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(HeapObjectReference);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_MAYBE_OBJECT_H_
