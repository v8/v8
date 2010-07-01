// Copyright 2008 the V8 project authors. All rights reserved.
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

#if defined(V8_TARGET_ARCH_IA32)

#include "codegen-inl.h"
#include "register-allocator-inl.h"
#include "virtual-frame-inl.h"

namespace v8 {
namespace internal {

// -------------------------------------------------------------------------
// Result implementation.

void Result::ToRegister() {
  ASSERT(is_valid());
  if (is_constant()) {
    CodeGenerator* code_generator =
        CodeGeneratorScope::Current(Isolate::Current());
    Result fresh = code_generator->allocator()->Allocate();
    ASSERT(fresh.is_valid());
    if (is_untagged_int32()) {
      fresh.set_untagged_int32(true);
      if (handle()->IsSmi()) {
      code_generator->masm()->Set(
          fresh.reg(),
          Immediate(Smi::cast(*handle())->value()));
      } else if (handle()->IsHeapNumber()) {
        double double_value = HeapNumber::cast(*handle())->value();
        int32_t value = DoubleToInt32(double_value);
        if (double_value == 0 && signbit(double_value)) {
          // Negative zero must not be converted to an int32 unless
          // the context allows it.
          code_generator->unsafe_bailout_->Branch(equal);
          code_generator->unsafe_bailout_->Branch(not_equal);
        } else if (double_value == value) {
          code_generator->masm()->Set(fresh.reg(), Immediate(value));
        } else {
          code_generator->unsafe_bailout_->Branch(equal);
          code_generator->unsafe_bailout_->Branch(not_equal);
        }
      } else {
        // Constant is not a number.  This was not predicted by AST analysis.
        code_generator->unsafe_bailout_->Branch(equal);
        code_generator->unsafe_bailout_->Branch(not_equal);
      }
    } else if (code_generator->IsUnsafeSmi(handle())) {
      code_generator->MoveUnsafeSmi(fresh.reg(), handle());
    } else {
      code_generator->masm()->Set(fresh.reg(), Immediate(handle()));
    }
    // This result becomes a copy of the fresh one.
    fresh.set_type_info(type_info());
    *this = fresh;
  }
  ASSERT(is_register());
}


void Result::ToRegister(Register target) {
  CodeGenerator* code_generator =
      CodeGeneratorScope::Current(Isolate::Current());
  ASSERT(is_valid());
  if (!is_register() || !reg().is(target)) {
    Result fresh = code_generator->allocator()->Allocate(target);
    ASSERT(fresh.is_valid());
    if (is_register()) {
      code_generator->masm()->mov(fresh.reg(), reg());
    } else {
      ASSERT(is_constant());
      if (is_untagged_int32()) {
        if (handle()->IsSmi()) {
          code_generator->masm()->Set(
              fresh.reg(),
              Immediate(Smi::cast(*handle())->value()));
        } else {
          ASSERT(handle()->IsHeapNumber());
          double double_value = HeapNumber::cast(*handle())->value();
          int32_t value = DoubleToInt32(double_value);
          if (double_value == 0 && signbit(double_value)) {
            // Negative zero must not be converted to an int32 unless
            // the context allows it.
            code_generator->unsafe_bailout_->Branch(equal);
            code_generator->unsafe_bailout_->Branch(not_equal);
          } else if (double_value == value) {
            code_generator->masm()->Set(fresh.reg(), Immediate(value));
          } else {
            code_generator->unsafe_bailout_->Branch(equal);
            code_generator->unsafe_bailout_->Branch(not_equal);
          }
        }
      } else {
        if (code_generator->IsUnsafeSmi(handle())) {
          code_generator->MoveUnsafeSmi(fresh.reg(), handle());
        } else {
          code_generator->masm()->Set(fresh.reg(), Immediate(handle()));
        }
      }
    }
    fresh.set_type_info(type_info());
    fresh.set_untagged_int32(is_untagged_int32());
    *this = fresh;
  } else if (is_register() && reg().is(target)) {
    ASSERT(code_generator->has_valid_frame());
    code_generator->frame()->Spill(target);
    ASSERT(code_generator->allocator()->count(target) == 1);
  }
  ASSERT(is_register());
  ASSERT(reg().is(target));
}


// -------------------------------------------------------------------------
// RegisterAllocator implementation.

Result RegisterAllocator::AllocateByteRegisterWithoutSpilling() {
  Result result = AllocateWithoutSpilling();
  // Check that the register is a byte register.  If not, unuse the
  // register if valid and return an invalid result.
  if (result.is_valid() && !result.reg().is_byte_register()) {
    result.Unuse();
    return Result();
  }
  return result;
}


} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_IA32
