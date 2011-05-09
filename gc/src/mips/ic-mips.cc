// Copyright 2010 the V8 project authors. All rights reserved.
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

#if defined(V8_TARGET_ARCH_MIPS)

#include "codegen-inl.h"
#include "code-stubs.h"
#include "ic-inl.h"
#include "runtime.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {


// ----------------------------------------------------------------------------
// Static IC stub generators.
//

#define __ ACCESS_MASM(masm)


void LoadIC::GenerateArrayLength(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void LoadIC::GenerateStringLength(MacroAssembler* masm, bool support_wrappers) {
  UNIMPLEMENTED_MIPS();
}


void LoadIC::GenerateFunctionPrototype(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


// Defined in ic.cc.
Object* CallIC_Miss(Arguments args);


void CallIC::GenerateMiss(MacroAssembler* masm, int argc) {
  UNIMPLEMENTED_MIPS();
}


void CallIC::GenerateMegamorphic(MacroAssembler* masm, int argc) {
  UNIMPLEMENTED_MIPS();
}


void CallIC::GenerateNormal(MacroAssembler* masm, int argc) {
  UNIMPLEMENTED_MIPS();
}


void KeyedCallIC::GenerateMiss(MacroAssembler* masm, int argc) {
  UNIMPLEMENTED_MIPS();
}


void KeyedCallIC::GenerateMegamorphic(MacroAssembler* masm, int argc) {
  UNIMPLEMENTED_MIPS();
}


void KeyedCallIC::GenerateNormal(MacroAssembler* masm, int argc) {
  UNIMPLEMENTED_MIPS();
}


// Defined in ic.cc.
Object* LoadIC_Miss(Arguments args);

void LoadIC::GenerateMegamorphic(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void LoadIC::GenerateNormal(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void LoadIC::GenerateMiss(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


bool LoadIC::PatchInlinedLoad(Address address, Object* map, int offset) {
  UNIMPLEMENTED_MIPS();
  return false;
}


bool LoadIC::PatchInlinedContextualLoad(Address address,
                                        Object* map,
                                        Object* cell,
                                        bool is_dont_delete) {
  UNIMPLEMENTED_MIPS();
  return false;
}


bool StoreIC::PatchInlinedStore(Address address, Object* map, int offset) {
  UNIMPLEMENTED_MIPS();
  return false;
}


bool KeyedLoadIC::PatchInlinedLoad(Address address, Object* map) {
  UNIMPLEMENTED_MIPS();
  return false;
}


bool KeyedStoreIC::PatchInlinedStore(Address address, Object* map) {
  UNIMPLEMENTED_MIPS();
  return false;
}


Object* KeyedLoadIC_Miss(Arguments args);


void KeyedLoadIC::GenerateMiss(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void KeyedLoadIC::GenerateRuntimeGetProperty(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void KeyedLoadIC::GenerateGeneric(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void KeyedLoadIC::GenerateString(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void KeyedStoreIC::GenerateRuntimeSetProperty(MacroAssembler* masm,
                                              StrictModeFlag strict_mode) {
  UNIMPLEMENTED_MIPS();
}


void KeyedStoreIC::GenerateGeneric(MacroAssembler* masm,
                                   StrictModeFlag strict_mode) {
  UNIMPLEMENTED_MIPS();
}


void KeyedLoadIC::GenerateIndexedInterceptor(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void KeyedStoreIC::GenerateMiss(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StoreIC::GenerateMegamorphic(MacroAssembler* masm,
                                  StrictModeFlag strict_mode) {
  UNIMPLEMENTED_MIPS();
}


void StoreIC::GenerateMiss(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StoreIC::GenerateArrayLength(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StoreIC::GenerateNormal(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StoreIC::GenerateGlobalProxy(MacroAssembler* masm,
                                  StrictModeFlag strict_mode) {
  UNIMPLEMENTED_MIPS();
}


#undef __


Condition CompareIC::ComputeCondition(Token::Value op) {
  UNIMPLEMENTED_MIPS();
  return kNoCondition;
}


void CompareIC::UpdateCaches(Handle<Object> x, Handle<Object> y) {
  UNIMPLEMENTED_MIPS();
}


void PatchInlinedSmiCode(Address address) {
  // Currently there is no smi inlining in the MIPS full code generator.
}


} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_MIPS
