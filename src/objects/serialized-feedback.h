// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SERIALIZED_FEEDBACK_H_
#define V8_OBJECTS_SERIALIZED_FEEDBACK_H_

#include "src/objects/fixed-array.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class FeedbackVector;

// A serialized representation of FeedbackVector, used to share collected
// feedback between native contexts.
//
// Note: The encoding is not final and thus not documented here yet. Currently,
// only smi-based feedback is shared/serialized.
class SerializedFeedback : public ByteArray {
 public:
  // Serialize current feedback vector values into a SerializedFeedback object.
  static Handle<SerializedFeedback> Serialize(Isolate* isolate,
                                              Handle<FeedbackVector> vector);

  // Deserialize into the given vector.
  void DeserializeInto(FeedbackVector vector) const;

  DECL_CAST(SerializedFeedback)
  OBJECT_CONSTRUCTORS(SerializedFeedback, ByteArray);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SERIALIZED_FEEDBACK_H_
