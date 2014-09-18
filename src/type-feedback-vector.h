// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TYPE_FEEDBACK_VECTOR_H_
#define V8_TYPE_FEEDBACK_VECTOR_H_

#include "src/checks.h"
#include "src/elements-kind.h"
#include "src/heap/heap.h"
#include "src/isolate.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

class TypeFeedbackVector : public FixedArray {
 public:
  // Casting.
  static TypeFeedbackVector* cast(Object* obj) {
    DCHECK(obj->IsTypeFeedbackVector());
    return reinterpret_cast<TypeFeedbackVector*>(obj);
  }

  static Handle<TypeFeedbackVector> Copy(Isolate* isolate,
                                         Handle<TypeFeedbackVector> vector);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(TypeFeedbackVector);
};
}
}  // namespace v8::internal

#endif  // V8_TRANSITIONS_H_
