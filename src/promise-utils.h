// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROMISE_UTILS_H_
#define V8_PROMISE_UTILS_H_

#include "src/contexts.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

// Helper methods for Promise builtins.
class PromiseUtils : public AllStatic {
 public:
  enum PromiseResolvingFunctionContextSlot {
    // Whether the resolve/reject callback was already called.
    kAlreadyVisitedSlot = Context::MIN_CONTEXT_SLOTS,

    // The promise which resolve/reject callbacks fulfill.
    kPromiseSlot,

    // Whether to trigger a debug event or not. Used in catch
    // prediction.
    kDebugEventSlot,
    kPromiseContextLength,
  };

  // These get and set the slots on the PromiseResolvingContext, which
  // is used by the resolve/reject promise callbacks.
  static JSPromise* GetPromise(Handle<Context> context);
  static Object* GetDebugEvent(Handle<Context> context);
  static bool HasAlreadyVisited(Handle<Context> context);
  static void SetAlreadyVisited(Handle<Context> context);

  static void CreateResolvingFunctions(Isolate* isolate,
                                       Handle<JSObject> promise,
                                       Handle<Object> debug_event,
                                       Handle<JSFunction>* resolve,
                                       Handle<JSFunction>* reject);
};

class GetPromiseCapabilityExecutor : public AllStatic {
 public:
  enum FunctionContextSlot {
    kCapabilitySlot = Context::MIN_CONTEXT_SLOTS,

    kContextLength,
  };
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PROMISE_UTILS_H_
