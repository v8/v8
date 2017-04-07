// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/globals.h"
#include "src/macro-assembler.h"

namespace v8 {
namespace internal {

void Builtins::Generate_InterpreterPushArgsAndCall(MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(
      masm, TailCallMode::kDisallow, InterpreterPushArgsMode::kOther);
}

void Builtins::Generate_InterpreterPushArgsAndCallFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(
      masm, TailCallMode::kDisallow, InterpreterPushArgsMode::kJSFunction);
}

void Builtins::Generate_InterpreterPushArgsAndCallWithFinalSpread(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(
      masm, TailCallMode::kDisallow, InterpreterPushArgsMode::kWithFinalSpread);
}

void Builtins::Generate_InterpreterPushArgsAndTailCall(MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(
      masm, TailCallMode::kAllow, InterpreterPushArgsMode::kOther);
}

void Builtins::Generate_InterpreterPushArgsAndTailCallFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(
      masm, TailCallMode::kAllow, InterpreterPushArgsMode::kJSFunction);
}

void Builtins::Generate_InterpreterPushArgsAndConstruct(MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndConstructImpl(
      masm, InterpreterPushArgsMode::kOther);
}

void Builtins::Generate_InterpreterPushArgsAndConstructWithFinalSpread(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndConstructImpl(
      masm, InterpreterPushArgsMode::kWithFinalSpread);
}

void Builtins::Generate_InterpreterPushArgsAndConstructFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndConstructImpl(
      masm, InterpreterPushArgsMode::kJSFunction);
}

}  // namespace internal
}  // namespace v8
