// Copyright 2013 the V8 project authors. All rights reserved.
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

#if V8_TARGET_ARCH_A64

#include "ic-inl.h"
#include "codegen.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {


#define __ ACCESS_MASM(masm)


// Helper function used to check that the dictionary doesn't contain
// the property. This function may return false negatives, so miss_label
// must always call a backup property check that is complete.
// This function is safe to call if the receiver has fast properties.
// Name must be unique and receiver must be a heap object.
static void GenerateDictionaryNegativeLookup(MacroAssembler* masm,
                                             Label* miss_label,
                                             Register receiver,
                                             Handle<Name> name,
                                             Register scratch0,
                                             Register scratch1) {
  ASSERT(!AreAliased(scratch0, scratch1));
  ASSERT(name->IsUniqueName());
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->negative_lookups(), 1, scratch0, scratch1);
  __ IncrementCounter(counters->negative_lookups_miss(), 1, scratch0, scratch1);

  Label done;

  const int kInterceptorOrAccessCheckNeededMask =
      (1 << Map::kHasNamedInterceptor) | (1 << Map::kIsAccessCheckNeeded);

  // Bail out if the receiver has a named interceptor or requires access checks.
  Register map = scratch1;
  __ Ldr(map, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ Ldrb(scratch0, FieldMemOperand(map, Map::kBitFieldOffset));
  __ Tst(scratch0, kInterceptorOrAccessCheckNeededMask);
  __ B(ne, miss_label);

  // Check that receiver is a JSObject.
  __ Ldrb(scratch0, FieldMemOperand(map, Map::kInstanceTypeOffset));
  __ Cmp(scratch0, FIRST_SPEC_OBJECT_TYPE);
  __ B(lt, miss_label);

  // Load properties array.
  Register properties = scratch0;
  __ Ldr(properties, FieldMemOperand(receiver, JSObject::kPropertiesOffset));
  // Check that the properties array is a dictionary.
  __ Ldr(map, FieldMemOperand(properties, HeapObject::kMapOffset));
  __ JumpIfNotRoot(map, Heap::kHashTableMapRootIndex, miss_label);

  NameDictionaryLookupStub::GenerateNegativeLookup(masm,
                                                     miss_label,
                                                     &done,
                                                     receiver,
                                                     properties,
                                                     name,
                                                     scratch1);
  __ Bind(&done);
  __ DecrementCounter(counters->negative_lookups_miss(), 1, scratch0, scratch1);
}


// Probe primary or secondary table.
// If the entry is found in the cache, the generated code jump to the first
// instruction of the stub in the cache.
// If there is a miss the code fall trough.
//
// 'receiver', 'name' and 'offset' registers are preserved on miss.
static void ProbeTable(Isolate* isolate,
                       MacroAssembler* masm,
                       Code::Flags flags,
                       StubCache::Table table,
                       Register receiver,
                       Register name,
                       Register offset,
                       Register scratch,
                       Register scratch2,
                       Register scratch3) {
  // Some code below relies on the fact that the Entry struct contains
  // 3 pointers (name, code, map).
  STATIC_ASSERT(sizeof(StubCache::Entry) == (3 * kPointerSize));

  ExternalReference key_offset(isolate->stub_cache()->key_reference(table));
  ExternalReference value_offset(isolate->stub_cache()->value_reference(table));
  ExternalReference map_offset(isolate->stub_cache()->map_reference(table));

  uintptr_t key_off_addr = reinterpret_cast<uintptr_t>(key_offset.address());
  uintptr_t value_off_addr =
      reinterpret_cast<uintptr_t>(value_offset.address());
  uintptr_t map_off_addr = reinterpret_cast<uintptr_t>(map_offset.address());

  Label miss;

  ASSERT(!AreAliased(name, offset, scratch, scratch2, scratch3));

  // Multiply by 3 because there are 3 fields per entry.
  __ Add(scratch3, offset, Operand(offset, LSL, 1));

  // Calculate the base address of the entry.
  __ Mov(scratch, Operand(key_offset));
  __ Add(scratch, scratch, Operand(scratch3, LSL, kPointerSizeLog2));

  // Check that the key in the entry matches the name.
  __ Ldr(scratch2, MemOperand(scratch));
  __ Cmp(name, scratch2);
  __ B(ne, &miss);

  // Check the map matches.
  __ Ldr(scratch2, MemOperand(scratch, map_off_addr - key_off_addr));
  __ Ldr(scratch3, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ Cmp(scratch2, scratch3);
  __ B(ne, &miss);

  // Get the code entry from the cache.
  __ Ldr(scratch, MemOperand(scratch, value_off_addr - key_off_addr));

  // Check that the flags match what we're looking for.
  __ Ldr(scratch2.W(), FieldMemOperand(scratch, Code::kFlagsOffset));
  __ Bic(scratch2.W(), scratch2.W(), Code::kFlagsNotUsedInLookup);
  __ Cmp(scratch2.W(), flags);
  __ B(ne, &miss);

#ifdef DEBUG
  if (FLAG_test_secondary_stub_cache && table == StubCache::kPrimary) {
    __ B(&miss);
  } else if (FLAG_test_primary_stub_cache && table == StubCache::kSecondary) {
    __ B(&miss);
  }
#endif

  // Jump to the first instruction in the code stub.
  __ Add(scratch, scratch, Code::kHeaderSize - kHeapObjectTag);
  __ Br(scratch);

  // Miss: fall through.
  __ Bind(&miss);
}


void StubCache::GenerateProbe(MacroAssembler* masm,
                              Code::Flags flags,
                              Register receiver,
                              Register name,
                              Register scratch,
                              Register extra,
                              Register extra2,
                              Register extra3) {
  Isolate* isolate = masm->isolate();
  Label miss;

  // Make sure the flags does not name a specific type.
  ASSERT(Code::ExtractTypeFromFlags(flags) == 0);

  // Make sure that there are no register conflicts.
  ASSERT(!AreAliased(receiver, name, scratch, extra, extra2, extra3));

  // Make sure extra and extra2 registers are valid.
  ASSERT(!extra.is(no_reg));
  ASSERT(!extra2.is(no_reg));
  ASSERT(!extra3.is(no_reg));

  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->megamorphic_stub_cache_probes(), 1,
                      extra2, extra3);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Compute the hash for primary table.
  __ Ldr(scratch, FieldMemOperand(name, Name::kHashFieldOffset));
  __ Ldr(extra, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ Add(scratch, scratch, extra);
  __ Eor(scratch, scratch, flags);
  // We shift out the last two bits because they are not part of the hash.
  __ Ubfx(scratch, scratch, kHeapObjectTagSize,
          CountTrailingZeros(kPrimaryTableSize, 64));

  // Probe the primary table.
  ProbeTable(isolate, masm, flags, kPrimary, receiver, name,
      scratch, extra, extra2, extra3);

  // Primary miss: Compute hash for secondary table.
  __ Sub(scratch, scratch, Operand(name, LSR, kHeapObjectTagSize));
  __ Add(scratch, scratch, flags >> kHeapObjectTagSize);
  __ And(scratch, scratch, kSecondaryTableSize - 1);

  // Probe the secondary table.
  ProbeTable(isolate, masm, flags, kSecondary, receiver, name,
      scratch, extra, extra2, extra3);

  // Cache miss: Fall-through and let caller handle the miss by
  // entering the runtime system.
  __ Bind(&miss);
  __ IncrementCounter(counters->megamorphic_stub_cache_misses(), 1,
                      extra2, extra3);
}


void StubCompiler::GenerateLoadGlobalFunctionPrototype(MacroAssembler* masm,
                                                       int index,
                                                       Register prototype) {
  // Load the global or builtins object from the current context.
  __ Ldr(prototype, GlobalObjectMemOperand());
  // Load the native context from the global or builtins object.
  __ Ldr(prototype,
         FieldMemOperand(prototype, GlobalObject::kNativeContextOffset));
  // Load the function from the native context.
  __ Ldr(prototype, ContextMemOperand(prototype, index));
  // Load the initial map. The global functions all have initial maps.
  __ Ldr(prototype,
         FieldMemOperand(prototype, JSFunction::kPrototypeOrInitialMapOffset));
  // Load the prototype from the initial map.
  __ Ldr(prototype, FieldMemOperand(prototype, Map::kPrototypeOffset));
}


void StubCompiler::GenerateDirectLoadGlobalFunctionPrototype(
    MacroAssembler* masm,
    int index,
    Register prototype,
    Label* miss) {
  Isolate* isolate = masm->isolate();
  // Check we're still in the same context.
  __ Ldr(prototype, GlobalObjectMemOperand());
  __ Cmp(prototype, Operand(isolate->global_object()));
  __ B(ne, miss);
  // Get the global function with the given index.
  Handle<JSFunction> function(
      JSFunction::cast(isolate->native_context()->get(index)));
  // Load its initial map. The global functions all have initial maps.
  __ Mov(prototype, Operand(Handle<Map>(function->initial_map())));
  // Load the prototype from the initial map.
  __ Ldr(prototype, FieldMemOperand(prototype, Map::kPrototypeOffset));
}


void StubCompiler::GenerateFastPropertyLoad(MacroAssembler* masm,
                                            Register dst,
                                            Register src,
                                            bool inobject,
                                            int index,
                                            Representation representation) {
  ASSERT(!FLAG_track_double_fields || !representation.IsDouble());
  USE(representation);
  if (inobject) {
    int offset = index * kPointerSize;
    __ Ldr(dst, FieldMemOperand(src, offset));
  } else {
    // Calculate the offset into the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    __ Ldr(dst, FieldMemOperand(src, JSObject::kPropertiesOffset));
    __ Ldr(dst, FieldMemOperand(dst, offset));
  }
}


void StubCompiler::GenerateLoadArrayLength(MacroAssembler* masm,
                                           Register receiver,
                                           Register scratch,
                                           Label* miss_label) {
  ASSERT(!AreAliased(receiver, scratch));

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, miss_label);

  // Check that the object is a JS array.
  __ JumpIfNotObjectType(receiver, scratch, scratch, JS_ARRAY_TYPE,
                         miss_label);

  // Load length directly from the JS array.
  __ Ldr(x0, FieldMemOperand(receiver, JSArray::kLengthOffset));
  __ Ret();
}


// Generate code to check if an object is a string.  If the object is a
// heap object, its map's instance type is left in the scratch1 register.
static void GenerateStringCheck(MacroAssembler* masm,
                                Register receiver,
                                Register scratch1,
                                Label* smi,
                                Label* non_string_object) {
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, smi);

  // Get the object's instance type filed.
  __ Ldr(scratch1, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ Ldrb(scratch1, FieldMemOperand(scratch1, Map::kInstanceTypeOffset));
  // Check if the "not string" bit is set.
  __ Tbnz(scratch1, MaskToBit(kNotStringTag), non_string_object);
}


// Generate code to load the length from a string object and return the length.
// If the receiver object is not a string or a wrapped string object the
// execution continues at the miss label. The register containing the
// receiver is not clobbered if the receiver is not a string.
void StubCompiler::GenerateLoadStringLength(MacroAssembler* masm,
                                            Register receiver,
                                            Register scratch1,
                                            Register scratch2,
                                            Label* miss,
                                            bool support_wrappers) {
  // Input registers can't alias because we don't want to clobber the
  // receiver register if the object is not a string.
  ASSERT(!AreAliased(receiver, scratch1, scratch2));

  Label check_wrapper;

  // Check if the object is a string leaving the instance type in the
  // scratch1 register.
  GenerateStringCheck(masm, receiver, scratch1, miss,
                      support_wrappers ? &check_wrapper : miss);

  // Load length directly from the string.
  __ Ldr(x0, FieldMemOperand(receiver, String::kLengthOffset));
  __ Ret();

  if (support_wrappers) {
    // Check if the object is a JSValue wrapper.
    __ Bind(&check_wrapper);
    __ Cmp(scratch1, Operand(JS_VALUE_TYPE));
    __ B(ne, miss);

    // Unwrap the value and check if the wrapped value is a string.
    __ Ldr(scratch1, FieldMemOperand(receiver, JSValue::kValueOffset));
    GenerateStringCheck(masm, scratch1, scratch2, miss, miss);
    __ Ldr(x0, FieldMemOperand(scratch1, String::kLengthOffset));
    __ Ret();
  }
}


void StubCompiler::GenerateLoadFunctionPrototype(MacroAssembler* masm,
                                                 Register receiver,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Label* miss_label) {
  __ TryGetFunctionPrototype(receiver, scratch1, scratch2, miss_label);
  // TryGetFunctionPrototype can't put the result directly in x0 because the
  // 3 inputs registers can't alias and we call this function from
  // LoadIC::GenerateFunctionPrototype, where receiver is x0. So we explicitly
  // move the result in x0.
  __ Mov(x0, scratch1);
  __ Ret();
}


// Generate code to check that a global property cell is empty. Create
// the property cell at compilation time if no cell exists for the
// property.
static void GenerateCheckPropertyCell(MacroAssembler* masm,
                                      Handle<GlobalObject> global,
                                      Handle<Name> name,
                                      Register scratch,
                                      Register the_hole,
                                      Label* miss) {
  Handle<Cell> cell = GlobalObject::EnsurePropertyCell(global, name);
  ASSERT(cell->value()->IsTheHole());
  __ Mov(scratch, Operand(cell));
  __ Ldr(scratch, FieldMemOperand(scratch, Cell::kValueOffset));
  __ Cmp(scratch, the_hole);
  __ B(ne, miss);
}


void BaseStoreStubCompiler::GenerateNegativeHolderLookup(
    MacroAssembler* masm,
    Handle<JSObject> holder,
    Register holder_reg,
    Handle<Name> name,
    Label* miss) {
  if (holder->IsJSGlobalObject()) {
    __ LoadRoot(scratch2(), Heap::kTheHoleValueRootIndex);
    GenerateCheckPropertyCell(
        masm, Handle<GlobalObject>::cast(holder), name,
        scratch1(), scratch2(), miss);
  } else if (!holder->HasFastProperties() && !holder->IsJSGlobalProxy()) {
    GenerateDictionaryNegativeLookup(
        masm, miss, holder_reg, name, scratch1(), scratch2());
  }
}


// Generate StoreTransition code, value is passed in x0 register.
// When leaving generated code after success, the receiver_reg and storage_reg
// may be clobbered. Upon branch to miss_label, the receiver and name registers
// have their original values.
void BaseStoreStubCompiler::GenerateStoreTransition(MacroAssembler* masm,
                                                    Handle<JSObject> object,
                                                    LookupResult* lookup,
                                                    Handle<Map> transition,
                                                    Handle<Name> name,
                                                    Register receiver_reg,
                                                    Register storage_reg,
                                                    Register value_reg,
                                                    Register scratch1,
                                                    Register scratch2,
                                                    Register scratch3,
                                                    Label* miss_label,
                                                    Label* slow) {
  Label exit;

  ASSERT(!AreAliased(receiver_reg, storage_reg, value_reg,
                     scratch1, scratch2, scratch3));

  // We don't need scratch3.
  scratch3 = NoReg;

  int descriptor = transition->LastAdded();
  DescriptorArray* descriptors = transition->instance_descriptors();
  PropertyDetails details = descriptors->GetDetails(descriptor);
  Representation representation = details.representation();
  ASSERT(!representation.IsNone());

  if (details.type() == CONSTANT) {
    Handle<Object> constant(descriptors->GetValue(descriptor), masm->isolate());
    __ LoadObject(scratch1, constant);
    __ Cmp(value_reg, scratch1);
    __ B(ne, miss_label);
  } else if (FLAG_track_fields && representation.IsSmi()) {
    __ JumpIfNotSmi(value_reg, miss_label);
  } else if (FLAG_track_heap_object_fields && representation.IsHeapObject()) {
    __ JumpIfSmi(value_reg, miss_label);
  } else if (FLAG_track_double_fields && representation.IsDouble()) {
    Label do_store, heap_number;
    __ AllocateHeapNumber(storage_reg, slow, scratch1, scratch2);

    // TODO(jbramley): Is fp_scratch the most appropriate FP scratch register?
    // It's only used in Fcmp, but it's not really safe to use it like this.
    __ JumpIfNotSmi(value_reg, &heap_number);
    __ SmiUntagToDouble(fp_scratch, value_reg);
    __ B(&do_store);

    __ Bind(&heap_number);
    __ CheckMap(value_reg, scratch1, Heap::kHeapNumberMapRootIndex,
                miss_label, DONT_DO_SMI_CHECK);
    __ Ldr(fp_scratch, FieldMemOperand(value_reg, HeapNumber::kValueOffset));

    __ Bind(&do_store);
    __ Str(fp_scratch, FieldMemOperand(storage_reg, HeapNumber::kValueOffset));
  }

  // Stub never generated for non-global objects that require access checks.
  ASSERT(object->IsJSGlobalProxy() || !object->IsAccessCheckNeeded());

  // Perform map transition for the receiver if necessary.
  if ((details.type() == FIELD) &&
      (object->map()->unused_property_fields() == 0)) {
    // The properties must be extended before we can store the value.
    // We jump to a runtime call that extends the properties array.
    __ Mov(scratch1, Operand(transition));
    __ Push(receiver_reg, scratch1, value_reg);
    __ TailCallExternalReference(
        ExternalReference(IC_Utility(IC::kSharedStoreIC_ExtendStorage),
                          masm->isolate()),
        3,
        1);
    return;
  }

  // Update the map of the object.
  __ Mov(scratch1, Operand(transition));
  __ Str(scratch1, FieldMemOperand(receiver_reg, HeapObject::kMapOffset));

  // Update the write barrier for the map field.
  __ RecordWriteField(receiver_reg,
                      HeapObject::kMapOffset,
                      scratch1,
                      scratch2,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs,
                      OMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);

  if (details.type() == CONSTANT) {
    ASSERT(value_reg.is(x0));
    __ Ret();
    return;
  }

  int index = transition->instance_descriptors()->GetFieldIndex(
      transition->LastAdded());

  // Adjust for the number of properties stored in the object. Even in the
  // face of a transition we can use the old map here because the size of the
  // object and the number of in-object properties is not going to change.
  index -= object->map()->inobject_properties();

  // TODO(verwaest): Share this code as a code stub.
  SmiCheck smi_check = representation.IsTagged()
      ? INLINE_SMI_CHECK : OMIT_SMI_CHECK;
  if (index < 0) {
    // Set the property straight into the object.
    int offset = object->map()->instance_size() + (index * kPointerSize);
    // TODO(jbramley): This construct appears in several places in this
    // function. Try to clean it up, perhaps using a result_reg.
    if (FLAG_track_double_fields && representation.IsDouble()) {
      __ Str(storage_reg, FieldMemOperand(receiver_reg, offset));
    } else {
      __ Str(value_reg, FieldMemOperand(receiver_reg, offset));
    }

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Update the write barrier for the array address.
      if (!FLAG_track_double_fields || !representation.IsDouble()) {
        __ Mov(storage_reg, value_reg);
      }
      __ RecordWriteField(receiver_reg,
                          offset,
                          storage_reg,
                          scratch1,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  } else {
    // Write to the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    // Get the properties array
    __ Ldr(scratch1,
           FieldMemOperand(receiver_reg, JSObject::kPropertiesOffset));
    if (FLAG_track_double_fields && representation.IsDouble()) {
      __ Str(storage_reg, FieldMemOperand(scratch1, offset));
    } else {
      __ Str(value_reg, FieldMemOperand(scratch1, offset));
    }

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Update the write barrier for the array address.
      if (!FLAG_track_double_fields || !representation.IsDouble()) {
        __ Mov(storage_reg, value_reg);
      }
      __ RecordWriteField(scratch1,
                          offset,
                          storage_reg,
                          receiver_reg,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  }

  __ Bind(&exit);
  // Return the value (register x0).
  ASSERT(value_reg.is(x0));
  __ Ret();
}


// Generate StoreField code, value is passed in x0 register.
// When leaving generated code after success, the receiver_reg and name_reg may
// be clobbered. Upon branch to miss_label, the receiver and name registers have
// their original values.
void BaseStoreStubCompiler::GenerateStoreField(MacroAssembler* masm,
                                               Handle<JSObject> object,
                                               LookupResult* lookup,
                                               Register receiver_reg,
                                               Register name_reg,
                                               Register value_reg,
                                               Register scratch1,
                                               Register scratch2,
                                               Label* miss_label) {
  // x0 : value
  Label exit;

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalProxy() || !object->IsAccessCheckNeeded());

  int index = lookup->GetFieldIndex().field_index();

  // Adjust for the number of properties stored in the object. Even in the
  // face of a transition we can use the old map here because the size of the
  // object and the number of in-object properties is not going to change.
  index -= object->map()->inobject_properties();

  Representation representation = lookup->representation();
  ASSERT(!representation.IsNone());
  if (FLAG_track_fields && representation.IsSmi()) {
    __ JumpIfNotSmi(value_reg, miss_label);
  } else if (FLAG_track_heap_object_fields && representation.IsHeapObject()) {
    __ JumpIfSmi(value_reg, miss_label);
  } else if (FLAG_track_double_fields && representation.IsDouble()) {
    // Load the double storage.
    if (index < 0) {
      int offset = (index * kPointerSize) + object->map()->instance_size();
      __ Ldr(scratch1, FieldMemOperand(receiver_reg, offset));
    } else {
      int offset = (index * kPointerSize) + FixedArray::kHeaderSize;
      __ Ldr(scratch1,
             FieldMemOperand(receiver_reg, JSObject::kPropertiesOffset));
      __ Ldr(scratch1, FieldMemOperand(scratch1, offset));
    }

    // Store the value into the storage.
    Label do_store, heap_number;
    // TODO(jbramley): Is fp_scratch the most appropriate FP scratch register?
    // It's only used in Fcmp, but it's not really safe to use it like this.
    __ JumpIfNotSmi(value_reg, &heap_number);
    __ SmiUntagToDouble(fp_scratch, value_reg);
    __ B(&do_store);

    __ Bind(&heap_number);
    __ CheckMap(value_reg, scratch2, Heap::kHeapNumberMapRootIndex,
                miss_label, DONT_DO_SMI_CHECK);
    __ Ldr(fp_scratch, FieldMemOperand(value_reg, HeapNumber::kValueOffset));

    __ Bind(&do_store);
    __ Str(fp_scratch, FieldMemOperand(scratch1, HeapNumber::kValueOffset));

    // Return the value (register x0).
    ASSERT(value_reg.is(x0));
    __ Ret();
    return;
  }

  // TODO(verwaest): Share this code as a code stub.
  SmiCheck smi_check = representation.IsTagged()
      ? INLINE_SMI_CHECK : OMIT_SMI_CHECK;
  if (index < 0) {
    // Set the property straight into the object.
    int offset = object->map()->instance_size() + (index * kPointerSize);
    __ Str(value_reg, FieldMemOperand(receiver_reg, offset));

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Skip updating write barrier if storing a smi.
      __ JumpIfSmi(value_reg, &exit);

      // Update the write barrier for the array address.
      // Pass the now unused name_reg as a scratch register.
      __ Mov(name_reg, value_reg);
      __ RecordWriteField(receiver_reg,
                          offset,
                          name_reg,
                          scratch1,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  } else {
    // Write to the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    // Get the properties array
    __ Ldr(scratch1,
           FieldMemOperand(receiver_reg, JSObject::kPropertiesOffset));
    __ Str(value_reg, FieldMemOperand(scratch1, offset));

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Skip updating write barrier if storing a smi.
      __ JumpIfSmi(value_reg, &exit);

      // Update the write barrier for the array address.
      // Ok to clobber receiver_reg and name_reg, since we return.
      __ Mov(name_reg, value_reg);
      __ RecordWriteField(scratch1,
                          offset,
                          name_reg,
                          receiver_reg,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  }

  __ Bind(&exit);
  // Return the value (register x0).
  ASSERT(value_reg.is(x0));
  __ Ret();
}


void BaseStoreStubCompiler::GenerateRestoreName(MacroAssembler* masm,
                                                Label* label,
                                                Handle<Name> name) {
  if (!label->is_unused()) {
    __ Bind(label);
    __ Mov(this->name(), Operand(name));
  }
}


// Calls GenerateCheckPropertyCell for each global object in the prototype chain
// from object to (but not including) holder.
static void GenerateCheckPropertyCells(MacroAssembler* masm,
                                       Handle<JSObject> object,
                                       Handle<JSObject> holder,
                                       Handle<Name> name,
                                       Register scratch1,
                                       Register scratch2,
                                       Label* miss) {
  bool the_hole_is_loaded = false;
  Handle<JSObject> current = object;
  while (!current.is_identical_to(holder)) {
    if (current->IsGlobalObject()) {
      if (!the_hole_is_loaded) {
        __ LoadRoot(scratch2, Heap::kTheHoleValueRootIndex);
        the_hole_is_loaded = true;
      }
      GenerateCheckPropertyCell(masm,
                                Handle<GlobalObject>::cast(current),
                                name,
                                scratch1,
                                scratch2,
                                miss);
    }
    current = Handle<JSObject>(JSObject::cast(current->GetPrototype()));
  }
}


// The function to called must be passed in x1.
static void GenerateCallFunction(MacroAssembler* masm,
                                 Handle<Object> object,
                                 const ParameterCount& arguments,
                                 Label* miss,
                                 Code::ExtraICState extra_ic_state,
                                 Register function,
                                 Register receiver,
                                 Register scratch) {
  ASSERT(!AreAliased(function, receiver, scratch));
  ASSERT(function.Is(x1));

  // Check that the function really is a function.
  __ JumpIfSmi(function, miss);
  __ JumpIfNotObjectType(function, scratch, scratch, JS_FUNCTION_TYPE, miss);

  // Patch the receiver on the stack with the global proxy if necessary.
  if (object->IsGlobalObject()) {
    __ Ldr(scratch,
           FieldMemOperand(receiver, GlobalObject::kGlobalReceiverOffset));
    __ Poke(scratch, arguments.immediate() * kPointerSize);
  }

  // Invoke the function.
  CallKind call_kind = CallICBase::Contextual::decode(extra_ic_state)
      ? CALL_AS_FUNCTION
      : CALL_AS_METHOD;
  __ InvokeFunction(
      function, arguments, JUMP_FUNCTION, NullCallWrapper(), call_kind);
}


static void PushInterceptorArguments(MacroAssembler* masm,
                                     Register receiver,
                                     Register holder,
                                     Register name,
                                     Handle<JSObject> holder_obj) {
  __ Push(name);
  Handle<InterceptorInfo> interceptor(holder_obj->GetNamedInterceptor());
  ASSERT(!masm->isolate()->heap()->InNewSpace(*interceptor));
  Register scratch = name;
  __ Mov(scratch, Operand(interceptor));
  __ Push(scratch, receiver, holder);
  __ Ldr(scratch, FieldMemOperand(scratch, InterceptorInfo::kDataOffset));
  __ Push(scratch);
  __ Mov(scratch, Operand(ExternalReference::isolate_address(masm->isolate())));
  __ Push(scratch);
}


static void CompileCallLoadPropertyWithInterceptor(
    MacroAssembler* masm,
    Register receiver,
    Register holder,
    Register name,
    Handle<JSObject> holder_obj) {
  PushInterceptorArguments(masm, receiver, holder, name, holder_obj);

  ExternalReference ref =
      ExternalReference(IC_Utility(IC::kLoadPropertyWithInterceptorOnly),
                        masm->isolate());
  // Put the number of on-stack arguments for runtime call in x0.
  // These arguemnts have been pushed by PushInterceptorArguments.
  __ Mov(x0, 6);
  __ Mov(x1, Operand(ref));

  CEntryStub stub(1);
  __ CallStub(&stub);
}


static const int kFastApiCallArguments = FunctionCallbackArguments::kArgsLength;

// Reserves space for the extra arguments to API function in the
// caller's frame.
//
// These arguments are set by CheckPrototypes and GenerateFastApiDirectCall.
static void ReserveSpaceForFastApiCall(MacroAssembler* masm,
                                       Register scratch) {
  ASSERT(Smi::FromInt(0) == 0);
  __ PushMultipleTimes(kFastApiCallArguments, xzr);
}


// Undoes the effects of ReserveSpaceForFastApiCall.
static void FreeSpaceForFastApiCall(MacroAssembler* masm) {
  __ Drop(kFastApiCallArguments);
}


static void GenerateFastApiDirectCall(MacroAssembler* masm,
                                      const CallOptimization& optimization,
                                      int argc) {
  // ----------- S t a t e -------------
  //  -- sp[0]              : holder (set by CheckPrototypes)
  //  -- sp[8]              : callee JS function
  //  -- sp[16]             : call data
  //  -- sp[24]             : isolate
  //  -- sp[32]             : ReturnValue default value
  //  -- sp[40]             : ReturnValue
  //  -- sp[48]             : last JS argument
  //  -- ...
  //  -- sp[(argc + 5) * 8] : first JS argument
  //  -- sp[(argc + 6) * 8] : receiver
  // -----------------------------------
  // Get the function and setup the context.
  Handle<JSFunction> function = optimization.constant_function();
  Register function_reg = x5;
  __ LoadHeapObject(function_reg, function);
  __ Ldr(cp, FieldMemOperand(function_reg, JSFunction::kContextOffset));

  // Pass the additional arguments.
  Handle<CallHandlerInfo> api_call_info = optimization.api_call_info();
  Handle<Object> call_data(api_call_info->data(), masm->isolate());
  Register call_data_reg = x6;
  if (masm->isolate()->heap()->InNewSpace(*call_data)) {
    __ Mov(x0, Operand(api_call_info));
    __ Ldr(call_data_reg, FieldMemOperand(x0, CallHandlerInfo::kDataOffset));
  } else {
    __ Mov(call_data_reg, Operand(call_data));
  }

  Register isolate_reg = x7;
  __ Mov(isolate_reg,
         Operand(ExternalReference::isolate_address(masm->isolate())));

  Register undefined_reg = x8;
  __ LoadRoot(undefined_reg, Heap::kUndefinedValueRootIndex);

  // Store JS function, call data, isolate, ReturnValue default and ReturnValue.
  // TODO(jbramley): Try to combine these accesses using stp.
  __ Poke(function_reg, 1 * kXRegSizeInBytes);
  __ Poke(call_data_reg, 2 * kXRegSizeInBytes);
  __ Poke(isolate_reg, 3 * kXRegSizeInBytes);
  __ Poke(undefined_reg, 4 * kXRegSizeInBytes);
  __ Poke(undefined_reg, 5 * kXRegSizeInBytes);

  Register implicit_args = x2;
  __ Add(implicit_args, masm->StackPointer(), 5 * kXRegSizeInBytes);

  FrameScope frame_scope(masm, StackFrame::MANUAL);
  // Allocate the v8::Arguments structure inside the ExitFrame since it's not
  // controlled by GC.
  const int kApiArgsStackSpace = 4;
  __ EnterExitFrame(
      false,
      x3,
      kApiArgsStackSpace + MacroAssembler::kCallApiFunctionSpillSpace);

  // Arguments structure is after the return address.
  Register args = x0;
  __ Add(args, masm->StackPointer(), kPointerSize);

  // v8::Arguments::implicit_args_
  __ Str(implicit_args, MemOperand(args, 0 * kPointerSize));
  // v8::Arguments::values_
  __ Add(x3, implicit_args, argc * kPointerSize);
  __ Str(x3, MemOperand(args, 1 * kPointerSize));
  // v8::Arguments::length_ = argc
  __ Mov(x3, argc);
  __ Str(x3, MemOperand(args, 2 * kPointerSize));
  // v8::Arguments::is_construct_call = 0
  __ Mov(x3, 0);
  __ Str(x3, MemOperand(args, 3 * kPointerSize));

  // After the call to the API function we need to free memory used for:
  //  - JS arguments
  //  - the receiver
  //  - the space allocated by ReserveSpaceForFastApiCall.
  //
  // The memory allocated for v8::Arguments structure will be freed when we'll
  // leave the ExitFrame.
  const int kStackUnwindSpace = argc + kFastApiCallArguments + 1;

  Address function_address = v8::ToCData<Address>(api_call_info->callback());
  bool returns_handle =
      !CallbackTable::ReturnsVoid(masm->isolate(), function_address);
  ApiFunction fun(function_address);
  ExternalReference::Type type =
      returns_handle ?
          ExternalReference::DIRECT_API_CALL :
          ExternalReference::DIRECT_API_CALL_NEW;
  ExternalReference ref = ExternalReference(&fun, type, masm->isolate());

  Address thunk_address =
      returns_handle ?
          FUNCTION_ADDR(&InvokeInvocationCallback) :
          FUNCTION_ADDR(&InvokeFunctionCallback);
  ExternalReference::Type thunk_type =
      returns_handle ?
          ExternalReference::PROFILING_API_CALL :
          ExternalReference::PROFILING_API_CALL_NEW;
  ApiFunction thunk_fun(thunk_address);
  ExternalReference thunk_ref =
      ExternalReference(&thunk_fun, thunk_type, masm->isolate());

  AllowExternalCallThatCantCauseGC scope(masm);
  // CallApiFunctionAndReturn can spill registers inside the exit frame,
  // after the return address and the v8::Arguments structure.
  const int spill_offset = 1 + kApiArgsStackSpace;
  __ CallApiFunctionAndReturn(ref,
                              function_address,
                              thunk_ref,
                              x1,
                              kStackUnwindSpace,
                              spill_offset,
                              returns_handle,
                              kFastApiCallArguments + 1);
}


class CallInterceptorCompiler BASE_EMBEDDED {
 public:
  CallInterceptorCompiler(StubCompiler* stub_compiler,
                          const ParameterCount& arguments,
                          Register name,
                          Code::ExtraICState extra_ic_state)
      : stub_compiler_(stub_compiler),
        arguments_(arguments),
        name_(name),
        extra_ic_state_(extra_ic_state) {}

  void Compile(MacroAssembler* masm,
               Handle<JSObject> object,
               Handle<JSObject> holder,
               Handle<Name> name,
               LookupResult* lookup,
               Register receiver,
               Register scratch1,
               Register scratch2,
               Register scratch3,
               Label* miss) {
    ASSERT(holder->HasNamedInterceptor());
    ASSERT(!holder->GetNamedInterceptor()->getter()->IsUndefined());

    // Check that the receiver isn't a smi.
    __ JumpIfSmi(receiver, miss);

    CallOptimization optimization(lookup);
    if (optimization.is_constant_call()) {
      CompileCacheable(masm, object, receiver, scratch1, scratch2, scratch3,
                       holder, lookup, name, optimization, miss);
    } else {
      CompileRegular(masm, object, receiver, scratch1, scratch2, scratch3,
                     name, holder, miss);
    }
  }

 private:
  void CompileCacheable(MacroAssembler* masm,
                        Handle<JSObject> object,
                        Register receiver,
                        Register scratch1,
                        Register scratch2,
                        Register scratch3,
                        Handle<JSObject> interceptor_holder,
                        LookupResult* lookup,
                        Handle<Name> name,
                        const CallOptimization& optimization,
                        Label* miss_label) {
    ASSERT(optimization.is_constant_call());
    ASSERT(!lookup->holder()->IsGlobalObject());

    Counters* counters = masm->isolate()->counters();
    int depth1 = kInvalidProtoDepth;
    int depth2 = kInvalidProtoDepth;
    bool can_do_fast_api_call = false;

    if (optimization.is_simple_api_call() &&
        !lookup->holder()->IsGlobalObject()) {
      depth1 = optimization.GetPrototypeDepthOfExpectedType(
          object, interceptor_holder);
      if (depth1 == kInvalidProtoDepth) {
        depth2 = optimization.GetPrototypeDepthOfExpectedType(
            interceptor_holder, Handle<JSObject>(lookup->holder()));
      }
      can_do_fast_api_call =
          depth1 != kInvalidProtoDepth || depth2 != kInvalidProtoDepth;
    }

    __ IncrementCounter(counters->call_const_interceptor(), 1,
                        scratch1, scratch2);

    if (can_do_fast_api_call) {
      __ IncrementCounter(counters->call_const_interceptor_fast_api(), 1,
                          scratch1, scratch2);
      ReserveSpaceForFastApiCall(masm, scratch1);
    }

    // Check that the maps from receiver to interceptor's holder
    // haven't changed and thus we can invoke interceptor.
    Label miss_cleanup;
    Label* miss = can_do_fast_api_call ? &miss_cleanup : miss_label;
    Register holder =
        stub_compiler_->CheckPrototypes(object, receiver, interceptor_holder,
                                        scratch1, scratch2, scratch3,
                                        name, depth1, miss);

    // Invoke an interceptor and if it provides a value,
    // branch to |regular_invoke|.
    Label regular_invoke;
    LoadWithInterceptor(masm, receiver, holder, interceptor_holder, scratch2,
                        &regular_invoke);

    // Interceptor returned nothing for this property.  Try to use cached
    // constant function.

    // Check that the maps from interceptor's holder to constant function's
    // holder haven't changed and thus we can use cached constant function.
    if (*interceptor_holder != lookup->holder()) {
      stub_compiler_->CheckPrototypes(interceptor_holder, receiver,
                                      Handle<JSObject>(lookup->holder()),
                                      scratch1, scratch2, scratch3,
                                      name, depth2, miss);
    } else {
      // CheckPrototypes has a side effect of fetching a 'holder'
      // for API (object which is instanceof for the signature).  It's
      // safe to omit it here, as if present, it should be fetched
      // by the previous CheckPrototypes.
      ASSERT(depth2 == kInvalidProtoDepth);
    }

    // Invoke function.
    if (can_do_fast_api_call) {
      GenerateFastApiDirectCall(masm, optimization, arguments_.immediate());
    } else {
      CallKind call_kind = CallICBase::Contextual::decode(extra_ic_state_)
          ? CALL_AS_FUNCTION
          : CALL_AS_METHOD;
      Handle<JSFunction> function = optimization.constant_function();
      ParameterCount expected(function);
      __ InvokeFunction(function, expected, arguments_,
                        JUMP_FUNCTION, NullCallWrapper(), call_kind);
    }

    // Deferred code for fast API call case, clean preallocated space.
    if (can_do_fast_api_call) {
      __ Bind(&miss_cleanup);
      FreeSpaceForFastApiCall(masm);
      __ B(miss_label);
    }

    // Invoke a regular function.
    __ Bind(&regular_invoke);
    if (can_do_fast_api_call) {
      FreeSpaceForFastApiCall(masm);
    }
  }

  void CompileRegular(MacroAssembler* masm,
                      Handle<JSObject> object,
                      Register receiver,
                      Register scratch1,
                      Register scratch2,
                      Register scratch3,
                      Handle<Name> name,
                      Handle<JSObject> interceptor_holder,
                      Label* miss_label) {
    Register holder =
        stub_compiler_->CheckPrototypes(object, receiver, interceptor_holder,
                                        scratch1, scratch2, scratch3,
                                        name, miss_label);

    // Call a runtime function to load the interceptor property.
    FrameScope scope(masm, StackFrame::INTERNAL);
    // The name_ register must be preserved across the call.
    __ Push(name_);
    PushInterceptorArguments(masm, receiver, holder, name_, interceptor_holder);
    __ CallExternalReference(
        ExternalReference(IC_Utility(IC::kLoadPropertyWithInterceptorForCall),
                          masm->isolate()),
        6);
    __ Pop(name_);
  }


  void LoadWithInterceptor(MacroAssembler* masm,
                           Register receiver,
                           Register holder,
                           Handle<JSObject> holder_obj,
                           Register scratch,
                           Label* interceptor_succeeded) {
    {
      FrameScope scope(masm, StackFrame::INTERNAL);
      __ Push(holder, name_);
      CompileCallLoadPropertyWithInterceptor(masm,
                                             receiver,
                                             holder,
                                             name_,
                                             holder_obj);
      __ Pop(name_, receiver);
    }

    // If interceptor returns no-result sentinel, call the constant function.
    __ JumpIfNotRoot(x0,
                     Heap::kNoInterceptorResultSentinelRootIndex,
                     interceptor_succeeded);
  }

  StubCompiler* stub_compiler_;
  const ParameterCount& arguments_;
  Register name_;
  Code::ExtraICState extra_ic_state_;
};


void StubCompiler::GenerateTailCall(MacroAssembler* masm, Handle<Code> code) {
  __ Jump(code, RelocInfo::CODE_TARGET);
}


#undef __
#define __ ACCESS_MASM(masm())


Register StubCompiler::CheckPrototypes(Handle<JSObject> object,
                                       Register object_reg,
                                       Handle<JSObject> holder,
                                       Register holder_reg,
                                       Register scratch1,
                                       Register scratch2,
                                       Handle<Name> name,
                                       int save_at_depth,
                                       Label* miss,
                                       PrototypeCheckType check) {
  // Make sure that the type feedback oracle harvests the receiver map.
  // TODO(svenpanne) Remove this hack when all ICs are reworked.
  __ Mov(scratch1, Operand(Handle<Map>(object->map())));

  Handle<JSObject> first = object;

  // object_reg and holder_reg registers can alias.
  ASSERT(!AreAliased(object_reg, scratch1, scratch2));
  ASSERT(!AreAliased(holder_reg, scratch1, scratch2));

  // Keep track of the current object in register reg.
  Register reg = object_reg;
  int depth = 0;

  if (save_at_depth == depth) {
    __ Poke(reg, 0);
  }

  // Check the maps in the prototype chain.
  // Traverse the prototype chain from the object and do map checks.
  Handle<JSObject> current = object;
  while (!current.is_identical_to(holder)) {
    ++depth;

    // Only global objects and objects that do not require access
    // checks are allowed in stubs.
    ASSERT(current->IsJSGlobalProxy() || !current->IsAccessCheckNeeded());

    Handle<JSObject> prototype(JSObject::cast(current->GetPrototype()));
    if (!current->HasFastProperties() &&
        !current->IsJSGlobalObject() &&
        !current->IsJSGlobalProxy()) {
      if (!name->IsUniqueName()) {
        ASSERT(name->IsString());
        name = factory()->InternalizeString(Handle<String>::cast(name));
      }
      ASSERT(current->property_dictionary()->FindEntry(*name) ==
             NameDictionary::kNotFound);

      GenerateDictionaryNegativeLookup(masm(), miss, reg, name,
                                       scratch1, scratch2);

      __ Ldr(scratch1, FieldMemOperand(reg, HeapObject::kMapOffset));
      reg = holder_reg;  // From now on the object will be in holder_reg.
      __ Ldr(reg, FieldMemOperand(scratch1, Map::kPrototypeOffset));
    } else {
      Register map_reg = scratch1;
      // TODO(jbramley): Skip this load when we don't need the map.
      __ Ldr(map_reg, FieldMemOperand(reg, HeapObject::kMapOffset));

      if (!current.is_identical_to(first) || (check == CHECK_ALL_MAPS)) {
        Handle<Map> current_map(current->map());
        __ CheckMap(map_reg, current_map, miss, DONT_DO_SMI_CHECK);
      }

      // Check access rights to the global object.  This has to happen after
      // the map check so that we know that the object is actually a global
      // object.
      if (current->IsJSGlobalProxy()) {
        __ CheckAccessGlobalProxy(reg, scratch2, miss);
      }
      reg = holder_reg;  // From now on the object will be in holder_reg.

      if (heap()->InNewSpace(*prototype)) {
        // The prototype is in new space; we cannot store a reference to it
        // in the code.  Load it from the map.
        __ Ldr(reg, FieldMemOperand(map_reg, Map::kPrototypeOffset));
      } else {
        // The prototype is in old space; load it directly.
        __ Mov(reg, Operand(prototype));
      }
    }

    if (save_at_depth == depth) {
      __ Poke(reg, 0);
    }

    // Go to the next object in the prototype chain.
    current = prototype;
  }

  // Log the check depth.
  LOG(isolate(), IntEvent("check-maps-depth", depth + 1));

  // Check the holder map.
  if (!holder.is_identical_to(first) || check == CHECK_ALL_MAPS) {
    // Check the holder map.
    __ CheckMap(reg, scratch1, Handle<Map>(current->map()), miss,
                DONT_DO_SMI_CHECK);
  }

  // Perform security check for access to the global object.
  ASSERT(holder->IsJSGlobalProxy() || !holder->IsAccessCheckNeeded());
  if (holder->IsJSGlobalProxy()) {
    __ CheckAccessGlobalProxy(reg, scratch1, miss);
  }

  // If we've skipped any global objects, it's not enough to verify that
  // their maps haven't changed.  We also need to check that the property
  // cell for the property is still empty.
  GenerateCheckPropertyCells(masm(), object, holder, name,
                             scratch1, scratch2, miss);

  // Return the register containing the holder.
  return reg;
}


void BaseLoadStubCompiler::HandlerFrontendFooter(Handle<Name> name,
                                                 Label* success,
                                                 Label* miss) {
  if (!miss->is_unused()) {
    __ B(success);
    __ Bind(miss);
    TailCallBuiltin(masm(), MissBuiltin(kind()));
  }
}


void BaseStoreStubCompiler::HandlerFrontendFooter(Handle<Name> name,
                                                  Label* success,
                                                  Label* miss) {
  if (!miss->is_unused()) {
    __ B(success);
    GenerateRestoreName(masm(), miss, name);
    TailCallBuiltin(masm(), MissBuiltin(kind()));
  }
}


Register BaseLoadStubCompiler::CallbackHandlerFrontend(
    Handle<JSObject> object,
    Register object_reg,
    Handle<JSObject> holder,
    Handle<Name> name,
    Label* success,
    Handle<ExecutableAccessorInfo> callback) {
  Label miss;

  Register reg = HandlerFrontendHeader(object, object_reg, holder, name, &miss);

  // TODO(jbramely): HandlerFrontendHeader returns its result in scratch1(), so
  // we can't use it below, but that isn't very obvious. Is there a better way
  // of handling this?

  if (!holder->HasFastProperties() && !holder->IsJSGlobalObject()) {
    ASSERT(!AreAliased(reg, scratch2(), scratch3(), scratch4()));

    // Load the properties dictionary.
    Register dictionary = scratch4();
    __ Ldr(dictionary, FieldMemOperand(reg, JSObject::kPropertiesOffset));

    // Probe the dictionary.
    Label probe_done;
    NameDictionaryLookupStub::GeneratePositiveLookup(masm(),
                                                     &miss,
                                                     &probe_done,
                                                     dictionary,
                                                     this->name(),
                                                     scratch2(),
                                                     scratch3());
    __ Bind(&probe_done);

    // If probing finds an entry in the dictionary, scratch3 contains the
    // pointer into the dictionary. Check that the value is the callback.
    Register pointer = scratch3();
    const int kElementsStartOffset = NameDictionary::kHeaderSize +
        NameDictionary::kElementsStartIndex * kPointerSize;
    const int kValueOffset = kElementsStartOffset + kPointerSize;
    __ Ldr(scratch2(), FieldMemOperand(pointer, kValueOffset));
    __ Cmp(scratch2(), Operand(callback));
    __ B(ne, &miss);
  }

  HandlerFrontendFooter(name, success, &miss);
  return reg;
}


void BaseLoadStubCompiler::NonexistentHandlerFrontend(
    Handle<JSObject> object,
    Handle<JSObject> last,
    Handle<Name> name,
    Label* success,
    Handle<GlobalObject> global) {
  Label miss;

  HandlerFrontendHeader(object, receiver(), last, name, &miss);

  // If the last object in the prototype chain is a global object,
  // check that the global property cell is empty.
  if (!global.is_null()) {
    GenerateCheckPropertyCell(masm(), global, name,
                              scratch1(), scratch2(), &miss);
  }

  HandlerFrontendFooter(name, success, &miss);
}


void BaseLoadStubCompiler::GenerateLoadField(Register reg,
                                             Handle<JSObject> holder,
                                             PropertyIndex field,
                                             Representation representation) {
  __ Mov(receiver(), reg);
  if (kind() == Code::LOAD_IC) {
    LoadFieldStub stub(field.is_inobject(holder),
                       field.translate(holder),
                       representation);
    GenerateTailCall(masm(), stub.GetCode(isolate()));
  } else {
    KeyedLoadFieldStub stub(field.is_inobject(holder),
                            field.translate(holder),
                            representation);
    GenerateTailCall(masm(), stub.GetCode(isolate()));
  }
}


void BaseLoadStubCompiler::GenerateLoadConstant(Handle<Object> value) {
  // Return the constant value.
  __ LoadObject(x0, value);
  __ Ret();
}


void BaseLoadStubCompiler::GenerateLoadCallback(
    Register reg,
    Handle<ExecutableAccessorInfo> callback) {
  // Build ExecutableAccessorInfo::args_ list on the stack and push property
  // name below the exit frame to make GC aware of them and store pointers to
  // them.
  __ Push(receiver());
  Register args_addr = scratch2();
  __ Mov(args_addr, __ StackPointer());

  if (heap()->InNewSpace(callback->data())) {
    __ Mov(scratch3(), Operand(callback));
    __ Ldr(scratch3(), FieldMemOperand(scratch3(),
                                       ExecutableAccessorInfo::kDataOffset));
  } else {
    __ Mov(scratch3(), Operand(Handle<Object>(callback->data(), isolate())));
  }
  // TODO(jbramley): Find another scratch register and combine the pushes
  // together. Can we use scratch1() and scratch2() here?
  __ Push(reg, scratch3());
  __ LoadRoot(scratch3(), Heap::kUndefinedValueRootIndex);
  __ Mov(scratch4(), Operand(ExternalReference::isolate_address(isolate())));
  __ Push(scratch3(), scratch3(), scratch4(), name());

  // Pass the Handle<Name> of the property name to the runtime.
  __ Mov(x0, __ StackPointer());

  FrameScope frame_scope(masm(), StackFrame::MANUAL);
  const int kApiStackSpace = 1;
  __ EnterExitFrame(false, scratch4(),
      kApiStackSpace + MacroAssembler::kCallApiFunctionSpillSpace);

  // Create ExecutableAccessorInfo instance on the stack above the exit frame
  // before the return address. ExecutableAccessorInfo has only one field: the
  // address of args_.
  __ Poke(args_addr, 1 * kPointerSize);

  // Get the address of ExecutableAccessorInfo instance and pass it to the
  // runtime.
  __ Add(x1, __ StackPointer(), 1 * kPointerSize);

  // CallApiFunctionAndReturn can spill registers inside the exit frame, after
  // the return address and the ExecutableAccessorInfo instance.
  const int spill_offset = 1 + kApiStackSpace;

  // After the call to the API function we need to free memory used for:
  //  - the holder
  //  - the callback data
  //  - the isolate
  //  - the property name
  //  - the receiver.
  //
  // The memory allocated inside the ExitFrame will be freed when we'll leave
  // the ExitFrame in CallApiFunctionAndReturn.
  const int kStackUnwindSpace = kFastApiCallArguments + 1;

  // Do the API call.
  Address getter_address = v8::ToCData<Address>(callback->getter());
  bool returns_handle = !CallbackTable::ReturnsVoid(isolate(), getter_address);

  ApiFunction fun(getter_address);
  ExternalReference::Type type =
      returns_handle ?
          ExternalReference::DIRECT_GETTER_CALL :
          ExternalReference::DIRECT_GETTER_CALL_NEW;
  ExternalReference ref = ExternalReference(&fun, type, isolate());

  Address thunk_address = returns_handle
      ? FUNCTION_ADDR(&InvokeAccessorGetter)
      : FUNCTION_ADDR(&InvokeAccessorGetterCallback);
  ExternalReference::Type thunk_type =
      returns_handle ?
          ExternalReference::PROFILING_GETTER_CALL :
          ExternalReference::PROFILING_GETTER_CALL_NEW;
  ApiFunction thunk_fun(thunk_address);
  ExternalReference thunk_ref =
      ExternalReference(&thunk_fun, thunk_type, isolate());

  // TODO(jbramley): I don't know where '5' comes from, but this goes away at
  // some point.
  __ CallApiFunctionAndReturn(ref,
                              getter_address,
                              thunk_ref,
                              x2,
                              kStackUnwindSpace,
                              spill_offset,
                              returns_handle,
                              5);
}


void BaseLoadStubCompiler::GenerateLoadInterceptor(
    Register holder_reg,
    Handle<JSObject> object,
    Handle<JSObject> interceptor_holder,
    LookupResult* lookup,
    Handle<Name> name) {
  ASSERT(!AreAliased(receiver(), this->name(),
                     scratch1(), scratch2(), scratch3()));
  ASSERT(interceptor_holder->HasNamedInterceptor());
  ASSERT(!interceptor_holder->GetNamedInterceptor()->getter()->IsUndefined());

  // So far the most popular follow ups for interceptor loads are FIELD
  // and CALLBACKS, so inline only them, other cases may be added later.
  bool compile_followup_inline = false;
  if (lookup->IsFound() && lookup->IsCacheable()) {
    if (lookup->IsField()) {
      compile_followup_inline = true;
    } else if (lookup->type() == CALLBACKS &&
               lookup->GetCallbackObject()->IsExecutableAccessorInfo()) {
      ExecutableAccessorInfo* callback =
          ExecutableAccessorInfo::cast(lookup->GetCallbackObject());
      compile_followup_inline = callback->getter() != NULL &&
          callback->IsCompatibleReceiver(*object);
    }
  }

  if (compile_followup_inline) {
    // Compile the interceptor call, followed by inline code to load the
    // property from further up the prototype chain if the call fails.
    // Check that the maps haven't changed.
    ASSERT(holder_reg.is(receiver()) || holder_reg.is(scratch1()));

    // Preserve the receiver register explicitly whenever it is different from
    // the holder and it is needed should the interceptor return without any
    // result. The CALLBACKS case needs the receiver to be passed into C++ code,
    // the FIELD case might cause a miss during the prototype check.
    bool must_perfrom_prototype_check = *interceptor_holder != lookup->holder();
    bool must_preserve_receiver_reg = !receiver().Is(holder_reg) &&
        (lookup->type() == CALLBACKS || must_perfrom_prototype_check);

    // Save necessary data before invoking an interceptor.
    // Requires a frame to make GC aware of pushed pointers.
    {
      FrameScope frame_scope(masm(), StackFrame::INTERNAL);
      if (must_preserve_receiver_reg) {
        __ Push(receiver(), holder_reg, this->name());
      } else {
        __ Push(holder_reg, this->name());
      }
      // Invoke an interceptor.  Note: map checks from receiver to
      // interceptor's holder has been compiled before (see a caller
      // of this method.)
      CompileCallLoadPropertyWithInterceptor(masm(),
                                             receiver(),
                                             holder_reg,
                                             this->name(),
                                             interceptor_holder);
      // Check if interceptor provided a value for property.  If it's
      // the case, return immediately.
      Label interceptor_failed;
      __ JumpIfRoot(x0,
                    Heap::kNoInterceptorResultSentinelRootIndex,
                    &interceptor_failed);
      frame_scope.GenerateLeaveFrame();
      __ Ret();

      __ Bind(&interceptor_failed);
      if (must_preserve_receiver_reg) {
        __ Pop(this->name(), holder_reg, receiver());
      } else {
        __ Pop(this->name(), holder_reg);
      }
      // Leave the internal frame.
    }
    GenerateLoadPostInterceptor(holder_reg, interceptor_holder, name, lookup);
  } else {  // !compile_followup_inline
    // Call the runtime system to load the interceptor.
    // Check that the maps haven't changed.
    PushInterceptorArguments(
        masm(), receiver(), holder_reg, this->name(), interceptor_holder);

    ExternalReference ref =
        ExternalReference(IC_Utility(IC::kLoadPropertyWithInterceptorForLoad),
                          isolate());
    __ TailCallExternalReference(ref, 6, 1);
  }
}


void CallStubCompiler::GenerateNameCheck(Handle<Name> name, Label* miss) {
  Register name_reg = x2;

  if (kind_ == Code::KEYED_CALL_IC) {
    __ Cmp(name_reg, Operand(name));
    __ B(ne, miss);
  }
}


// The receiver is loaded from the stack and left in x0 register.
void CallStubCompiler::GenerateGlobalReceiverCheck(Handle<JSObject> object,
                                                   Handle<JSObject> holder,
                                                   Handle<Name> name,
                                                   Label* miss) {
  ASSERT(holder->IsGlobalObject());

  const int argc = arguments().immediate();

  // Get the receiver from the stack.
  Register receiver = x0;
  __ Peek(receiver, argc * kPointerSize);

  // Check that the maps haven't changed.
  __ JumpIfSmi(receiver, miss);
  CheckPrototypes(object, receiver, holder, x3, x1, x4, name, miss);
}


// Load the function object into x1 register.
void CallStubCompiler::GenerateLoadFunctionFromCell(
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Label* miss) {
  // Get the value from the cell.
  __ Mov(x3, Operand(cell));
  Register function_reg = x1;
  __ Ldr(function_reg, FieldMemOperand(x3, Cell::kValueOffset));

  // Check that the cell contains the same function.
  if (heap()->InNewSpace(*function)) {
    // We can't embed a pointer to a function in new space so we have
    // to verify that the shared function info is unchanged. This has
    // the nice side effect that multiple closures based on the same
    // function can all use this call IC. Before we load through the
    // function, we have to verify that it still is a function.
    __ JumpIfSmi(function_reg, miss);
    __ JumpIfNotObjectType(function_reg, x3, x3, JS_FUNCTION_TYPE, miss);

    // Check the shared function info. Make sure it hasn't changed.
    __ Mov(x3, Operand(Handle<SharedFunctionInfo>(function->shared())));
    __ Ldr(x4,
        FieldMemOperand(function_reg, JSFunction::kSharedFunctionInfoOffset));
    __ Cmp(x4, x3);
  } else {
    __ Cmp(function_reg, Operand(function));
  }
  __ B(ne, miss);
}


void CallStubCompiler::GenerateMissBranch() {
  Handle<Code> code =
      isolate()->stub_cache()->ComputeCallMiss(arguments().immediate(),
                                               kind_,
                                               extra_state_);
  __ Jump(code, RelocInfo::CODE_TARGET);
}


Handle<Code> CallStubCompiler::CompileCallField(Handle<JSObject> object,
                                                Handle<JSObject> holder,
                                                PropertyIndex index,
                                                Handle<Name> name) {
  // ----------- S t a t e -------------
  //  -- x2    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;
  const int argc = arguments().immediate();

  GenerateNameCheck(name, &miss);

  // Get the receiver of the function from the stack.
  Register receiver = x0;
  __ Peek(receiver, argc * kXRegSizeInBytes);
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Do the right check and compute the holder register.
  Register holder_reg = CheckPrototypes(
      object, receiver, holder, x1, x3, x4, name, &miss);
  Register function = x1;
  GenerateFastPropertyLoad(masm(), function, holder_reg,
                           index.is_inobject(holder),
                           index.translate(holder),
                           Representation::Tagged());

  GenerateCallFunction(
      masm(), object, arguments(), &miss, extra_state_, function, receiver, x3);

  // Handle call cache miss.
  __ Bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(Code::FIELD, name);
}


Handle<Code> CallStubCompiler::CompileArrayCodeCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- x2    : name
  //  -- lr    : return address
  //  -- sp[(argc - n - 1) * 8] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 8]           : receiver
  // -----------------------------------
  Label miss;

  // Check that function is still array.
  const int argc = arguments().immediate();
  GenerateNameCheck(name, &miss);

  Register receiver = x1;
  if (cell.is_null()) {
    __ Peek(receiver, argc * kPointerSize);

    // Check that the receiver isn't a smi.
    __ JumpIfSmi(receiver, &miss);

    // Check that the maps haven't changed.
    CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder, x3, x0,
                    x4, name, &miss);
  } else {
    ASSERT(cell->value() == *function);
    GenerateGlobalReceiverCheck(Handle<JSObject>::cast(object), holder, name,
                                &miss);
    GenerateLoadFunctionFromCell(cell, function, &miss);
  }

  Handle<AllocationSite> site = isolate()->factory()->NewAllocationSite();
  site->set_transition_info(Smi::FromInt(GetInitialFastElementsKind()));
  Handle<Cell> site_feedback_cell = isolate()->factory()->NewCell(site);
  __ Mov(x0, argc);
  __ Mov(x1, Operand(function));
  __ Mov(x2, Operand(site_feedback_cell));

  ArrayConstructorStub stub(isolate());
  __ TailCallStub(&stub);

  __ Bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileArrayPushCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- x2    : name (Must be preserved on miss.)
  //  -- lr    : return address
  //  -- sp[(argc - n - 1) * 8] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 8]           : receiver
  // -----------------------------------

  // If object is not an array, bail out to regular call.
  if (!object->IsJSArray() || !cell.is_null()) return Handle<Code>::null();

  Label miss;
  Register result = x0;
  const int argc = arguments().immediate();

  GenerateNameCheck(name, &miss);

  // Get the receiver from the stack
  Register receiver = x1;
  __ Peek(receiver, argc * kPointerSize);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Check that the maps haven't changed.
  CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder, x3, x0, x4,
                  name, &miss);

  if (argc == 0) {
    // Nothing to do, just return the length.
    __ Ldr(result, FieldMemOperand(receiver, JSArray::kLengthOffset));
    __ Drop(argc + 1);
    __ Ret();
  } else {
    Label call_builtin;

    if (argc == 1) {  // Otherwise fall through to call the builtin.
      Label attempt_to_grow_elements, with_write_barrier, check_double;

      // Note that even though we assign the array length to x0 and the value
      // to push in x4, they are not always live. Both x0 and x4 can be locally
      // reused as scratch registers.
      Register length = x0;
      Register value = x4;
      Register elements = x6;
      Register end_elements = x5;
      // Get the elements array of the object.
      __ Ldr(elements, FieldMemOperand(receiver, JSArray::kElementsOffset));

      // Check that the elements are in fast mode and writable.
      __ CheckMap(elements,
                  x0,
                  Heap::kFixedArrayMapRootIndex,
                  &check_double,
                  DONT_DO_SMI_CHECK);

      // Get the array's length and calculate new length.
      __ Ldr(length, FieldMemOperand(receiver, JSArray::kLengthOffset));
      STATIC_ASSERT(kSmiTag == 0);
      __ Add(length, length, Operand(Smi::FromInt(argc)));

      // Check if we could survive without allocation.
      __ Ldr(x4, FieldMemOperand(elements, FixedArray::kLengthOffset));
      __ Cmp(length, x4);
      __ B(gt, &attempt_to_grow_elements);

      // Check if value is a smi.
      __ Peek(value, (argc - 1) * kPointerSize);
      __ JumpIfNotSmi(value, &with_write_barrier);

      // Save new length.
      __ Str(length, FieldMemOperand(receiver, JSArray::kLengthOffset));

      // Store the value.
      // We may need a register containing the address end_elements below,
      // so write back the value in end_elements.
      __ Add(end_elements, elements,
             Operand::UntagSmiAndScale(length, kPointerSizeLog2));
      const int kEndElementsOffset =
          FixedArray::kHeaderSize - kHeapObjectTag - argc * kPointerSize;
      __ Str(value, MemOperand(end_elements, kEndElementsOffset, PreIndex));

      // Check for a smi.
      __ Drop(argc + 1);
      __ Ret();

      __ Bind(&check_double);
      // Check that the elements are in fast mode and writable.
      __ CheckMap(elements,
                  x0,
                  Heap::kFixedDoubleArrayMapRootIndex,
                  &call_builtin,
                  DONT_DO_SMI_CHECK);

      // Get the array's length and calculate new length.
      Register old_length = x5;
      __ Ldr(old_length, FieldMemOperand(receiver, JSArray::kLengthOffset));
      STATIC_ASSERT(kSmiTag == 0);
      __ Add(length, old_length, Operand(Smi::FromInt(argc)));

      // Check if we could survive without allocation.
      __ Ldr(x4, FieldMemOperand(elements, FixedArray::kLengthOffset));
      __ Cmp(length, x4);
      __ B(gt, &call_builtin);

      __ Peek(value, (argc - 1) * kPointerSize);
      __ StoreNumberToDoubleElements(
          value, old_length, elements, x3, d0, d1,
          &call_builtin);

      // Save new length.
      __ Str(length, FieldMemOperand(receiver, JSArray::kLengthOffset));

      // Check for a smi.
      __ Drop(argc + 1);
      __ Ret();


      __ Bind(&with_write_barrier);
      Register map = x3;
      __ Ldr(map, FieldMemOperand(receiver, HeapObject::kMapOffset));

      if (FLAG_smi_only_arrays  && !FLAG_trace_elements_transitions) {
        Label fast_object, not_fast_object;
        __ CheckFastObjectElements(map, x7, &not_fast_object);
        __ B(&fast_object);

        // In case of fast smi-only, convert to fast object, otherwise bail out.
        __ Bind(&not_fast_object);
        __ CheckFastSmiElements(map, x7, &call_builtin);

        __ Ldr(x7, FieldMemOperand(x4, HeapObject::kMapOffset));
        __ JumpIfRoot(x7, Heap::kHeapNumberMapRootIndex, &call_builtin);

        Label try_holey_map;
        __ LoadTransitionedArrayMapConditional(FAST_SMI_ELEMENTS,
                                               FAST_ELEMENTS,
                                               map,
                                               x7,
                                               &try_holey_map);
        // GenerateMapChangeElementsTransition expects the receiver to be in x2.
        // Since from this point we cannot jump on 'miss' it is ok to clobber
        // x2 (which initialy contained called function name).
        __ Mov(x2, receiver);
        ElementsTransitionGenerator::
            GenerateMapChangeElementsTransition(masm(),
                                                DONT_TRACK_ALLOCATION_SITE,
                                                NULL);
        __ B(&fast_object);

        __ Bind(&try_holey_map);
        __ LoadTransitionedArrayMapConditional(FAST_HOLEY_SMI_ELEMENTS,
                                               FAST_HOLEY_ELEMENTS,
                                               map,
                                               x7,
                                               &call_builtin);
        // The previous comment about x2 usage also applies here.
        __ Mov(x2, receiver);
        ElementsTransitionGenerator::
            GenerateMapChangeElementsTransition(masm(),
                                                DONT_TRACK_ALLOCATION_SITE,
                                                NULL);
        __ Bind(&fast_object);
      } else {
        __ CheckFastObjectElements(map, x3, &call_builtin);
      }

      // Save new length.
      __ Str(length, FieldMemOperand(receiver, JSArray::kLengthOffset));

      // Store the value.
      // We may need a register containing the address end_elements below,
      // so write back the value in end_elements.
      __ Add(end_elements, elements,
             Operand::UntagSmiAndScale(length, kPointerSizeLog2));
      __ Str(x4, MemOperand(end_elements, kEndElementsOffset, PreIndex));

      __ RecordWrite(elements,
                     end_elements,
                     x4,
                     kLRHasNotBeenSaved,
                     kDontSaveFPRegs,
                     EMIT_REMEMBERED_SET,
                     OMIT_SMI_CHECK);
      __ Drop(argc + 1);
      __ Ret();


      __ Bind(&attempt_to_grow_elements);
      // When we jump here, x4 must hold the length of elements.
      Register elements_length = x4;

      if (!FLAG_inline_new) {
        __ B(&call_builtin);
      }

      __ Peek(x2, (argc - 1) * kPointerSize);
      // Growing elements that are SMI-only requires special handling in case
      // the new element is non-Smi. For now, delegate to the builtin.
      Label no_fast_elements_check;
      __ JumpIfSmi(x2, &no_fast_elements_check);
      __ Ldr(x7, FieldMemOperand(receiver, HeapObject::kMapOffset));
      __ CheckFastObjectElements(x7, x7, &call_builtin);
      __ Bind(&no_fast_elements_check);

      ExternalReference new_space_allocation_top =
          ExternalReference::new_space_allocation_top_address(isolate());
      ExternalReference new_space_allocation_limit =
          ExternalReference::new_space_allocation_limit_address(isolate());

      const int kAllocationDelta = 4;
      // Load top and check if it is the end of elements.
      __ Add(end_elements, elements,
             Operand::UntagSmiAndScale(length, kPointerSizeLog2));
      __ Add(end_elements, end_elements, kEndElementsOffset);
      __ Mov(x7, Operand(new_space_allocation_top));
      __ Ldr(x3, MemOperand(x7));
      __ Cmp(end_elements, x3);
      __ B(ne, &call_builtin);

      __ Mov(x10, Operand(new_space_allocation_limit));
      __ Ldr(x10, MemOperand(x10));
      __ Add(x3, x3, kAllocationDelta * kPointerSize);
      __ Cmp(x3, x10);
      __ B(hi, &call_builtin);

      // We fit and could grow elements.
      // Update new_space_allocation_top.
      __ Str(x3, MemOperand(x7));
      // Push the argument.
      __ Str(x2, MemOperand(end_elements));
      // Fill the rest with holes.
      __ LoadRoot(x3, Heap::kTheHoleValueRootIndex);
      for (int i = 1; i < kAllocationDelta; i++) {
        __ Str(x3, MemOperand(end_elements, i * kPointerSize));
      }

      // Update elements' and array's sizes.
      __ Str(length, FieldMemOperand(receiver, JSArray::kLengthOffset));
      __ Add(elements_length,
             elements_length,
             Operand(Smi::FromInt(kAllocationDelta)));
      __ Str(elements_length,
             FieldMemOperand(elements, FixedArray::kLengthOffset));

      // Elements are in new space, so write barrier is not required.
      __ Drop(argc + 1);
      __ Ret();
    }
    __ Bind(&call_builtin);
    __ TailCallExternalReference(
        ExternalReference(Builtins::c_ArrayPush, isolate()), argc + 1, 1);
  }

  // Handle call cache miss.
  __ Bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileArrayPopCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- x2    : name
  //  -- lr    : return address
  //  -- sp[(argc - n - 1) * 8] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 8]           : receiver
  // -----------------------------------

  // If object is not an array, bail out to regular call.
  if (!object->IsJSArray() || !cell.is_null()) return Handle<Code>::null();

  const int argc = arguments().immediate();
  Register result = x0;
  Label miss, return_undefined, call_builtin;

  GenerateNameCheck(name, &miss);

  // Get the receiver from the stack
  Register receiver = x1;
  __ Peek(receiver, argc * kPointerSize);
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Check that the maps haven't changed.
  CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder,
                  x3, x4, x0, name, &miss);

  // Get the elements array of the object.
  Register elements = x3;
  __ Ldr(elements, FieldMemOperand(receiver, JSArray::kElementsOffset));

  // Check that the elements are in fast mode and writable.
  __ CheckMap(elements,
              x0,
              Heap::kFixedArrayMapRootIndex,
              &call_builtin,
              DONT_DO_SMI_CHECK);

  // Get the array's length and calculate new length.
  Register length = x4;
  __ Ldr(length, FieldMemOperand(receiver, JSArray::kLengthOffset));
  __ Subs(length, length, Operand(Smi::FromInt(1)));
  __ B(lt, &return_undefined);

  // Get the last element.
  __ Add(elements, elements,
         Operand::UntagSmiAndScale(length, kPointerSizeLog2));
  __ Ldr(result, FieldMemOperand(elements, FixedArray::kHeaderSize));
  __ JumpIfRoot(result, Heap::kTheHoleValueRootIndex, &call_builtin);

  // Set the array's length.
  __ Str(length, FieldMemOperand(receiver, JSArray::kLengthOffset));

  // Fill with the hole.
  Register hole_value = x6;
  __ LoadRoot(hole_value, Heap::kTheHoleValueRootIndex);
  __ Str(hole_value, FieldMemOperand(elements, FixedArray::kHeaderSize));
  __ Drop(argc + 1);
  __ Ret();

  __ Bind(&return_undefined);
  __ LoadRoot(result, Heap::kUndefinedValueRootIndex);
  __ Drop(argc + 1);
  __ Ret();

  __ Bind(&call_builtin);
  __ TailCallExternalReference(
      ExternalReference(Builtins::c_ArrayPop, isolate()), argc + 1, 1);

  // Handle call cache miss.
  __ Bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileStringCharCodeAtCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- x2                     : function name
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 8] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 8]           : receiver
  // -----------------------------------

  // If object is not a string, bail out to regular call.
  if (!object->IsString() || !cell.is_null()) return Handle<Code>::null();

  const int argc = arguments().immediate();
  Label miss;
  Label name_miss;
  Label index_out_of_range;
  Label* index_out_of_range_label = &index_out_of_range;

  if (kind_ == Code::CALL_IC &&
      (CallICBase::StringStubState::decode(extra_state_) ==
       DEFAULT_STRING_STUB)) {
    index_out_of_range_label = &miss;
  }
  GenerateNameCheck(name, &name_miss);

  // Check that the maps starting from the prototype haven't changed.
  Register prototype = x0;
  GenerateDirectLoadGlobalFunctionPrototype(masm(),
                                            Context::STRING_FUNCTION_INDEX,
                                            prototype,
                                            &miss);
  ASSERT(!object.is_identical_to(holder));
  CheckPrototypes(
      Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
      prototype, holder, x1, x3, x4, name, &miss);

  Register result = x0;
  Register receiver = x1;
  Register index = x4;

  __ Peek(receiver, argc * kPointerSize);
  if (argc > 0) {
    __ Peek(index, (argc - 1) * kPointerSize);
  } else {
    __ LoadRoot(index, Heap::kUndefinedValueRootIndex);
  }

  StringCharCodeAtGenerator generator(receiver,
                                      index,
                                      result,
                                      &miss,  // When not a string.
                                      &miss,  // When not a number.
                                      index_out_of_range_label,
                                      STRING_INDEX_IS_NUMBER);
  generator.GenerateFast(masm());
  __ Drop(argc + 1);
  __ Ret();

  StubRuntimeCallHelper call_helper;
  generator.GenerateSlow(masm(), call_helper);

  if (index_out_of_range.is_linked()) {
    __ Bind(&index_out_of_range);
    __ LoadRoot(result, Heap::kNanValueRootIndex);
    __ Drop(argc + 1);
    __ Ret();
  }

  __ Bind(&miss);
  // Restore function name in x2.
  __ Mov(x2, Operand(name));
  __ Bind(&name_miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileStringCharAtCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- x2                     : function name
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 8] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 8]           : receiver
  // -----------------------------------

  // If object is not a string, bail out to regular call.
  if (!object->IsString() || !cell.is_null()) return Handle<Code>::null();

  const int argc = arguments().immediate();
  Label miss;
  Label name_miss;
  Label index_out_of_range;
  Label* index_out_of_range_label = &index_out_of_range;

  if (kind_ == Code::CALL_IC &&
      (CallICBase::StringStubState::decode(extra_state_) ==
       DEFAULT_STRING_STUB)) {
    index_out_of_range_label = &miss;
  }
  GenerateNameCheck(name, &name_miss);

  // Check that the maps starting from the prototype haven't changed.
  Register prototype = x0;
  GenerateDirectLoadGlobalFunctionPrototype(masm(),
                                            Context::STRING_FUNCTION_INDEX,
                                            prototype,
                                            &miss);
  ASSERT(!object.is_identical_to(holder));
  CheckPrototypes(
      Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
      prototype, holder, x1, x3, x4, name, &miss);

  Register receiver = x0;
  Register index = x4;
  Register scratch = x3;
  Register result = x0;

  __ Peek(receiver, argc * kPointerSize);
  if (argc > 0) {
    __ Peek(index, (argc - 1) * kPointerSize);
  } else {
    __ LoadRoot(index, Heap::kUndefinedValueRootIndex);
  }

  StringCharAtGenerator generator(receiver,
                                  index,
                                  scratch,
                                  result,
                                  &miss,  // When not a string.
                                  &miss,  // When not a number.
                                  index_out_of_range_label,
                                  STRING_INDEX_IS_NUMBER);
  generator.GenerateFast(masm());
  __ Drop(argc + 1);
  __ Ret();

  StubRuntimeCallHelper call_helper;
  generator.GenerateSlow(masm(), call_helper);

  if (index_out_of_range.is_linked()) {
    __ Bind(&index_out_of_range);
    __ LoadRoot(result, Heap::kempty_stringRootIndex);
    __ Drop(argc + 1);
    __ Ret();
  }

  __ Bind(&miss);
  // Restore function name in x2.
  __ Mov(x2, Operand(name));
  __ Bind(&name_miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileStringFromCharCodeCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- x2                     : function name
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 8] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 8]           : receiver
  // -----------------------------------
  const int argc = arguments().immediate();

  // If the object is not a JSObject or we got an unexpected number of
  // arguments, bail out to the regular call.
  if (!object->IsJSObject() || argc != 1) return Handle<Code>::null();

  Label miss;
  GenerateNameCheck(name, &miss);

  if (cell.is_null()) {
    Register receiver = x1;
    __ Peek(receiver, kPointerSize);
    __ JumpIfSmi(receiver, &miss);

    CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder,
                    x0, x3, x4, name, &miss);
  } else {
    ASSERT(cell->value() == *function);
    GenerateGlobalReceiverCheck(Handle<JSObject>::cast(object), holder, name,
                                &miss);
    GenerateLoadFunctionFromCell(cell, function, &miss);
  }

  // Load the char code argument.
  Register code = x1;
  __ Peek(code, 0);

  // Check the code is a smi.
  Label slow;
  __ JumpIfNotSmi(code, &slow);

  // Make sure the smi code is a uint16.
  __ And(code, code, Operand(Smi::FromInt(0xffff)));

  Register result = x0;
  StringCharFromCodeGenerator generator(code, result);
  generator.GenerateFast(masm());
  __ Drop(argc + 1);
  __ Ret();

  StubRuntimeCallHelper call_helper;
  generator.GenerateSlow(masm(), call_helper);

  // Tail call the full function. We do not have to patch the receiver
  // because the function makes no use of it.
  __ Bind(&slow);
  ParameterCount expected(function);
  __ InvokeFunction(function, expected, arguments(),
                    JUMP_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);

  __ Bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileMathFloorCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- x2                     : function name (must be preserved on miss)
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 8] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 8]           : receiver
  // -----------------------------------
  Label miss;
  Label return_result;
  Register result = x0;
  const int argc = arguments().immediate();

  // If the object is not a JSObject or we got an unexpected number of
  // arguments, bail out to the regular call.
  if (!object->IsJSObject() || argc != 1) return Handle<Code>::null();

  GenerateNameCheck(name, &miss);

  if (cell.is_null()) {
    Register receiver = x1;
    __ Peek(receiver, kPointerSize);
    __ JumpIfSmi(receiver, &miss);
    CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder,
                    x0, x3, x4, name, &miss);
  } else {
    ASSERT(cell->value() == *function);
    GenerateGlobalReceiverCheck(
        Handle<JSObject>::cast(object), holder, name, &miss);
    GenerateLoadFunctionFromCell(cell, function, &miss);
  }

  // Load the (only) argument.
  Register arg = x0;
  __ Peek(arg, 0);

  // If the argument is a smi, just return.
  __ JumpIfSmi(arg, &return_result);

  // Load the HeapNumber.
  Label slow;
  __ CheckMap(arg, x1, Heap::kHeapNumberMapRootIndex, &slow, DONT_DO_SMI_CHECK);

  FPRegister double_value = d0;
  __ Ldr(double_value, FieldMemOperand(arg, HeapNumber::kValueOffset));

  // Try to do the conversion and check for overflow.
  Label zero_or_overflow;
  Register int_value = x3;
  __ Fcvtms(int_value, double_value);
  __ Cmp(int_value, Smi::kMaxValue);
  __ Ccmp(int_value, Smi::kMinValue, NFlag, le);
  // If the second comparison is skipped, we will have N=1 and V=0, this will
  // force the following "lt" condition to be true.
  __ B(lt, &zero_or_overflow);

  Label smi_result;
  __ Cbnz(int_value, &smi_result);

  __ Bind(&zero_or_overflow);
  Register value = x1;
  __ Fmov(value, double_value);

  // Extract the exponent.
  // TODO(all): The constants in the HeapNumber class assume that the double
  // is stored in two 32-bit registers. They should assume offset within a
  // 64-bit register on 64-bit systems. However if we want to change that we
  // have to make some changes in x64 back-end.
  static const int exponent_shift =
      CountTrailingZeros(Double::kExponentMask, 64);
  static const int exponent_width = CountSetBits(Double::kExponentMask, 64);
  Register exponent = x3;
  __ Ubfx(exponent, value, exponent_shift, exponent_width);

  // Check for NaN, Infinity, and -Infinity. They are invariant through
  // a Math.Floor call, so just return the original argument.
  __ Cmp(exponent, Double::kExponentMask >> exponent_shift);
  __ B(&return_result, eq);

  // If the exponent is null, the number was 0 or -0. Otherwise the result
  // can't fit in a smi and we go to the slow path.
  __ Cbnz(exponent, &slow);

  // Check for -0.
  // If our HeapNumber is negative it was -0, so we just return it.
  __ TestAndBranchIfAnySet(value, Double::kSignMask, &return_result);

  __ Bind(&smi_result);
  // Tag and return the result.
  __ SmiTag(result, int_value);

  __ Bind(&return_result);
  __ Drop(argc + 1);
  __ Ret();

  __ Bind(&slow);
  // Tail call the full function. We do not have to patch the receiver
  // because the function makes no use of it.
  ParameterCount expected(function);
  __ InvokeFunction(function, expected, arguments(),
                    JUMP_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);

  __ Bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileMathAbsCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- x2                     : function name
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 8] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 8]           : receiver
  // -----------------------------------

  const int argc = arguments().immediate();

  // If the object is not a JSObject or we got an unexpected number of
  // arguments, bail out to the regular call.
  if (!object->IsJSObject() || argc != 1) return Handle<Code>::null();

  Register result = x0;
  Label miss, slow;
  GenerateNameCheck(name, &miss);

  if (cell.is_null()) {
    Register receiver = x1;
    __ Peek(receiver, kPointerSize);
    __ JumpIfSmi(receiver, &miss);
    CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder,
                    x0, x3, x4, name, &miss);
  } else {
    ASSERT(cell->value() == *function);
    GenerateGlobalReceiverCheck(Handle<JSObject>::cast(object), holder, name,
                                &miss);
    GenerateLoadFunctionFromCell(cell, function, &miss);
  }

  // Load the (only) argument.
  Register arg = x0;
  __ Peek(arg, 0);

  // Check if the argument is a smi.
  Label not_smi;
  __ JumpIfNotSmi(arg, &not_smi);

  __ SmiAbs(arg, x1, &slow);
  // Smi case done.
  __ Drop(argc + 1);
  __ Ret();

  // Check if the argument is a heap number and load its value.
  __ Bind(&not_smi);
  __ CheckMap(
      arg, x1, Heap::kHeapNumberMapRootIndex, &slow, DONT_DO_SMI_CHECK);
  Register value = x1;
  __ Ldr(value, FieldMemOperand(arg, HeapNumber::kValueOffset));

  // Check the sign of the argument. If the argument is positive, return it.
  Label negative_sign;
  __ TestAndBranchIfAnySet(value, Double::kSignMask, &negative_sign);
  __ Drop(argc + 1);
  __ Ret();

  __ Bind(&negative_sign);
  FPRegister double_value = d0;
  __ Fmov(double_value, value);
  __ Fabs(double_value, double_value);
  __ AllocateHeapNumberWithValue(result, double_value, &slow, x1, x3);
  __ Drop(argc + 1);
  __ Ret();

  // Tail call the full function. We do not have to patch the receiver
  // because the function makes no use of it.
  __ Bind(&slow);
  ParameterCount expected(function);
  __ InvokeFunction(function, expected, arguments(),
                    JUMP_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);

  __ Bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileFastApiCall(
    const CallOptimization& optimization,
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  Counters* counters = isolate()->counters();

  ASSERT(optimization.is_simple_api_call());
  // Bail out if object is a global object as we don't want to
  // repatch it to global receiver.
  if (object->IsGlobalObject()) return Handle<Code>::null();
  if (!cell.is_null()) return Handle<Code>::null();
  if (!object->IsJSObject()) return Handle<Code>::null();
  int depth = optimization.GetPrototypeDepthOfExpectedType(
      Handle<JSObject>::cast(object), holder);
  if (depth == kInvalidProtoDepth) return Handle<Code>::null();

  Label miss, miss_before_stack_reserved;
  GenerateNameCheck(name, &miss_before_stack_reserved);

  const int argc = arguments().immediate();

  // Get the receiver from the stack.
  Register receiver = x1;
  __ Peek(receiver, argc * kPointerSize);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss_before_stack_reserved);

  __ IncrementCounter(counters->call_const(), 1, x0, x3);
  __ IncrementCounter(counters->call_const_fast_api(), 1, x0, x3);

  ReserveSpaceForFastApiCall(masm(), x0);

  // Check that the maps haven't changed and find a Holder as a side effect.
  CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder, x0, x3, x4,
                  name, depth, &miss);

  GenerateFastApiDirectCall(masm(), optimization, argc);

  __ Bind(&miss);
  FreeSpaceForFastApiCall(masm());

  __ Bind(&miss_before_stack_reserved);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(function);
}


void CallStubCompiler::CompileHandlerFrontend(Handle<Object> object,
                                              Handle<JSObject> holder,
                                              Handle<Name> name,
                                              CheckType check,
                                              Label* success) {
  // ----------- S t a t e -------------
  //  -- x2    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;
  GenerateNameCheck(name, &miss);

  // Get the receiver from the stack.
  const int argc = arguments().immediate();
  Register receiver = x1;
  __ Peek(receiver, argc * kPointerSize);

  // Check that the receiver isn't a smi.
  if (check != NUMBER_CHECK) {
    __ JumpIfSmi(receiver, &miss);
  }

  // Make sure that it's okay not to patch the on stack receiver
  // unless we're doing a receiver map check.
  ASSERT(!object->IsGlobalObject() || check == RECEIVER_MAP_CHECK);

  switch (check) {
    case RECEIVER_MAP_CHECK: {
      __ IncrementCounter(isolate()->counters()->call_const(), 1, x0, x3);

      // Check that the maps haven't changed.
      CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder,
                      x0, x3, x4, name, &miss);

      // Patch the receiver on the stack with the global proxy if necessary.
      if (object->IsGlobalObject()) {
        __ Ldr(x3,
               FieldMemOperand(receiver, GlobalObject::kGlobalReceiverOffset));
        __ Poke(x3,  argc * kPointerSize);
      }
      break;
    }
    case STRING_CHECK: {
      // Check that the object is a string.
      __ JumpIfObjectType(receiver, x3, x3, FIRST_NONSTRING_TYPE, &miss, ge);
      // Check that the maps starting from the prototype haven't changed.
      Register prototype = x0;
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::STRING_FUNCTION_INDEX, prototype, &miss);
      CheckPrototypes(
          Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
          prototype, holder, x3, x1, x4, name, &miss);
      break;
    }
    case SYMBOL_CHECK: {
      // Check that the object is a symbol.
      __ JumpIfNotObjectType(receiver, x3, x3, SYMBOL_TYPE, &miss);
      // Check that the maps starting from the prototype haven't changed.
      Register prototype = x0;
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::SYMBOL_FUNCTION_INDEX, prototype, &miss);
      CheckPrototypes(
          Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
          prototype, holder, x3, x1, x4, name, &miss);
      break;
    }
    case NUMBER_CHECK: {
      Label fast;
      // Check that the object is a smi or a heap number.
      __ JumpIfSmi(receiver, &fast);
      __ JumpIfNotObjectType(receiver, x0, x0, HEAP_NUMBER_TYPE, &miss);

      __ Bind(&fast);
      // Check that the maps starting from the prototype haven't changed.
      Register prototype = x0;
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::NUMBER_FUNCTION_INDEX, prototype, &miss);
      CheckPrototypes(
          Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
          prototype, holder, x3, x1, x4, name, &miss);
      break;
    }
    case BOOLEAN_CHECK: {
      Label fast;
      // Check that the object is a boolean.
      __ JumpIfRoot(receiver, Heap::kTrueValueRootIndex, &fast);
      __ JumpIfNotRoot(receiver, Heap::kFalseValueRootIndex, &miss);

      __ Bind(&fast);
      // Check that the maps starting from the prototype haven't changed.
      Register prototype = x0;
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::BOOLEAN_FUNCTION_INDEX, prototype, &miss);
      CheckPrototypes(
          Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
          prototype, holder, x3, x1, x4, name, &miss);
      break;
    }
  }

  __ B(success);

  // Handle call cache miss.
  __ Bind(&miss);
  GenerateMissBranch();
}


void CallStubCompiler::CompileHandlerBackend(Handle<JSFunction> function) {
  CallKind call_kind = CallICBase::Contextual::decode(extra_state_)
      ? CALL_AS_FUNCTION
      : CALL_AS_METHOD;
  ParameterCount expected(function);
  __ InvokeFunction(function, expected, arguments(),
                    JUMP_FUNCTION, NullCallWrapper(), call_kind);
}


Handle<Code> CallStubCompiler::CompileCallConstant(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Name> name,
    CheckType check,
    Handle<JSFunction> function) {
  if (HasCustomCallGenerator(function)) {
    Handle<Code> code = CompileCustomCall(object, holder,
                                          Handle<Cell>::null(),
                                          function, Handle<String>::cast(name),
                                          Code::CONSTANT);
    // A null handle means bail out to the regular compiler code below.
    if (!code.is_null()) return code;
  }

  Label success;

  CompileHandlerFrontend(object, holder, name, check, &success);
  __ Bind(&success);
  CompileHandlerBackend(function);

  // Return the generated code.
  return GetCode(function);
}


Handle<Code> CallStubCompiler::CompileCallInterceptor(Handle<JSObject> object,
                                                      Handle<JSObject> holder,
                                                      Handle<Name> name) {
  // ----------- S t a t e -------------
  //  -- x2    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;
  Register name_reg = x2;

  GenerateNameCheck(name, &miss);

  const int argc = arguments().immediate();
  LookupResult lookup(isolate());
  LookupPostInterceptor(holder, name, &lookup);

  // Get the receiver from the stack.
  Register receiver = x5;
  __ Peek(receiver, argc * kPointerSize);

  CallInterceptorCompiler compiler(this, arguments(), name_reg, extra_state_);
  compiler.Compile(
      masm(), object, holder, name, &lookup, receiver, x3, x4, x0, &miss);

  // Move returned value, the function to call, to x1 (this is required by
  // GenerateCallFunction).
  Register function = x1;
  __ Mov(function, x0);

  // Restore receiver.
  __ Peek(receiver, argc * kPointerSize);

  GenerateCallFunction(
      masm(), object, arguments(), &miss, extra_state_, function, receiver, x3);

  // Handle call cache miss.
  __ Bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(Code::INTERCEPTOR, name);
}


Handle<Code> CallStubCompiler::CompileCallGlobal(
    Handle<JSObject> object,
    Handle<GlobalObject> holder,
    Handle<PropertyCell> cell,
    Handle<JSFunction> function,
    Handle<Name> name) {
  // ----------- S t a t e -------------
  //  -- x2    : name
  //  -- lr    : return address
  // -----------------------------------
  if (HasCustomCallGenerator(function)) {
    Handle<Code> code = CompileCustomCall(
        object, holder, cell, function, Handle<String>::cast(name),
        Code::NORMAL);
    // A null handle means bail out to the regular compiler code below.
    if (!code.is_null()) return code;
  }

  Label miss;
  GenerateNameCheck(name, &miss);

  // Get the number of arguments.
  const int argc = arguments().immediate();

  GenerateGlobalReceiverCheck(object, holder, name, &miss);
  GenerateLoadFunctionFromCell(cell, function, &miss);
  // After these two calls the receiver is left in x0 and the function in x1.
  Register receiver_reg = x0;
  Register function_reg = x1;

  // Patch the receiver on the stack with the global proxy if necessary.
  if (object->IsGlobalObject()) {
    __ Ldr(x3,
           FieldMemOperand(receiver_reg, GlobalObject::kGlobalReceiverOffset));
    __ Poke(x3, argc * kPointerSize);
  }

  // Set up the context.
  __ Ldr(cp, FieldMemOperand(function_reg, JSFunction::kContextOffset));

  // Jump to the cached code (tail call).
  Counters* counters = isolate()->counters();
  __ IncrementCounter(counters->call_global_inline(), 1, x3, x4);
  ParameterCount expected(function->shared()->formal_parameter_count());
  CallKind call_kind = CallICBase::Contextual::decode(extra_state_)
      ? CALL_AS_FUNCTION
      : CALL_AS_METHOD;
  // We call indirectly through the code field in the function to
  // allow recompilation to take effect without changing any of the
  // call sites.
  __ Ldr(x3, FieldMemOperand(function_reg, JSFunction::kCodeEntryOffset));
  __ InvokeCode(
      x3, expected, arguments(), JUMP_FUNCTION, NullCallWrapper(), call_kind);

  // Handle call cache miss.
  __ Bind(&miss);
  __ IncrementCounter(counters->call_global_inline_miss(), 1, x1, x3);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(Code::NORMAL, name);
}


Handle<Code> StoreStubCompiler::CompileStoreCallback(
    Handle<JSObject> object,
    Handle<JSObject> holder,
    Handle<Name> name,
    Handle<ExecutableAccessorInfo> callback) {
  ASM_LOCATION("StoreStubCompiler::CompileStoreCallback");

  Label success;
  HandlerFrontend(object, receiver(), holder, name, &success);
  __ Bind(&success);

  // Stub never generated for non-global objects that require access checks.
  ASSERT(holder->IsJSGlobalProxy() || !holder->IsAccessCheckNeeded());

  __ Mov(scratch1(), Operand(callback));
  __ Mov(scratch2(), Operand(name));
  __ Push(receiver(), scratch1(), scratch2(), value());

  // Do tail-call to the runtime system.
  ExternalReference store_callback_property =
      ExternalReference(IC_Utility(IC::kStoreCallbackProperty), isolate());
  __ TailCallExternalReference(store_callback_property, 4, 1);

  // Return the generated code.
  return GetCode(kind(), Code::CALLBACKS, name);
}


#undef __
#define __ ACCESS_MASM(masm)


void StoreStubCompiler::GenerateStoreViaSetter(
    MacroAssembler* masm,
    Handle<JSFunction> setter) {
  // ----------- S t a t e -------------
  //  -- x0    : value
  //  -- x1    : receiver
  //  -- x2    : name
  //  -- lr    : return address
  // -----------------------------------
  Register value_reg = x0;
  Register receiver_reg = x1;
  Label miss;

  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Save value register, so we can restore it later.
    __ Push(value_reg);

    if (!setter.is_null()) {
      // Call the JavaScript setter with receiver and value on the stack.
      __ Push(receiver_reg, value_reg);
      ParameterCount actual(1);
      ParameterCount expected(setter);
      __ InvokeFunction(setter, expected, actual,
                        CALL_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);
    } else {
      // If we generate a global code snippet for deoptimization only, remember
      // the place to continue after deoptimization.
      masm->isolate()->heap()->SetSetterStubDeoptPCOffset(masm->pc_offset());
    }

    // We have to return the passed value, not the return value of the setter.
    __ Pop(value_reg);

    // Restore context register.
    __ Ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  }
  __ Ret();
}


#undef __
#define __ ACCESS_MASM(masm())


Handle<Code> StoreStubCompiler::CompileStoreInterceptor(
    Handle<JSObject> object,
    Handle<Name> name) {
  Label miss;

  ASM_LOCATION("StoreStubCompiler::CompileStoreInterceptor");

  // Check that the map of the object hasn't changed.
  __ CheckMap(receiver(), scratch1(), Handle<Map>(object->map()), &miss,
              DO_SMI_CHECK);

  // Perform global security token check if needed.
  if (object->IsJSGlobalProxy()) {
    __ CheckAccessGlobalProxy(receiver(), scratch1(), &miss);
  }

  // Stub is never generated for non-global objects that require access checks.
  ASSERT(object->IsJSGlobalProxy() || !object->IsAccessCheckNeeded());

  __ Mov(scratch1(), Operand(Smi::FromInt(strict_mode())));
  __ Push(receiver(), this->name(), value(), scratch1());

  // Do tail-call to the runtime system.
  ExternalReference store_ic_property =
      ExternalReference(IC_Utility(IC::kStoreInterceptorProperty), isolate());
  __ TailCallExternalReference(store_ic_property, 4, 1);

  // Handle store cache miss.
  __ Bind(&miss);
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  return GetICCode(kind(), Code::INTERCEPTOR, name);
}


Handle<Code> StoreStubCompiler::CompileStoreGlobal(
    Handle<GlobalObject> object,
    Handle<PropertyCell> cell,
    Handle<Name> name) {
  Label miss;

  ASM_LOCATION("StoreStubCompiler::CompileStoreGlobal");

  // Check that the map of the global has not changed.
  __ Ldr(scratch1(), FieldMemOperand(receiver(), HeapObject::kMapOffset));
  __ Cmp(scratch1(), Operand(Handle<Map>(object->map())));
  __ B(ne, &miss);

  // Check that the value in the cell is not the hole. If it is, this
  // cell could have been deleted and reintroducing the global needs
  // to update the property details in the property dictionary of the
  // global object. We bail out to the runtime system to do that.
  __ Mov(scratch1(), Operand(cell));
  __ Ldr(scratch2(), FieldMemOperand(scratch1(),
                                     Cell::kValueOffset));
  __ JumpIfRoot(scratch2(), Heap::kTheHoleValueRootIndex, &miss);

  // Store the value in the cell.
  __ Str(value(), FieldMemOperand(scratch1(),
                                  Cell::kValueOffset));
  // Cells are always rescanned, so no write barrier here.

  Counters* counters = isolate()->counters();
  __ IncrementCounter(counters->named_store_global_inline(), 1,
                      scratch1(), scratch2());
  __ Ret();

  // Handle store cache miss.
  __ Bind(&miss);
  __ IncrementCounter(counters->named_store_global_inline_miss(), 1,
                      scratch1(), scratch2());
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  return GetICCode(kind(), Code::NORMAL, name);
}


Handle<Code> LoadStubCompiler::CompileLoadNonexistent(
    Handle<JSObject> object,
    Handle<JSObject> last,
    Handle<Name> name,
    Handle<GlobalObject> global) {
  Label success;
  NonexistentHandlerFrontend(object, last, name, &success, global);

  __ Bind(&success);
  // Return undefined if maps of the full prototype chain are still the
  // same and no global property with this name contains a value.
  __ LoadRoot(x0, Heap::kUndefinedValueRootIndex);
  __ Ret();

  // Return the generated code.
  return GetCode(kind(), Code::NONEXISTENT, name);
}


// TODO(all): The so-called scratch registers are significant in some cases. For
// example, KeyedStoreStubCompiler::registers()[3] (x3) is actually used for
// KeyedStoreCompiler::transition_map(). We should verify which registers are
// actually scratch registers, and which are important. For now, we use the same
// assignments as ARM to remain on the safe side.

Register* LoadStubCompiler::registers() {
  // receiver, name, scratch1, scratch2, scratch3, scratch4.
  static Register registers[] = { x0, x2, x3, x1, x4, x5 };
  return registers;
}


Register* KeyedLoadStubCompiler::registers() {
  // receiver, name/key, scratch1, scratch2, scratch3, scratch4.
  static Register registers[] = { x1, x0, x2, x3, x4, x5 };
  return registers;
}


Register* StoreStubCompiler::registers() {
  // receiver, name, value, scratch1, scratch2, scratch3.
  static Register registers[] = { x1, x2, x0, x3, x4, x5 };
  return registers;
}


Register* KeyedStoreStubCompiler::registers() {
  // receiver, name, value, scratch1, scratch2, scratch3.
  static Register registers[] = { x2, x1, x0, x3, x4, x5 };
  return registers;
}


void KeyedLoadStubCompiler::GenerateNameCheck(Handle<Name> name,
                                              Register name_reg,
                                              Label* miss) {
  __ Cmp(name_reg, Operand(name));
  __ B(ne, miss);
}


void KeyedStoreStubCompiler::GenerateNameCheck(Handle<Name> name,
                                               Register name_reg,
                                               Label* miss) {
  __ Cmp(name_reg, Operand(name));
  __ B(ne, miss);
}


#undef __
#define __ ACCESS_MASM(masm)

void LoadStubCompiler::GenerateLoadViaGetter(MacroAssembler* masm,
                                             Handle<JSFunction> getter) {
  // ----------- S t a t e -------------
  //  -- x0    : receiver
  //  -- x2    : name
  //  -- lr    : return address
  // -----------------------------------
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    if (!getter.is_null()) {
      // Call the JavaScript getter with the receiver on the stack.
      __ Push(x0);
      ParameterCount actual(0);
      ParameterCount expected(getter);
      __ InvokeFunction(getter, expected, actual,
                        CALL_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);
    } else {
      // If we generate a global code snippet for deoptimization only, remember
      // the place to continue after deoptimization.
      masm->isolate()->heap()->SetGetterStubDeoptPCOffset(masm->pc_offset());
    }

    // Restore context register.
    __ Ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  }
  __ Ret();
}


#undef __
#define __ ACCESS_MASM(masm())


Handle<Code> LoadStubCompiler::CompileLoadGlobal(
    Handle<JSObject> object,
    Handle<GlobalObject> global,
    Handle<PropertyCell> cell,
    Handle<Name> name,
    bool is_dont_delete) {
  Label success, miss;

  __ CheckMap(
      receiver(), scratch1(), Handle<Map>(object->map()), &miss, DO_SMI_CHECK);
  HandlerFrontendHeader(
      object, receiver(), Handle<JSObject>::cast(global), name, &miss);

  // Get the value from the cell.
  __ Mov(x3, Operand(cell));
  __ Ldr(x4, FieldMemOperand(x3, Cell::kValueOffset));

  // Check for deleted property if property can actually be deleted.
  if (!is_dont_delete) {
    __ JumpIfRoot(x4, Heap::kTheHoleValueRootIndex, &miss);
  }

  HandlerFrontendFooter(name, &success, &miss);
  __ Bind(&success);

  Counters* counters = isolate()->counters();
  __ IncrementCounter(counters->named_load_global_stub(), 1, x1, x3);
  __ Mov(x0, x4);
  __ Ret();

  // Return the generated code.
  return GetICCode(kind(), Code::NORMAL, name);
}


Handle<Code> BaseLoadStoreStubCompiler::CompilePolymorphicIC(
    MapHandleList* receiver_maps,
    CodeHandleList* handlers,
    Handle<Name> name,
    Code::StubType type,
    IcCheckType check) {
  Label miss;

  if (check == PROPERTY) {
    GenerateNameCheck(name, this->name(), &miss);
  }

  __ JumpIfSmi(receiver(), &miss);

  Register map_reg = scratch1();
  __ Ldr(map_reg, FieldMemOperand(receiver(), HeapObject::kMapOffset));
  int receiver_count = receiver_maps->length();
  int number_of_handled_maps = 0;
  for (int current = 0; current < receiver_count; ++current) {
    Handle<Map> map = receiver_maps->at(current);
    if (!map->is_deprecated()) {
      number_of_handled_maps++;
      Label try_next;
      __ Cmp(map_reg, Operand(receiver_maps->at(current)));
      __ B(ne, &try_next);
      __ Jump(handlers->at(current), RelocInfo::CODE_TARGET);
      __ Bind(&try_next);
    }
  }
  ASSERT(number_of_handled_maps != 0);

  __ Bind(&miss);
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  InlineCacheState state =
      (number_of_handled_maps > 1) ? POLYMORPHIC : MONOMORPHIC;
  return GetICCode(kind(), type, name, state);
}


Handle<Code> KeyedStoreStubCompiler::CompileStorePolymorphic(
    MapHandleList* receiver_maps,
    CodeHandleList* handler_stubs,
    MapHandleList* transitioned_maps) {
  Label miss;

  ASM_LOCATION("KeyedStoreStubCompiler::CompileStorePolymorphic");

  __ JumpIfSmi(receiver(), &miss);

  int receiver_count = receiver_maps->length();
  __ Ldr(scratch1(), FieldMemOperand(receiver(), HeapObject::kMapOffset));
  for (int i = 0; i < receiver_count; i++) {
    __ Cmp(scratch1(), Operand(receiver_maps->at(i)));

    Label skip;
    __ B(&skip, ne);
    if (!transitioned_maps->at(i).is_null()) {
      // This argument is used by the handler stub. For example, see
      // ElementsTransitionGenerator::GenerateMapChangeElementsTransition.
      __ Mov(transition_map(), Operand(transitioned_maps->at(i)));
    }
    __ Jump(handler_stubs->at(i), RelocInfo::CODE_TARGET);
    __ Bind(&skip);
  }

  __ Bind(&miss);
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  return GetICCode(
      kind(), Code::NORMAL, factory()->empty_string(), POLYMORPHIC);
}


#undef __
#define __ ACCESS_MASM(masm)

void KeyedLoadStubCompiler::GenerateLoadDictionaryElement(
    MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- x0     : key
  //  -- x1     : receiver
  // -----------------------------------
  Label slow, miss_force_generic;

  Register result = x0;
  Register key = x0;
  Register receiver = x1;

  __ JumpIfNotSmi(key, &miss_force_generic);
  __ Ldr(x4, FieldMemOperand(receiver, JSObject::kElementsOffset));
  __ LoadFromNumberDictionary(&slow, x4, key, result, x2, x3, x5, x6);
  __ Ret();

  __ Bind(&slow);
  __ IncrementCounter(
      masm->isolate()->counters()->keyed_load_external_array_slow(), 1, x2, x3);
  TailCallBuiltin(masm, Builtins::kKeyedLoadIC_Slow);

  // Miss case, call the runtime.
  __ Bind(&miss_force_generic);
  TailCallBuiltin(masm, Builtins::kKeyedLoadIC_MissForceGeneric);
}


} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_A64
