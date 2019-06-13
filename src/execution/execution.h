// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_EXECUTION_H_
#define V8_EXECUTION_EXECUTION_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class MicrotaskQueue;

template <typename T>
class Handle;

class Execution final : public AllStatic {
 public:
  // Whether to report pending messages, or keep them pending on the isolate.
  enum class MessageHandling { kReport, kKeepPending };
  enum class Target { kCallable, kRunMicrotasks };

  // Call a function, the caller supplies a receiver and an array
  // of arguments.
  //
  // When the function called is not in strict mode, receiver is
  // converted to an object.
  //
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Object> Call(
      Isolate* isolate, Handle<Object> callable, Handle<Object> receiver,
      int argc, Handle<Object> argv[]);

  // Construct object from function, the caller supplies an array of
  // arguments.
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> New(
      Isolate* isolate, Handle<Object> constructor, int argc,
      Handle<Object> argv[]);
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> New(
      Isolate* isolate, Handle<Object> constructor, Handle<Object> new_target,
      int argc, Handle<Object> argv[]);

  // Call a function, just like Call(), but handle don't report exceptions
  // externally.
  // The return value is either the result of calling the function (if no
  // exception occurred), or an empty handle.
  // If message_handling is MessageHandling::kReport, exceptions (except for
  // termination exceptions) will be stored in exception_out (if not a
  // nullptr).
  V8_EXPORT_PRIVATE static MaybeHandle<Object> TryCall(
      Isolate* isolate, Handle<Object> callable, Handle<Object> receiver,
      int argc, Handle<Object> argv[], MessageHandling message_handling,
      MaybeHandle<Object>* exception_out);
  // Convenience method for performing RunMicrotasks
  static MaybeHandle<Object> TryRunMicrotasks(
      Isolate* isolate, MicrotaskQueue* microtask_queue,
      MaybeHandle<Object>* exception_out);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_EXECUTION_H_
