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
#include "regexp-macro-assembler-ia32.h"

namespace v8 { namespace internal {

/*
 * This assembler uses the following register assignment convention
 * - edx : current character, or kEndOfInput if current position is not
 *         inside string. The kEndOfInput value is greater than 0xffff,
 *         so any tests that don't check whether the current position
 *         is inside the correct range should retain bits above the
 *         15th in their computations, and fail if the value is too
 *         great.
 * - edi : current position in input.
 * - esi : end of input (points to byte after last character in input).
 * - ebp : points to the location above the registers on the stack,
 *         as if by the "enter <register_count>" opcode.
 * - esp : points to tip of backtracking stack.
 *
 * The registers eax, ebx and eax are free to use for computations.
 *
 * Each call to a public method should retain this convention.
 * The stack is expected to have the following structure (tentative):
 *
 *       - pointer to array where captures can be stored
 *       - end of input
 *       - start of input
 *       - return address
 * ebp-> - old ebp
 *       - register 0  ebp[-4]
 *       - register 1  ebp[-8]
 *       - ...
 *
 * The data before ebp must be placed there by the calling code.
 */

RegExpMacroAssemblerIA32::RegExpMacroAssemblerIA32()
 : masm_(new MacroAssembler(NULL, kRegExpCodeSize)),
   constants_(kRegExpConstantsSize),
   num_registers_(0),
   ignore_case(false) {}


RegExpMacroAssemblerIA32::~RegExpMacroAssemblerIA32() {
  delete masm_;
}


#define __ masm_->

void RegExpMacroAssemblerIA32::AdvanceCurrentPosition(int by) {
  __ add(edi, by * sizeof(SubjectChar));
  __ cmp(edi, esi);
  Label inside_string;
  Backtrack();

  __ bind(&inside_string);
  ReadChar(edx, 0);
}


void RegExpMacroAssemblerIA32::AdvanceRegister(int reg, int by) {
  ASSERT(reg >= 0);
  ASSERT(reg < num_registers);
  __ add(register_location(reg), by);
}


void RegExpMacroAssemblerIA32::Backtrack() {
  __ ret();
}


void RegExpMacroAssemblerIA32::Bind(Label* label) {
  __ bind(label);
}


void RegExpMacroAssemblerIA32::CheckBitmap(uc16 start,
                                           Label* bitmap,
                                           Label* on_zero) {
  ReadCurrentChar(eax);
  __ sub(eax, start);
  __ cmp(eax, 64); // FIXME: 64 = length_of_bitmap_in_bits.
  BranchOrBacktrack(greater_equal, on_zero);
  __ mov(ebx, eax);
  __ shr(ebx, 3);
  // TODO: Where is the bitmap stored? Pass the bitmap as argument instead.
  // __ mov(ecx, position_of_bitmap);
  __ movzx_b(ebx, Operand(ecx, ebx, times_1, 0));
  __ and_(eax, (1<<3)-1);
  __ bt(ebx, eax);
  __ j(greater_equal, on_zero);  // Aka. jump on carry set.
}


void RegExpMacroAssemblerIA32::CheckCharacterClass(RegExpCharacterClass *cclass,
                                                   Label* on_failure) {
  UNREACHABLE();  // Not implemented.
}


void RegExpMacroAssemblerIA32::CheckCharacters(Vector<uc16> str,
                                               Label* on_failure) {
  if (sizeof(SubjectChar) == 1) {
    for (int i = 0; i < str.length(); i++) {
      if (str[i] > String::kMaxAsciiCharCode) {
        __ jmp(on_failure);
        return;
      }
    }
  }
  int byte_length = str.length() * sizeof(SubjectChar);
  __ mov(ebx, edi);
  __ add(ebx, byte_length);
  __ cmp(ebx, esi);
  BranchOrBacktrack(greater_equal, on_failure);

  if (str.length() <= kMaxInlineStringTests || ignore_case()) {
    // TODO: make proper loop if str.length is large but ignore_case is true;
    for(int i = 0; i < str.length(); i++) {
      ReadChar(eax, i);
      if (ignore_case()) {
        Canonicalize(eax);
      }
      __ cmp(eax, str[i]);
      BranchOrBacktrack(not_equal, on_failure);
    }
    add(edi, byte_length);
  } else {
    int offset;
    ArraySlice<SubjectChar> constant_buffer =
        constants_.GetBuffer<SubjectChar>(str.length());
    for (int i = 0; i < str.length(); i++) {
      constant_buffer[i] = str[i];
    }
    __ mov(ebx, esi);
    LoadConstantBufferAddress(esi, constant_buffer);
    __ mov(ecx, str.length());
    if (sizeof(SubjectChar) == 1) {
      __ rep_cmpsb();
    } else {
      ASSERT(sizeof(SubjectChar)==2);
      __ rep_cmpsw();
    }
    __ mov(esi, ebx);
    BranchOrBacktrack(not_equal, on_failure);
  }
};


void RegExpMacroAssemblerIA32::CheckCurrentPosition(int register_index,
                                                    Label* on_equal) {
  __ cmp(register_location(register_index), edi);
  BranchOrBacktrack(equal, on_equal);
}


void RegExpMacroAssemblerIA32::DispatchHalfNibbleMap(
    uc16 start,
    Label* half_nibble_map,
    const Vector<Label*>& destinations) {

  if (sizeof(SubjectChar) == 1 && start > String::kMaxAsciiCharCode) {
    return;
  }

  Label fallthrough;

  ReadCurrentChar(eax);
  __ sub(eax, start);
  __ cmp(eax, 64);  // FIXME: 64 = size of map in bytes. Found somehow??
  __ j(greater_equal, &fallthrough);

  __ mov(ebx, eax);
  __ shr(eax, 2);
  __ movzx_b(eax, Operand(ecx, eax)); // FIXME: ecx holds address of map
  Label got_nybble;
  Label high_bits;
  __ and_(ebx, 0x03);
  __ shr(eax, ebx);

  Label second_bit_set, case_3, case_1;
  __ test(eax, 2);
  __ j(not_equal, &second_bit_set);
  __ test(eax, 1);
  __ j(not_equal, &case_1);
  // Case 0:
  __ jmp(&destinations[0]);
  __ bind(&case_1);
  // Case 1:
  __ jmp(&destinations[1]);
  __ bind(&second_bit_set);
  __ test(eax, 1);
  __ j(not_equal, &case_3);
  // Case 2
  __ jmp(&destinations[2]);
  __ bind(&case_3);
  // Case 3:
  __ jmp(&destinations[3]);

  __ bind(&fallthrough);
}


void RegExpMacroAssemblerIA32::DispatchByteMap(
    uc16 start,
    Label* byte_map,
    const Vector<Label*>& destinations) {

  if (sizeof(SubjectChar) == 1 && start > String::kMaxAsciiCharCode) {
    return;
  }

  Label fallthrough;

  ReadCurrentChar(eax);
  __ sub(eax, start);
  __ cmp(eax, 64);  // FIXME: 64 = size of map. Found somehow??
  __ j(greater_equal, &fallthrough);

  __ movzx_b(eax, Operand(ecx, eax));  // FIXME: ecx must hold address of map
  // jump table: jump to destinations[eax];

  __ bind(&fallthrough);
}

void RegExpMacroAssemblerIA32::DispatchHighByteMap(
    byte start,
    Label* byte_map,
    const Vector<Label*>& destinations) {
  Label fallthrough;
  ReadCurrentChar(eax);
  __ shr(eax, 8);
  __ sub(eax, start);
  __ cmp(eax, destinations.length() - start);
  __ j(greater_equal, &fallthrough);

  // TODO jumptable: jump to destinations[eax]
  __ bind(&fallthrough);
}


void RegExpMacroAssemblerIA32::EmitOrLink(Label* label) {
  UNREACHABLE();  // Has no use.
}


void RegExpMacroAssemblerIA32::Fail() {
  Exit(false);
}

Handle<Object> RegExpMacroAssemblerIA32::GetCode() {
  // something
  return Handle();
}


void RegExpMacroAssemblerIA32::GoTo(Label &to) {
  __ jmp(to);
}


void RegExpMacroAssemblerIA32::IfRegisterGE(int reg,
                                            int comparand,
                                            Label* if_ge) {
  __ cmp(register_location(reg), comparand);
  BranchOrBacktrack(greater_equal, if_ge);
}


void RegExpMacroAssemblerIA32::IfRegisterLT(int reg,
                                            int comparand,
                                            Label* if_lt) {
  __ cmp(register_location(reg), comparand);
  BranchOrBacktrack(less, if_lt);
}


Re2kImplementation RegExpMacroAssemblerIA32::Implementation() {
  return kIA32Implementation;
}


void RegExpMacroAssemblerIA32::PopCurrentPosition() {
  __ pop(edi);
  ReadChar(edx, 0);
}


void RegExpMacroAssemblerIA32::PopRegister(int register_index) {
  __ pop(register_location(register_index));
}


void RegExpMacroAssemblerIA32::PushBacktrack(Label* label) {
  Label cont;
  __ call(&cont);
  __ jmp(label);
  __ bind(&cont);
}


void RegExpMacroAssemblerIA32::PushCurrentPosition() {
  __ push(edi);
}


void RegExpMacroAssemblerIA32::PushRegister(int register_index) {
  __ push(register_location(register_index));
}


void RegExpMacroAssemblerIA32::SetRegister(int register_index, int to) {
  __ mov(register_location(register_index), to);
}


void RegExpMacroAssemblerIA32::Succeed() {
  Exit(true);
}

void RegExpMacroAssemblerIA32::WriteCurrentPositionToRegister() {
  __ mov(register_location(register_index), edi);
}

// Custom :

void RegExpMacroAssemblerIA32::Initialize(int num_registers, bool ignore_case) {
  num_registers_ = num_registers;
  ignore_case_ = ignore_case;
  __ enter(num_registers * sizeof(uint32_t));
}


Operand RegExpMacroAssemblerIA32::register_location(int register_index) {
  ASSERT(register_index < (1<<30));
  return Operand(ebp, -((register_index + 1) * sizeof(uint32_t)));
}


void RegExpMacroAssemblerIA32::BranchOrBacktrack(Condition condition,
                                                 Label* to) {
  if (condition < 0) {  // No condition
    if (to == NULL) {
      Backtrack();
      return;
    }
    __ jmp(to);
    return;
  } else if (to == NULL) {
    Label skip;
    __ j(NegateCondition(condition), &skip);
    Backtrack();
    __ bind(&skip);
    return;
  }
  __ j(condition, to);
}


void RegExpMacroAssemblerIA32::Canonicalize(Register reg) {
  if (sizeof(SubjectChar) == 1) {
    Label end;
    __ cmp(reg, 'a');
    __ j(below, &end);
    __ cmp(reg, 'z');
    __ j(above, &end);
    __ sub(reg, 'a' - 'A');
    __ bind(&end);
    return;
  }
  ASSERT(sizeof(SubjectChar) == 2);
  // TODO: Use some tables.
}


void RegExpMacroAssemblerIA32::Exit(bool success) {
  if (success) {
    // Copy captures to output capture array.
  }
  __ leave();
  __ mov(eax, success ? 1 : 0);
  __ ret();
}


void RegExpMacroAssemblerIA32::ReadChar(Register destination, int offset) {
  if (sizeof(SubjectChar) == 1) {
    __ movzx_b(destination, Operand(edi, offset));
    return;
  }
  ASSERT(sizeof(SubjectChar) == 2);
  __ movzx_w(destination, Operand(edi, offset * 2));
}


void RegExpMacroAssemblerIA32::ReadCurrentChar(Register destination) {
  mov(destination, edx);
}


template <typename T>
void LoadConstantBufferAddress(Register reg, ArraySlice<T>& buffer) {
  __ mov(reg, buffer.array());
  __ add(reg, buffer.base_offset());
}

#undef __
}}
