// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/bytecode-verifier.h"

#include "src/codegen/handler-table.h"
#include "src/flags/flags.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecodes.h"
#include "src/objects/bytecode-array-inl.h"
#include "src/utils/bit-vector.h"

namespace v8::internal {

// static
void BytecodeVerifier::Verify(IsolateForSandbox isolate,
                              Handle<BytecodeArray> bytecode, Zone* zone) {
  if (v8_flags.verify_bytecode_full) {
    VerifyFull(isolate, bytecode, zone);
  } else if (v8_flags.verify_bytecode_light) {
    VerifyLight(isolate, bytecode, zone);
  }

  bytecode->MarkVerified(isolate);
}

// static
void BytecodeVerifier::VerifyLight(IsolateForSandbox isolate,
                                   Handle<BytecodeArray> bytecode, Zone* zone) {
  // VerifyLight is meant to catch the most important issues (in particular,
  // ones that we've seen in the past) and should be lightweight enough to be
  // enabled by default.
  //
  // In particular, the lightweight verification ensures basic control-flow
  // integrity (CFI) by validating that jump targets are valid.

  unsigned bytecode_length = bytecode->length();
  BitVector valid_offsets(bytecode_length, zone);
  BitVector seen_jumps(bytecode_length, zone);

  interpreter::BytecodeArrayIterator iterator(bytecode);
  for (; !iterator.done(); iterator.Advance()) {
    int current_offset = iterator.current_offset();
    valid_offsets.Add(current_offset);

    interpreter::Bytecode current_bytecode = iterator.current_bytecode();

    if (interpreter::Bytecodes::IsJump(current_bytecode)) {
      unsigned target_offset = iterator.GetJumpTargetOffset();
      Check(target_offset < bytecode_length, "Invalid jump offset");
      seen_jumps.Add(target_offset);
    } else if (interpreter::Bytecodes::IsSwitch(current_bytecode)) {
      for (const auto entry : iterator.GetJumpTableTargetOffsets()) {
        unsigned target_offset = entry.target_offset;
        Check(target_offset < bytecode_length, "Invalid switch offset");
        seen_jumps.Add(target_offset);
      }
    }
  }

  Check(seen_jumps.IsSubsetOf(valid_offsets), "Invalid control-flow");

  HandlerTable table(*bytecode);
  for (int i = 0; i < table.NumberOfRangeEntries(); ++i) {
    unsigned start = table.GetRangeStart(i);
    unsigned end = table.GetRangeEnd(i);
    unsigned handler = table.GetRangeHandler(i);
    Check(end <= bytecode_length && start <= end,
          "Invalid exception handler range");
    Check(handler < bytecode_length && valid_offsets.Contains(handler),
          "Invalid exception handler offset");
  }
}

// static
void BytecodeVerifier::VerifyFull(IsolateForSandbox isolate,
                                  Handle<BytecodeArray> bytecode, Zone* zone) {
  // VerifyFull does full verification and is for now just used during fuzzing
  // (to test the verification). However, in the future it may also (sometimes)
  // be enabled in production as well.

  // TODO(461681036): Implement this.
  VerifyLight(isolate, bytecode, zone);
}

}  // namespace v8::internal
