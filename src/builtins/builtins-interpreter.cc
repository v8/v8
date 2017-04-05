// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/globals.h"
#include "src/handles-inl.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

Handle<Code> Builtins::InterpreterPushArgsAndCall(
    TailCallMode tail_call_mode, InterpreterPushArgsMode mode) {
  switch (mode) {
    case InterpreterPushArgsMode::kJSFunction:
      if (tail_call_mode == TailCallMode::kDisallow) {
        return InterpreterPushArgsAndCallFunction();
      } else {
        return InterpreterPushArgsAndTailCallFunction();
      }
    case InterpreterPushArgsMode::kWithFinalSpread:
      CHECK(tail_call_mode == TailCallMode::kDisallow);
      return InterpreterPushArgsAndCallWithFinalSpread();
    case InterpreterPushArgsMode::kOther:
      if (tail_call_mode == TailCallMode::kDisallow) {
        return InterpreterPushArgsAndCall();
      } else {
        return InterpreterPushArgsAndTailCall();
      }
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::InterpreterPushArgsAndConstruct(
    InterpreterPushArgsMode mode) {
  switch (mode) {
    case InterpreterPushArgsMode::kJSFunction:
      return InterpreterPushArgsAndConstructFunction();
    case InterpreterPushArgsMode::kWithFinalSpread:
      return InterpreterPushArgsAndConstructWithFinalSpread();
    case InterpreterPushArgsMode::kOther:
      return InterpreterPushArgsAndConstruct();
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

}  // namespace internal
}  // namespace v8
