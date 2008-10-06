// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#include "codegen-inl.h"
#include "ic-inl.h"
#include "runtime.h"
#include "stub-cache.h"

namespace v8 { namespace internal {


// ----------------------------------------------------------------------------
// Static IC stub generators.
//

#define __ masm->


// Helper function used from LoadIC/CallIC GenerateNormal.
static void GenerateDictionaryLoad(MacroAssembler* masm,
                                   Label* done_label,
                                   Label* miss_label,
                                   Register t0,
                                   Register t1) {
  // Register use:
  //
  // t0 - used to hold the property dictionary.
  //
  // t1 - initially the receiver
  //    - used for the index into the property dictionary
  //    - holds the result on exit.
  //
  // r3 - used as temporary and to hold the capacity of the property
  //      dictionary.
  //
  // r2 - holds the name of the property and is unchanges.

  // Check for the absence of an interceptor.
  // Load the map into t0.
  __ ldr(t0, FieldMemOperand(t1, JSObject::kMapOffset));
  // Test the has_named_interceptor bit in the map.
  __ ldr(t0, FieldMemOperand(t1, Map::kInstanceAttributesOffset));
  __ tst(t0, Operand(1 << (Map::kHasNamedInterceptor + (3 * 8))));
  // Jump to miss if the interceptor bit is set.
  __ b(ne, miss_label);


  // Check that the properties array is a dictionary.
  __ ldr(t0, FieldMemOperand(t1, JSObject::kPropertiesOffset));
  __ ldr(r3, FieldMemOperand(t0, HeapObject::kMapOffset));
  __ cmp(r3, Operand(Factory::hash_table_map()));
  __ b(ne, miss_label);

  // Compute the capacity mask.
  const int kCapacityOffset =
      Array::kHeaderSize + Dictionary::kCapacityIndex * kPointerSize;
  __ ldr(r3, FieldMemOperand(t0, kCapacityOffset));
  __ mov(r3, Operand(r3, ASR, kSmiTagSize));  // convert smi to int
  __ sub(r3, r3, Operand(1));

  const int kElementsStartOffset =
      Array::kHeaderSize + Dictionary::kElementsStartIndex * kPointerSize;

  // Generate an unrolled loop that performs a few probes before
  // giving up. Measurements done on Gmail indicate that 2 probes
  // cover ~93% of loads from dictionaries.
  static const int kProbes = 4;
  for (int i = 0; i < kProbes; i++) {
    // Compute the masked index: (hash + i + i * i) & mask.
    __ ldr(t1, FieldMemOperand(r2, String::kLengthOffset));
    __ mov(t1, Operand(t1, LSR, String::kHashShift));
    if (i > 0) __ add(t1, t1, Operand(Dictionary::GetProbeOffset(i)));
    __ and_(t1, t1, Operand(r3));

    // Scale the index by multiplying by the element size.
    ASSERT(Dictionary::kElementSize == 3);
    __ add(t1, t1, Operand(t1, LSL, 1));  // t1 = t1 * 3

    // Check if the key is identical to the name.
    __ add(t1, t0, Operand(t1, LSL, 2));
    __ ldr(ip, FieldMemOperand(t1, kElementsStartOffset));
    __ cmp(r2, Operand(ip));
    if (i != kProbes - 1) {
      __ b(eq, done_label);
    } else {
      __ b(ne, miss_label);
    }
  }

  // Check that the value is a normal property.
  __ bind(done_label);  // t1 == t0 + 4*index
  __ ldr(r3, FieldMemOperand(t1, kElementsStartOffset + 2 * kPointerSize));
  __ tst(r3, Operand(PropertyDetails::TypeField::mask() << kSmiTagSize));
  __ b(ne, miss_label);

  // Get the value at the masked, scaled index and return.
  __ ldr(t1, FieldMemOperand(t1, kElementsStartOffset + 1 * kPointerSize));
}


void LoadIC::GenerateArrayLength(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  Label miss;

  __ ldr(r0, MemOperand(sp, 0));

  // Check that the receiver isn't a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the object is a JS array.
  __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
  __ ldrb(r1, FieldMemOperand(r1, Map::kInstanceTypeOffset));
  __ cmp(r1, Operand(JS_ARRAY_TYPE));
  __ b(ne, &miss);

  // Load length directly from the JS array.
  __ ldr(r0, FieldMemOperand(r0, JSArray::kLengthOffset));
  __ Ret();

  // Cache miss: Jump to runtime.
  __ bind(&miss);
  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Miss));
  __ Jump(ic, RelocInfo::CODE_TARGET);
}


void LoadIC::GenerateShortStringLength(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  Label miss;

  __ ldr(r0, MemOperand(sp, 0));

  // Check that the receiver isn't a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the object is a short string.
  __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
  __ ldrb(r1, FieldMemOperand(r1, Map::kInstanceTypeOffset));
  __ and_(r1, r1, Operand(kIsNotStringMask | kStringSizeMask));
  // The cast is to resolve the overload for the argument of 0x0.
  __ cmp(r1, Operand(static_cast<int32_t>(kStringTag | kShortStringTag)));
  __ b(ne, &miss);

  // Load length directly from the string.
  __ ldr(r0, FieldMemOperand(r0, String::kLengthOffset));
  __ mov(r0, Operand(r0, LSR, String::kShortLengthShift));
  __ mov(r0, Operand(r0, LSL, kSmiTagSize));
  __ Ret();

  // Cache miss: Jump to runtime.
  __ bind(&miss);
  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Miss));
  __ Jump(ic, RelocInfo::CODE_TARGET);
}


void LoadIC::GenerateMediumStringLength(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  Label miss;

  __ ldr(r0, MemOperand(sp, 0));

  // Check that the receiver isn't a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the object is a medium string.
  __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
  __ ldrb(r1, FieldMemOperand(r1, Map::kInstanceTypeOffset));
  __ and_(r1, r1, Operand(kIsNotStringMask | kStringSizeMask));
  __ cmp(r1, Operand(kStringTag | kMediumStringTag));
  __ b(ne, &miss);

  // Load length directly from the string.
  __ ldr(r0, FieldMemOperand(r0, String::kLengthOffset));
  __ mov(r0, Operand(r0, LSR, String::kMediumLengthShift));
  __ mov(r0, Operand(r0, LSL, kSmiTagSize));
  __ Ret();

  // Cache miss: Jump to runtime.
  __ bind(&miss);
  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Miss));
  __ Jump(ic, RelocInfo::CODE_TARGET);
}


void LoadIC::GenerateLongStringLength(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  Label miss;

  __ ldr(r0, MemOperand(sp, 0));
  // Check that the receiver isn't a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the object is a long string.
  __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
  __ ldrb(r1, FieldMemOperand(r1, Map::kInstanceTypeOffset));
  __ and_(r1, r1, Operand(kIsNotStringMask | kStringSizeMask));
  __ cmp(r1, Operand(kStringTag | kLongStringTag));
  __ b(ne, &miss);

  // Load length directly from the string.
  __ ldr(r0, FieldMemOperand(r0, String::kLengthOffset));
  __ mov(r0, Operand(r0, LSR, String::kLongLengthShift));
  __ mov(r0, Operand(r0, LSL, kSmiTagSize));
  __ Ret();

  // Cache miss: Jump to runtime.
  __ bind(&miss);
  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Miss));
  __ Jump(ic, RelocInfo::CODE_TARGET);
}


void LoadIC::GenerateFunctionPrototype(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  // NOTE: Right now, this code always misses on ARM which is
  // sub-optimal. We should port the fast case code from IA-32.

  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Miss));
  __ Jump(ic, RelocInfo::CODE_TARGET);
}


// Defined in ic.cc.
Object* CallIC_Miss(Arguments args);

void CallIC::GenerateMegamorphic(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- lr: return address
  // -----------------------------------
  Label number, non_number, non_string, boolean, probe, miss;

  // Get the receiver of the function from the stack into r1.
  __ ldr(r1, MemOperand(sp, argc * kPointerSize));
  // Get the name of the function from the stack; 1 ~ receiver.
  __ ldr(r2, MemOperand(sp, (argc + 1) * kPointerSize));

  // Probe the stub cache.
  Code::Flags flags =
      Code::ComputeFlags(Code::CALL_IC, MONOMORPHIC, NORMAL, argc);
  StubCache::GenerateProbe(masm, flags, r1, r2, r3);

  // If the stub cache probing failed, the receiver might be a value.
  // For value objects, we use the map of the prototype objects for
  // the corresponding JSValue for the cache and that is what we need
  // to probe.
  //
  // Check for number.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &number);
  __ ldr(r3, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ ldrb(r3, FieldMemOperand(r3, Map::kInstanceTypeOffset));
  __ cmp(r3, Operand(HEAP_NUMBER_TYPE));
  __ b(ne, &non_number);
  __ bind(&number);
  StubCompiler::GenerateLoadGlobalFunctionPrototype(
      masm, Context::NUMBER_FUNCTION_INDEX, r1);
  __ b(&probe);

  // Check for string.
  __ bind(&non_number);
  __ cmp(r3, Operand(FIRST_NONSTRING_TYPE));
  __ b(hs, &non_string);
  StubCompiler::GenerateLoadGlobalFunctionPrototype(
      masm, Context::STRING_FUNCTION_INDEX, r1);
  __ b(&probe);

  // Check for boolean.
  __ bind(&non_string);
  __ cmp(r1, Operand(Factory::true_value()));
  __ b(eq, &boolean);
  __ cmp(r1, Operand(Factory::false_value()));
  __ b(ne, &miss);
  __ bind(&boolean);
  StubCompiler::GenerateLoadGlobalFunctionPrototype(
      masm, Context::BOOLEAN_FUNCTION_INDEX, r1);

  // Probe the stub cache for the value object.
  __ bind(&probe);
  StubCache::GenerateProbe(masm, flags, r1, r2, r3);

  // Cache miss: Jump to runtime.
  __ bind(&miss);
  Generate(masm, argc, ExternalReference(IC_Utility(kCallIC_Miss)));
}


void CallIC::GenerateNormal(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- lr: return address
  // -----------------------------------

  Label miss, probe, done, global;

  // Get the receiver of the function from the stack into r1.
  __ ldr(r1, MemOperand(sp, argc * kPointerSize));
  // Get the name of the function from the stack; 1 ~ receiver.
  __ ldr(r2, MemOperand(sp, (argc + 1) * kPointerSize));

  // Check that the receiver isn't a smi.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the receiver is a valid JS object.
  __ ldr(r0, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ ldrb(r0, FieldMemOperand(r0, Map::kInstanceTypeOffset));
  __ cmp(r0, Operand(FIRST_JS_OBJECT_TYPE));
  __ b(lt, &miss);

  // If this assert fails, we have to check upper bound too.
  ASSERT(LAST_TYPE == JS_FUNCTION_TYPE);

  // Check for access to global object (unlikely).
  __ cmp(r0, Operand(JS_GLOBAL_OBJECT_TYPE));
  __ b(eq, &global);

  // Search the dictionary placing the result in r1.
  __ bind(&probe);
  GenerateDictionaryLoad(masm, &done, &miss, r0, r1);

  // Check that the value isn't a smi.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the value is a JSFunction.
  __ ldr(r0, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ ldrb(r0, FieldMemOperand(r0, Map::kInstanceTypeOffset));
  __ cmp(r0, Operand(JS_FUNCTION_TYPE));
  __ b(ne, &miss);

  // Patch the function on the stack; 1 ~ receiver.
  __ str(r1, MemOperand(sp, (argc + 1) * kPointerSize));

  // Invoke the function.
  ParameterCount actual(argc);
  __ InvokeFunction(r1, actual, JUMP_FUNCTION);

  // Global object access: Check access rights.
  __ bind(&global);
  __ CheckAccessGlobal(r1, r0, &miss);
  __ b(&probe);

  // Cache miss: Jump to runtime.
  __ bind(&miss);
  Generate(masm, argc, ExternalReference(IC_Utility(kCallIC_Miss)));
}


void CallIC::Generate(MacroAssembler* masm,
                      int argc,
                      const ExternalReference& f) {
  // ----------- S t a t e -------------
  //  -- lr: return address
  // -----------------------------------

  // Get the receiver of the function from the stack.
  __ ldr(r2, MemOperand(sp, argc * kPointerSize));
  // Get the name of the function to call from the stack.
  __ ldr(r1, MemOperand(sp, (argc + 1) * kPointerSize));

  __ EnterInternalFrame();

  // Push the receiver and the name of the function.
  __ stm(db_w, sp, r1.bit() | r2.bit());

  // Call the entry.
  __ mov(r0, Operand(2));
  __ mov(r1, Operand(f));

  CEntryStub stub;
  __ CallStub(&stub);

  // Move result to r1.
  __ mov(r1, Operand(r0));

  __ LeaveInternalFrame();

  // Patch the function on the stack; 1 ~ receiver.
  __ str(r1, MemOperand(sp, (argc + 1) * kPointerSize));

  // Invoke the function.
  ParameterCount actual(argc);
  __ InvokeFunction(r1, actual, JUMP_FUNCTION);
}


// Defined in ic.cc.
Object* LoadIC_Miss(Arguments args);

void LoadIC::GenerateMegamorphic(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  __ ldr(r0, MemOperand(sp, 0));
  // Probe the stub cache.
  Code::Flags flags = Code::ComputeFlags(Code::LOAD_IC, MONOMORPHIC);
  StubCache::GenerateProbe(masm, flags, r0, r2, r3);

  // Cache miss: Jump to runtime.
  Generate(masm, ExternalReference(IC_Utility(kLoadIC_Miss)));
}


void LoadIC::GenerateNormal(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  Label miss, probe, done, global;

  __ ldr(r0, MemOperand(sp, 0));
  // Check that the receiver isn't a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the receiver is a valid JS object.
  __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
  __ ldrb(r1, FieldMemOperand(r1, Map::kInstanceTypeOffset));
  __ cmp(r1, Operand(FIRST_JS_OBJECT_TYPE));
  __ b(lt, &miss);
  // If this assert fails, we have to check upper bound too.
  ASSERT(LAST_TYPE == JS_FUNCTION_TYPE);

  // Check for access to global object (unlikely).
  __ cmp(r1, Operand(JS_GLOBAL_OBJECT_TYPE));
  __ b(eq, &global);


  __ bind(&probe);
  GenerateDictionaryLoad(masm, &done, &miss, r1, r0);
  __ Ret();

  // Global object access: Check access rights.
  __ bind(&global);
  __ CheckAccessGlobal(r0, r1, &miss);
  __ b(&probe);

  // Cache miss: Restore receiver from stack and jump to runtime.
  __ bind(&miss);
  Generate(masm, ExternalReference(IC_Utility(kLoadIC_Miss)));
}


void LoadIC::GenerateMiss(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kLoadIC_Miss)));
}


void LoadIC::Generate(MacroAssembler* masm, const ExternalReference& f) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  __ ldr(r0, MemOperand(sp, 0));
  __ push(r0);
  __ push(r2);

  // Perform tail call to the entry.
  __ TailCallRuntime(f, 2);
}


// TODO(1224671): ICs for keyed load/store is not implemented on ARM.
void KeyedLoadIC::GenerateMiss(MacroAssembler* masm) {
}

void KeyedLoadIC::Generate(MacroAssembler* masm, const ExternalReference& f) {
}

void KeyedLoadIC::GenerateGeneric(MacroAssembler* masm) {
}

void KeyedStoreIC::Generate(MacroAssembler* masm,
                            const ExternalReference& f) {
}

void KeyedStoreIC::GenerateGeneric(MacroAssembler* masm) {
}


void StoreIC::GenerateMegamorphic(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r0    : value
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  // Get the receiver from the stack and probe the stub cache.
  __ ldr(r1, MemOperand(sp));
  Code::Flags flags = Code::ComputeFlags(Code::STORE_IC, MONOMORPHIC);
  StubCache::GenerateProbe(masm, flags, r1, r2, r3);

  // Cache miss: Jump to runtime.
  Generate(masm, ExternalReference(IC_Utility(kStoreIC_Miss)));
}


void StoreIC::GenerateExtendStorage(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r0    : value
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  __ ldr(r3, MemOperand(sp));  // copy receiver
  __ stm(db_w, sp, r0.bit() | r2.bit() | r3.bit());

  // Perform tail call to the entry.
  __ TailCallRuntime(ExternalReference(IC_Utility(kStoreIC_ExtendStorage)), 3);
}


void StoreIC::Generate(MacroAssembler* masm, const ExternalReference& f) {
  // ----------- S t a t e -------------
  //  -- r0    : value
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  __ ldr(r3, MemOperand(sp));  // copy receiver
  __ stm(db_w, sp, r0.bit() | r2.bit() | r3.bit());

  // Perform tail call to the entry.
  __ TailCallRuntime(f, 3);
}


#undef __


} }  // namespace v8::internal
