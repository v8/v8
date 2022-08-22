// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/tagged-impl.h"

#include <sstream>

#include "src/objects/objects.h"
#include "src/strings/string-stream.h"
#include "src/utils/ostreams.h"

#ifdef V8_EXTERNAL_CODE_SPACE
// For IsCodeSpaceObject().
#include "src/heap/heap-write-barrier-inl.h"
#endif

namespace v8 {
namespace internal {

#ifdef V8_EXTERNAL_CODE_SPACE
bool CheckObjectComparisonAllowed(Address a, Address b) {
  if (!HAS_STRONG_HEAP_OBJECT_TAG(a) || !HAS_STRONG_HEAP_OBJECT_TAG(b)) {
    return true;
  }
  HeapObject obj_a = HeapObject::unchecked_cast(Object(a));
  HeapObject obj_b = HeapObject::unchecked_cast(Object(b));
  // This check might fail when we try to compare Code object with non-Code
  // object. The main legitimate case when such "mixed" comparison could happen
  // is comparing two AbstractCode objects. If that's the case one must use
  // AbstractCode's == operator instead of Object's one or SafeEquals().
  CHECK_EQ(IsCodeSpaceObject(obj_a), IsCodeSpaceObject(obj_b));
  return true;
}
#endif  // V8_EXTERNAL_CODE_SPACE

template <HeapObjectReferenceType kRefType, typename StorageType>
void TaggedImpl<kRefType, StorageType>::ShortPrint(FILE* out) {
  OFStream os(out);
  os << Brief(*this);
}

template <HeapObjectReferenceType kRefType, typename StorageType>
void TaggedImpl<kRefType, StorageType>::ShortPrint(StringStream* accumulator) {
  std::ostringstream os;
  os << Brief(*this);
  accumulator->Add(os.str().c_str());
}

template <HeapObjectReferenceType kRefType, typename StorageType>
void TaggedImpl<kRefType, StorageType>::ShortPrint(std::ostream& os) {
  os << Brief(*this);
}

// Explicit instantiation declarations.
template class TaggedImpl<HeapObjectReferenceType::STRONG, Address>;
template class TaggedImpl<HeapObjectReferenceType::WEAK, Address>;

}  // namespace internal
}  // namespace v8
