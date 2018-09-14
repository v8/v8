// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MICROTASK_QUEUE_H_
#define V8_OBJECTS_MICROTASK_QUEUE_H_

#include "src/objects.h"
#include "src/objects/microtask.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class MicrotaskQueue : public Struct {
 public:
  DECL_CAST(MicrotaskQueue)
  DECL_VERIFIER(MicrotaskQueue)
  DECL_PRINTER(MicrotaskQueue)

  static void EnqueueMicrotask(Handle<MicrotaskQueue> microtask_queue,
                               Handle<Microtask> microtask);
  static void RunMicrotasks(Handle<MicrotaskQueue> microtask_queue);

  static const int kSize = HeapObject::kHeaderSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(MicrotaskQueue);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_MICROTASK_QUEUE_H_
