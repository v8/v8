// Copyright 2006-2008 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "frames-inl.h"
#include "assembler-arm-inl.h"


namespace v8 { namespace internal {


StackFrame::Type StackFrame::ComputeType(State* state) {
  ASSERT(state->fp != NULL);
  if (state->pp == NULL) {
    if (Memory::Address_at(state->fp +
                           EntryFrameConstants::kConstructMarkOffset) != 0) {
      return ENTRY_CONSTRUCT;
    } else {
      return ENTRY;
    }
  } else if (StandardFrame::IsArgumentsAdaptorFrame(state->fp)) {
    return ARGUMENTS_ADAPTOR;
  } else if (
      Memory::Object_at(state->fp +
                        StandardFrameConstants::kFunctionOffset)->IsSmi()) {
    return INTERNAL;
  } else {
    return JAVA_SCRIPT;
  }
}


StackFrame::Type ExitFrame::GetStateForFramePointer(Address fp, State* state) {
  if (fp == 0) return NONE;
  // Compute frame type and stack pointer.
  Address sp = fp + ExitFrameConstants::kSPDisplacement;
  Type type;
  if (Memory::Address_at(fp + ExitFrameConstants::kDebugMarkOffset) != 0) {
    type = EXIT_DEBUG;
    sp -= kNumJSCallerSaved * kPointerSize;
  } else {
    type = EXIT;
  }
  // Fill in the state.
  state->sp = sp;
  state->fp = fp;
  state->pp = fp + ExitFrameConstants::kPPDisplacement;
  state->pc_address = reinterpret_cast<Address*>(sp - 1 * kPointerSize);
  return type;
}


void ExitFrame::Iterate(ObjectVisitor* v) const {
  // Traverse pointers in the callee-saved registers.
  const int offset = ExitFrameConstants::kSavedRegistersOffset;
  Object** base = &Memory::Object_at(fp() + offset);
  Object** limit = base + kNumJSCalleeSaved;
  v->VisitPointers(base, limit);
}


void ExitFrame::RestoreCalleeSavedRegisters(Object* buffer[]) const {
  // The callee-saved registers in an exit frame are pointed to by the
  // frame pointer. See the implementations of C entry runtime stubs.
  const int offset = ExitFrameConstants::kSavedRegistersOffset;
  memcpy(buffer, fp() + offset, kNumJSCalleeSaved * kPointerSize);
}


int JavaScriptFrame::GetProvidedParametersCount() const {
  const int offset = JavaScriptFrameConstants::kArgsLengthOffset;
  int result = Memory::int_at(fp() + offset);
  // We never remove extra parameters provided on the stack; we only
  // fill in undefined values for parameters not provided.
  ASSERT(0 <= result && result <= ComputeParametersCount());
  return result;
}


Address JavaScriptFrame::GetCallerStackPointer() const {
  return state_.pp;
}


Address ArgumentsAdaptorFrame::GetCallerStackPointer() const {
  // Argument adaptor frames aren't used on ARM (yet).
  UNIMPLEMENTED();
  return 0;
}


Address InternalFrame::GetCallerStackPointer() const {
  return state_.pp;
}


RegList JavaScriptFrame::FindCalleeSavedRegisters() const {
  const unsigned kRegListTag = 1;  // pc values have bit 0 cleared (no thumb)
  const unsigned kRegListTagSize = 1;
  const unsigned kRegListTagMask = (1 << kRegListTagSize) - 1;

  // The prologue pc (or the cached register list) is available as a
  // slot in the fixed part of the stack frame.
  const int offset = +4 * kPointerSize;

  // Once the register list has been calculated for a frame, it is
  // cached in the prologue pc stack slot. Check the cache before
  // doing the more expensive instruction decoding.
  uint32_t cache = Memory::int_at(fp() + offset);
  if ((cache & kRegListTagMask) == kRegListTag) {
    return static_cast<RegList>(cache >> kRegListTagSize);
  }

  // If we can't find the register list in the instruction stream, we
  // assume it's the empty list. [NOTE: Is this really a smart thing
  // to do?  Don't all JavaScript frames have the instruction?]
  RegList result = 0;

  // Compute the address of the stm (store multiple) instruction.
  Address stm_address = AddressFrom<Address>(cache - PcStoreOffset());
  ASSERT((Memory::int32_at(stm_address) & 0xffffcc00) == 0xe92dcc00);

  // Fetch the instruction preceeding the stm - if it is also a stm
  // instruction we read the register list from there.
  uint32_t instruction = Memory::int32_at(stm_address - 4);
  if ((instruction & 0xfffffc00) == 0xe92d0000) {
    // The register list shouldn't be empty and must consist only of JS
    // callee-saved registers.
    result = instruction & 0xffff;
    ASSERT(result != 0 && (result & ~kJSCalleeSaved) == 0);
  }

  // Cache the result in the prologue pc stack slot before returning
  // it. This way future access to the register list is a bit faster.
  Memory::int_at(fp() + offset) = (result << kRegListTagSize) | kRegListTag;
  return result;
}


void JavaScriptFrame::RestoreCalleeSavedRegisters(Object* buffer[]) const {
  // The callee-saved registers in java script frames are in the fixed
  // part of the frame below the frame pointer.
  const int n = NumRegs(FindCalleeSavedRegisters());
  const int offset = 5 * kPointerSize;
  memcpy(buffer, fp() + offset, n * kPointerSize);
}


Code* JavaScriptFrame::FindCode() const {
  const int offset = StandardFrameConstants::kCodeOffset;
  Object* code = Memory::Object_at(fp() + offset);
  if (code == NULL) {
    // The code object isn't set; find it and set it.
    code = Heap::FindCodeObject(pc());
    ASSERT(!code->IsFailure());
    Memory::Object_at(fp() + offset) = code;
  }
  ASSERT(code != NULL);
  return Code::cast(code);
}


} }  // namespace v8::internal
