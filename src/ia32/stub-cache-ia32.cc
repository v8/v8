// Copyright 2012 the V8 project authors. All rights reserved.
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

#if V8_TARGET_ARCH_IA32

#include "ic-inl.h"
#include "codegen.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)


static void ProbeTable(Isolate* isolate,
                       MacroAssembler* masm,
                       Code::Flags flags,
                       StubCache::Table table,
                       Register name,
                       Register receiver,
                       // Number of the cache entry pointer-size scaled.
                       Register offset,
                       Register extra) {
  ExternalReference key_offset(isolate->stub_cache()->key_reference(table));
  ExternalReference value_offset(isolate->stub_cache()->value_reference(table));
  ExternalReference map_offset(isolate->stub_cache()->map_reference(table));

  Label miss;

  // Multiply by 3 because there are 3 fields per entry (name, code, map).
  __ lea(offset, Operand(offset, offset, times_2, 0));

  if (extra.is_valid()) {
    // Get the code entry from the cache.
    __ mov(extra, Operand::StaticArray(offset, times_1, value_offset));

    // Check that the key in the entry matches the name.
    __ cmp(name, Operand::StaticArray(offset, times_1, key_offset));
    __ j(not_equal, &miss);

    // Check the map matches.
    __ mov(offset, Operand::StaticArray(offset, times_1, map_offset));
    __ cmp(offset, FieldOperand(receiver, HeapObject::kMapOffset));
    __ j(not_equal, &miss);

    // Check that the flags match what we're looking for.
    __ mov(offset, FieldOperand(extra, Code::kFlagsOffset));
    __ and_(offset, ~Code::kFlagsNotUsedInLookup);
    __ cmp(offset, flags);
    __ j(not_equal, &miss);

#ifdef DEBUG
    if (FLAG_test_secondary_stub_cache && table == StubCache::kPrimary) {
      __ jmp(&miss);
    } else if (FLAG_test_primary_stub_cache && table == StubCache::kSecondary) {
      __ jmp(&miss);
    }
#endif

    // Jump to the first instruction in the code stub.
    __ add(extra, Immediate(Code::kHeaderSize - kHeapObjectTag));
    __ jmp(extra);

    __ bind(&miss);
  } else {
    // Save the offset on the stack.
    __ push(offset);

    // Check that the key in the entry matches the name.
    __ cmp(name, Operand::StaticArray(offset, times_1, key_offset));
    __ j(not_equal, &miss);

    // Check the map matches.
    __ mov(offset, Operand::StaticArray(offset, times_1, map_offset));
    __ cmp(offset, FieldOperand(receiver, HeapObject::kMapOffset));
    __ j(not_equal, &miss);

    // Restore offset register.
    __ mov(offset, Operand(esp, 0));

    // Get the code entry from the cache.
    __ mov(offset, Operand::StaticArray(offset, times_1, value_offset));

    // Check that the flags match what we're looking for.
    __ mov(offset, FieldOperand(offset, Code::kFlagsOffset));
    __ and_(offset, ~Code::kFlagsNotUsedInLookup);
    __ cmp(offset, flags);
    __ j(not_equal, &miss);

#ifdef DEBUG
    if (FLAG_test_secondary_stub_cache && table == StubCache::kPrimary) {
      __ jmp(&miss);
    } else if (FLAG_test_primary_stub_cache && table == StubCache::kSecondary) {
      __ jmp(&miss);
    }
#endif

    // Restore offset and re-load code entry from cache.
    __ pop(offset);
    __ mov(offset, Operand::StaticArray(offset, times_1, value_offset));

    // Jump to the first instruction in the code stub.
    __ add(offset, Immediate(Code::kHeaderSize - kHeapObjectTag));
    __ jmp(offset);

    // Pop at miss.
    __ bind(&miss);
    __ pop(offset);
  }
}


void StubCompiler::GenerateDictionaryNegativeLookup(MacroAssembler* masm,
                                                    Label* miss_label,
                                                    Register receiver,
                                                    Handle<Name> name,
                                                    Register scratch0,
                                                    Register scratch1) {
  ASSERT(name->IsUniqueName());
  ASSERT(!receiver.is(scratch0));
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->negative_lookups(), 1);
  __ IncrementCounter(counters->negative_lookups_miss(), 1);

  __ mov(scratch0, FieldOperand(receiver, HeapObject::kMapOffset));

  const int kInterceptorOrAccessCheckNeededMask =
      (1 << Map::kHasNamedInterceptor) | (1 << Map::kIsAccessCheckNeeded);

  // Bail out if the receiver has a named interceptor or requires access checks.
  __ test_b(FieldOperand(scratch0, Map::kBitFieldOffset),
            kInterceptorOrAccessCheckNeededMask);
  __ j(not_zero, miss_label);

  // Check that receiver is a JSObject.
  __ CmpInstanceType(scratch0, FIRST_SPEC_OBJECT_TYPE);
  __ j(below, miss_label);

  // Load properties array.
  Register properties = scratch0;
  __ mov(properties, FieldOperand(receiver, JSObject::kPropertiesOffset));

  // Check that the properties array is a dictionary.
  __ cmp(FieldOperand(properties, HeapObject::kMapOffset),
         Immediate(masm->isolate()->factory()->hash_table_map()));
  __ j(not_equal, miss_label);

  Label done;
  NameDictionaryLookupStub::GenerateNegativeLookup(masm,
                                                   miss_label,
                                                   &done,
                                                   properties,
                                                   name,
                                                   scratch1);
  __ bind(&done);
  __ DecrementCounter(counters->negative_lookups_miss(), 1);
}


void StubCache::GenerateProbe(MacroAssembler* masm,
                              Code::Flags flags,
                              Register receiver,
                              Register name,
                              Register scratch,
                              Register extra,
                              Register extra2,
                              Register extra3) {
  Label miss;

  // Assert that code is valid.  The multiplying code relies on the entry size
  // being 12.
  ASSERT(sizeof(Entry) == 12);

  // Assert the flags do not name a specific type.
  ASSERT(Code::ExtractTypeFromFlags(flags) == 0);

  // Assert that there are no register conflicts.
  ASSERT(!scratch.is(receiver));
  ASSERT(!scratch.is(name));
  ASSERT(!extra.is(receiver));
  ASSERT(!extra.is(name));
  ASSERT(!extra.is(scratch));

  // Assert scratch and extra registers are valid, and extra2/3 are unused.
  ASSERT(!scratch.is(no_reg));
  ASSERT(extra2.is(no_reg));
  ASSERT(extra3.is(no_reg));

  Register offset = scratch;
  scratch = no_reg;

  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->megamorphic_stub_cache_probes(), 1);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Get the map of the receiver and compute the hash.
  __ mov(offset, FieldOperand(name, Name::kHashFieldOffset));
  __ add(offset, FieldOperand(receiver, HeapObject::kMapOffset));
  __ xor_(offset, flags);
  // We mask out the last two bits because they are not part of the hash and
  // they are always 01 for maps.  Also in the two 'and' instructions below.
  __ and_(offset, (kPrimaryTableSize - 1) << kHeapObjectTagSize);
  // ProbeTable expects the offset to be pointer scaled, which it is, because
  // the heap object tag size is 2 and the pointer size log 2 is also 2.
  ASSERT(kHeapObjectTagSize == kPointerSizeLog2);

  // Probe the primary table.
  ProbeTable(isolate(), masm, flags, kPrimary, name, receiver, offset, extra);

  // Primary miss: Compute hash for secondary probe.
  __ mov(offset, FieldOperand(name, Name::kHashFieldOffset));
  __ add(offset, FieldOperand(receiver, HeapObject::kMapOffset));
  __ xor_(offset, flags);
  __ and_(offset, (kPrimaryTableSize - 1) << kHeapObjectTagSize);
  __ sub(offset, name);
  __ add(offset, Immediate(flags));
  __ and_(offset, (kSecondaryTableSize - 1) << kHeapObjectTagSize);

  // Probe the secondary table.
  ProbeTable(
      isolate(), masm, flags, kSecondary, name, receiver, offset, extra);

  // Cache miss: Fall-through and let caller handle the miss by
  // entering the runtime system.
  __ bind(&miss);
  __ IncrementCounter(counters->megamorphic_stub_cache_misses(), 1);
}


void StubCompiler::GenerateLoadGlobalFunctionPrototype(MacroAssembler* masm,
                                                       int index,
                                                       Register prototype) {
  __ LoadGlobalFunction(index, prototype);
  __ LoadGlobalFunctionInitialMap(prototype, prototype);
  // Load the prototype from the initial map.
  __ mov(prototype, FieldOperand(prototype, Map::kPrototypeOffset));
}


void StubCompiler::GenerateDirectLoadGlobalFunctionPrototype(
    MacroAssembler* masm,
    int index,
    Register prototype,
    Label* miss) {
  // Check we're still in the same context.
  __ cmp(Operand(esi, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)),
         masm->isolate()->global_object());
  __ j(not_equal, miss);
  // Get the global function with the given index.
  Handle<JSFunction> function(
      JSFunction::cast(masm->isolate()->native_context()->get(index)));
  // Load its initial map. The global functions all have initial maps.
  __ Set(prototype, Immediate(Handle<Map>(function->initial_map())));
  // Load the prototype from the initial map.
  __ mov(prototype, FieldOperand(prototype, Map::kPrototypeOffset));
}


void StubCompiler::GenerateLoadArrayLength(MacroAssembler* masm,
                                           Register receiver,
                                           Register scratch,
                                           Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, miss_label);

  // Check that the object is a JS array.
  __ CmpObjectType(receiver, JS_ARRAY_TYPE, scratch);
  __ j(not_equal, miss_label);

  // Load length directly from the JS array.
  __ mov(eax, FieldOperand(receiver, JSArray::kLengthOffset));
  __ ret(0);
}


// Generate code to check if an object is a string.  If the object is
// a string, the map's instance type is left in the scratch register.
static void GenerateStringCheck(MacroAssembler* masm,
                                Register receiver,
                                Register scratch,
                                Label* smi,
                                Label* non_string_object) {
  // Check that the object isn't a smi.
  __ JumpIfSmi(receiver, smi);

  // Check that the object is a string.
  __ mov(scratch, FieldOperand(receiver, HeapObject::kMapOffset));
  __ movzx_b(scratch, FieldOperand(scratch, Map::kInstanceTypeOffset));
  STATIC_ASSERT(kNotStringTag != 0);
  __ test(scratch, Immediate(kNotStringTag));
  __ j(not_zero, non_string_object);
}


void StubCompiler::GenerateLoadStringLength(MacroAssembler* masm,
                                            Register receiver,
                                            Register scratch1,
                                            Register scratch2,
                                            Label* miss) {
  Label check_wrapper;

  // Check if the object is a string leaving the instance type in the
  // scratch register.
  GenerateStringCheck(masm, receiver, scratch1, miss, &check_wrapper);

  // Load length from the string and convert to a smi.
  __ mov(eax, FieldOperand(receiver, String::kLengthOffset));
  __ ret(0);

  // Check if the object is a JSValue wrapper.
  __ bind(&check_wrapper);
  __ cmp(scratch1, JS_VALUE_TYPE);
  __ j(not_equal, miss);

  // Check if the wrapped value is a string and load the length
  // directly if it is.
  __ mov(scratch2, FieldOperand(receiver, JSValue::kValueOffset));
  GenerateStringCheck(masm, scratch2, scratch1, miss, miss);
  __ mov(eax, FieldOperand(scratch2, String::kLengthOffset));
  __ ret(0);
}


void StubCompiler::GenerateLoadFunctionPrototype(MacroAssembler* masm,
                                                 Register receiver,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Label* miss_label) {
  __ TryGetFunctionPrototype(receiver, scratch1, scratch2, miss_label);
  __ mov(eax, scratch1);
  __ ret(0);
}


void StubCompiler::GenerateFastPropertyLoad(MacroAssembler* masm,
                                            Register dst,
                                            Register src,
                                            bool inobject,
                                            int index,
                                            Representation representation) {
  ASSERT(!FLAG_track_double_fields || !representation.IsDouble());
  int offset = index * kPointerSize;
  if (!inobject) {
    // Calculate the offset into the properties array.
    offset = offset + FixedArray::kHeaderSize;
    __ mov(dst, FieldOperand(src, JSObject::kPropertiesOffset));
    src = dst;
  }
  __ mov(dst, FieldOperand(src, offset));
}


static void PushInterceptorArguments(MacroAssembler* masm,
                                     Register receiver,
                                     Register holder,
                                     Register name,
                                     Handle<JSObject> holder_obj) {
  STATIC_ASSERT(StubCache::kInterceptorArgsNameIndex == 0);
  STATIC_ASSERT(StubCache::kInterceptorArgsInfoIndex == 1);
  STATIC_ASSERT(StubCache::kInterceptorArgsThisIndex == 2);
  STATIC_ASSERT(StubCache::kInterceptorArgsHolderIndex == 3);
  STATIC_ASSERT(StubCache::kInterceptorArgsLength == 4);
  __ push(name);
  Handle<InterceptorInfo> interceptor(holder_obj->GetNamedInterceptor());
  ASSERT(!masm->isolate()->heap()->InNewSpace(*interceptor));
  Register scratch = name;
  __ mov(scratch, Immediate(interceptor));
  __ push(scratch);
  __ push(receiver);
  __ push(holder);
}


static void CompileCallLoadPropertyWithInterceptor(
    MacroAssembler* masm,
    Register receiver,
    Register holder,
    Register name,
    Handle<JSObject> holder_obj,
    IC::UtilityId id) {
  PushInterceptorArguments(masm, receiver, holder, name, holder_obj);
  __ CallExternalReference(
      ExternalReference(IC_Utility(id), masm->isolate()),
      StubCache::kInterceptorArgsLength);
}


// Number of pointers to be reserved on stack for fast API call.
static const int kFastApiCallArguments = FunctionCallbackArguments::kArgsLength;


// Reserves space for the extra arguments to API function in the
// caller's frame.
//
// These arguments are set by CheckPrototypes and GenerateFastApiCall.
static void ReserveSpaceForFastApiCall(MacroAssembler* masm, Register scratch) {
  // ----------- S t a t e -------------
  //  -- esp[0] : return address
  //  -- esp[4] : last argument in the internal frame of the caller
  // -----------------------------------
  __ pop(scratch);
  for (int i = 0; i < kFastApiCallArguments; i++) {
    __ push(Immediate(Smi::FromInt(0)));
  }
  __ push(scratch);
}


// Undoes the effects of ReserveSpaceForFastApiCall.
static void FreeSpaceForFastApiCall(MacroAssembler* masm, Register scratch) {
  // ----------- S t a t e -------------
  //  -- esp[0]  : return address.
  //  -- esp[4]  : last fast api call extra argument.
  //  -- ...
  //  -- esp[kFastApiCallArguments * 4] : first fast api call extra argument.
  //  -- esp[kFastApiCallArguments * 4 + 4] : last argument in the internal
  //                                          frame.
  // -----------------------------------
  __ pop(scratch);
  __ add(esp, Immediate(kPointerSize * kFastApiCallArguments));
  __ push(scratch);
}


static void GenerateFastApiCallBody(MacroAssembler* masm,
                                    const CallOptimization& optimization,
                                    int argc,
                                    bool restore_context);


// Generates call to API function.
static void GenerateFastApiCall(MacroAssembler* masm,
                                const CallOptimization& optimization,
                                int argc) {
  typedef FunctionCallbackArguments FCA;
  // Save calling context.
  __ mov(Operand(esp, (1 + FCA::kContextSaveIndex) * kPointerSize), esi);

  // Get the function and setup the context.
  Handle<JSFunction> function = optimization.constant_function();
  __ LoadHeapObject(edi, function);
  __ mov(esi, FieldOperand(edi, JSFunction::kContextOffset));

  // Construct the FunctionCallbackInfo.
  __ mov(Operand(esp, (1 + FCA::kCalleeIndex) * kPointerSize), edi);
  Handle<CallHandlerInfo> api_call_info = optimization.api_call_info();
  Handle<Object> call_data(api_call_info->data(), masm->isolate());
  if (masm->isolate()->heap()->InNewSpace(*call_data)) {
    __ mov(ecx, api_call_info);
    __ mov(ebx, FieldOperand(ecx, CallHandlerInfo::kDataOffset));
    __ mov(Operand(esp, (1 + FCA::kDataIndex) * kPointerSize), ebx);
  } else {
    __ mov(Operand(esp, (1 + FCA::kDataIndex) * kPointerSize),
           Immediate(call_data));
  }
  __ mov(Operand(esp, (1 + FCA::kIsolateIndex) * kPointerSize),
         Immediate(reinterpret_cast<int>(masm->isolate())));
  __ mov(Operand(esp, (1 + FCA::kReturnValueOffset) * kPointerSize),
         masm->isolate()->factory()->undefined_value());
  __ mov(Operand(esp, (1 + FCA::kReturnValueDefaultValueIndex) * kPointerSize),
         masm->isolate()->factory()->undefined_value());

  // Prepare arguments.
  STATIC_ASSERT(kFastApiCallArguments == 7);
  __ lea(eax, Operand(esp, 1 * kPointerSize));

  GenerateFastApiCallBody(masm, optimization, argc, false);
}


// Generate call to api function.
// This function uses push() to generate smaller, faster code than
// the version above. It is an optimization that should will be removed
// when api call ICs are generated in hydrogen.
static void GenerateFastApiCall(MacroAssembler* masm,
                                const CallOptimization& optimization,
                                Register receiver,
                                Register scratch1,
                                Register scratch2,
                                Register scratch3,
                                int argc,
                                Register* values) {
  ASSERT(optimization.is_simple_api_call());

  // Copy return value.
  __ pop(scratch1);

  // receiver
  __ push(receiver);

  // Write the arguments to stack frame.
  for (int i = 0; i < argc; i++) {
    Register arg = values[argc-1-i];
    ASSERT(!receiver.is(arg));
    ASSERT(!scratch1.is(arg));
    ASSERT(!scratch2.is(arg));
    ASSERT(!scratch3.is(arg));
    __ push(arg);
  }

  typedef FunctionCallbackArguments FCA;

  STATIC_ASSERT(FCA::kHolderIndex == 0);
  STATIC_ASSERT(FCA::kIsolateIndex == 1);
  STATIC_ASSERT(FCA::kReturnValueDefaultValueIndex == 2);
  STATIC_ASSERT(FCA::kReturnValueOffset == 3);
  STATIC_ASSERT(FCA::kDataIndex == 4);
  STATIC_ASSERT(FCA::kCalleeIndex == 5);
  STATIC_ASSERT(FCA::kContextSaveIndex == 6);
  STATIC_ASSERT(FCA::kArgsLength == 7);

  // context save
  __ push(esi);

  // Get the function and setup the context.
  Handle<JSFunction> function = optimization.constant_function();
  __ LoadHeapObject(scratch2, function);
  __ mov(esi, FieldOperand(scratch2, JSFunction::kContextOffset));
  // callee
  __ push(scratch2);

  Isolate* isolate = masm->isolate();
  Handle<CallHandlerInfo> api_call_info = optimization.api_call_info();
  Handle<Object> call_data(api_call_info->data(), isolate);
  // Push data from ExecutableAccessorInfo.
  if (isolate->heap()->InNewSpace(*call_data)) {
    __ mov(scratch2, api_call_info);
    __ mov(scratch3, FieldOperand(scratch2, CallHandlerInfo::kDataOffset));
    __ push(scratch3);
  } else {
    __ push(Immediate(call_data));
  }
  // return value
  __ push(Immediate(isolate->factory()->undefined_value()));
  // return value default
  __ push(Immediate(isolate->factory()->undefined_value()));
  // isolate
  __ push(Immediate(reinterpret_cast<int>(isolate)));
  // holder
  __ push(receiver);

  // store receiver address for GenerateFastApiCallBody
  ASSERT(!scratch1.is(eax));
  __ mov(eax, esp);

  // return address
  __ push(scratch1);

  GenerateFastApiCallBody(masm, optimization, argc, true);
}


static void GenerateFastApiCallBody(MacroAssembler* masm,
                                    const CallOptimization& optimization,
                                    int argc,
                                    bool restore_context) {
  // ----------- S t a t e -------------
  //  -- esp[0]              : return address
  //  -- esp[4] - esp[28]    : FunctionCallbackInfo, incl.
  //                         :  object passing the type check
  //                            (set by CheckPrototypes)
  //  -- esp[32]             : last argument
  //  -- ...
  //  -- esp[(argc + 7) * 4] : first argument
  //  -- esp[(argc + 8) * 4] : receiver
  //
  //  -- eax : receiver address
  // -----------------------------------
  typedef FunctionCallbackArguments FCA;

  // API function gets reference to the v8::Arguments. If CPU profiler
  // is enabled wrapper function will be called and we need to pass
  // address of the callback as additional parameter, always allocate
  // space for it.
  const int kApiArgc = 1 + 1;

  // Allocate the v8::Arguments structure in the arguments' space since
  // it's not controlled by GC.
  const int kApiStackSpace = 4;

  Handle<CallHandlerInfo> api_call_info = optimization.api_call_info();

  // Function address is a foreign pointer outside V8's heap.
  Address function_address = v8::ToCData<Address>(api_call_info->callback());
  __ PrepareCallApiFunction(kApiArgc + kApiStackSpace);

  // FunctionCallbackInfo::implicit_args_.
  __ mov(ApiParameterOperand(2), eax);
  __ add(eax, Immediate((argc + kFastApiCallArguments - 1) * kPointerSize));
  // FunctionCallbackInfo::values_.
  __ mov(ApiParameterOperand(3), eax);
  // FunctionCallbackInfo::length_.
  __ Set(ApiParameterOperand(4), Immediate(argc));
  // FunctionCallbackInfo::is_construct_call_.
  __ Set(ApiParameterOperand(5), Immediate(0));

  // v8::InvocationCallback's argument.
  __ lea(eax, ApiParameterOperand(2));
  __ mov(ApiParameterOperand(0), eax);

  Address thunk_address = FUNCTION_ADDR(&InvokeFunctionCallback);

  Operand context_restore_operand(ebp,
                                  (2 + FCA::kContextSaveIndex) * kPointerSize);
  Operand return_value_operand(ebp,
                               (2 + FCA::kReturnValueOffset) * kPointerSize);
  __ CallApiFunctionAndReturn(function_address,
                              thunk_address,
                              ApiParameterOperand(1),
                              argc + kFastApiCallArguments + 1,
                              return_value_operand,
                              restore_context ?
                                  &context_restore_operand : NULL);
}


class CallInterceptorCompiler BASE_EMBEDDED {
 public:
  CallInterceptorCompiler(CallStubCompiler* stub_compiler,
                          const ParameterCount& arguments,
                          Register name)
      : stub_compiler_(stub_compiler),
        arguments_(arguments),
        name_(name) {}

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

    Counters* counters = masm->isolate()->counters();
    __ IncrementCounter(counters->call_const_interceptor(), 1);

    if (can_do_fast_api_call) {
      __ IncrementCounter(counters->call_const_interceptor_fast_api(), 1);
      ReserveSpaceForFastApiCall(masm, scratch1);
    }

    // Check that the maps from receiver to interceptor's holder
    // haven't changed and thus we can invoke interceptor.
    Label miss_cleanup;
    Label* miss = can_do_fast_api_call ? &miss_cleanup : miss_label;
    Register holder =
        stub_compiler_->CheckPrototypes(
            IC::CurrentTypeOf(object, masm->isolate()), receiver,
            interceptor_holder, scratch1, scratch2, scratch3,
            name, depth1, miss);

    // Invoke an interceptor and if it provides a value,
    // branch to |regular_invoke|.
    Label regular_invoke;
    LoadWithInterceptor(masm, receiver, holder, interceptor_holder,
                        &regular_invoke);

    // Interceptor returned nothing for this property.  Try to use cached
    // constant function.

    // Check that the maps from interceptor's holder to constant function's
    // holder haven't changed and thus we can use cached constant function.
    if (*interceptor_holder != lookup->holder()) {
      stub_compiler_->CheckPrototypes(
          IC::CurrentTypeOf(interceptor_holder, masm->isolate()), holder,
          handle(lookup->holder()), scratch1, scratch2, scratch3,
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
      GenerateFastApiCall(masm, optimization, arguments_.immediate());
    } else {
      Handle<JSFunction> fun = optimization.constant_function();
      stub_compiler_->GenerateJumpFunction(object, fun);
    }

    // Deferred code for fast API call case---clean preallocated space.
    if (can_do_fast_api_call) {
      __ bind(&miss_cleanup);
      FreeSpaceForFastApiCall(masm, scratch1);
      __ jmp(miss_label);
    }

    // Invoke a regular function.
    __ bind(&regular_invoke);
    if (can_do_fast_api_call) {
      FreeSpaceForFastApiCall(masm, scratch1);
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
        stub_compiler_->CheckPrototypes(
            IC::CurrentTypeOf(object, masm->isolate()), receiver,
            interceptor_holder, scratch1, scratch2, scratch3, name, miss_label);

    FrameScope scope(masm, StackFrame::INTERNAL);
    // Save the name_ register across the call.
    __ push(name_);

    CompileCallLoadPropertyWithInterceptor(
        masm, receiver, holder, name_, interceptor_holder,
        IC::kLoadPropertyWithInterceptorForCall);

    // Restore the name_ register.
    __ pop(name_);

    // Leave the internal frame.
  }

  void LoadWithInterceptor(MacroAssembler* masm,
                           Register receiver,
                           Register holder,
                           Handle<JSObject> holder_obj,
                           Label* interceptor_succeeded) {
    {
      FrameScope scope(masm, StackFrame::INTERNAL);
      __ push(receiver);
      __ push(holder);
      __ push(name_);

      CompileCallLoadPropertyWithInterceptor(
          masm, receiver, holder, name_, holder_obj,
          IC::kLoadPropertyWithInterceptorOnly);

      __ pop(name_);
      __ pop(holder);
      __ pop(receiver);
      // Leave the internal frame.
    }

    __ cmp(eax, masm->isolate()->factory()->no_interceptor_result_sentinel());
    __ j(not_equal, interceptor_succeeded);
  }

  CallStubCompiler* stub_compiler_;
  const ParameterCount& arguments_;
  Register name_;
};


void StoreStubCompiler::GenerateRestoreName(MacroAssembler* masm,
                                            Label* label,
                                            Handle<Name> name) {
  if (!label->is_unused()) {
    __ bind(label);
    __ mov(this->name(), Immediate(name));
  }
}


// Generate code to check that a global property cell is empty. Create
// the property cell at compilation time if no cell exists for the
// property.
void StubCompiler::GenerateCheckPropertyCell(MacroAssembler* masm,
                                             Handle<JSGlobalObject> global,
                                             Handle<Name> name,
                                             Register scratch,
                                             Label* miss) {
  Handle<PropertyCell> cell =
      JSGlobalObject::EnsurePropertyCell(global, name);
  ASSERT(cell->value()->IsTheHole());
  Handle<Oddball> the_hole = masm->isolate()->factory()->the_hole_value();
  if (Serializer::enabled()) {
    __ mov(scratch, Immediate(cell));
    __ cmp(FieldOperand(scratch, PropertyCell::kValueOffset),
           Immediate(the_hole));
  } else {
    __ cmp(Operand::ForCell(cell), Immediate(the_hole));
  }
  __ j(not_equal, miss);
}


void StoreStubCompiler::GenerateNegativeHolderLookup(
    MacroAssembler* masm,
    Handle<JSObject> holder,
    Register holder_reg,
    Handle<Name> name,
    Label* miss) {
  if (holder->IsJSGlobalObject()) {
    GenerateCheckPropertyCell(
        masm, Handle<JSGlobalObject>::cast(holder), name, scratch1(), miss);
  } else if (!holder->HasFastProperties() && !holder->IsJSGlobalProxy()) {
    GenerateDictionaryNegativeLookup(
        masm, miss, holder_reg, name, scratch1(), scratch2());
  }
}


// Receiver_reg is preserved on jumps to miss_label, but may be destroyed if
// store is successful.
void StoreStubCompiler::GenerateStoreTransition(MacroAssembler* masm,
                                                Handle<JSObject> object,
                                                LookupResult* lookup,
                                                Handle<Map> transition,
                                                Handle<Name> name,
                                                Register receiver_reg,
                                                Register storage_reg,
                                                Register value_reg,
                                                Register scratch1,
                                                Register scratch2,
                                                Register unused,
                                                Label* miss_label,
                                                Label* slow) {
  int descriptor = transition->LastAdded();
  DescriptorArray* descriptors = transition->instance_descriptors();
  PropertyDetails details = descriptors->GetDetails(descriptor);
  Representation representation = details.representation();
  ASSERT(!representation.IsNone());

  if (details.type() == CONSTANT) {
    Handle<Object> constant(descriptors->GetValue(descriptor), masm->isolate());
    __ CmpObject(value_reg, constant);
    __ j(not_equal, miss_label);
  } else if (FLAG_track_fields && representation.IsSmi()) {
      __ JumpIfNotSmi(value_reg, miss_label);
  } else if (FLAG_track_heap_object_fields && representation.IsHeapObject()) {
    __ JumpIfSmi(value_reg, miss_label);
  } else if (FLAG_track_double_fields && representation.IsDouble()) {
    Label do_store, heap_number;
    __ AllocateHeapNumber(storage_reg, scratch1, scratch2, slow);

    __ JumpIfNotSmi(value_reg, &heap_number);
    __ SmiUntag(value_reg);
    if (CpuFeatures::IsSupported(SSE2)) {
      CpuFeatureScope use_sse2(masm, SSE2);
      __ Cvtsi2sd(xmm0, value_reg);
    } else {
      __ push(value_reg);
      __ fild_s(Operand(esp, 0));
      __ pop(value_reg);
    }
    __ SmiTag(value_reg);
    __ jmp(&do_store);

    __ bind(&heap_number);
    __ CheckMap(value_reg, masm->isolate()->factory()->heap_number_map(),
                miss_label, DONT_DO_SMI_CHECK);
    if (CpuFeatures::IsSupported(SSE2)) {
      CpuFeatureScope use_sse2(masm, SSE2);
      __ movsd(xmm0, FieldOperand(value_reg, HeapNumber::kValueOffset));
    } else {
      __ fld_d(FieldOperand(value_reg, HeapNumber::kValueOffset));
    }

    __ bind(&do_store);
    if (CpuFeatures::IsSupported(SSE2)) {
      CpuFeatureScope use_sse2(masm, SSE2);
      __ movsd(FieldOperand(storage_reg, HeapNumber::kValueOffset), xmm0);
    } else {
      __ fstp_d(FieldOperand(storage_reg, HeapNumber::kValueOffset));
    }
  }

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalProxy() || !object->IsAccessCheckNeeded());

  // Perform map transition for the receiver if necessary.
  if (details.type() == FIELD &&
      object->map()->unused_property_fields() == 0) {
    // The properties must be extended before we can store the value.
    // We jump to a runtime call that extends the properties array.
    __ pop(scratch1);  // Return address.
    __ push(receiver_reg);
    __ push(Immediate(transition));
    __ push(value_reg);
    __ push(scratch1);
    __ TailCallExternalReference(
        ExternalReference(IC_Utility(IC::kSharedStoreIC_ExtendStorage),
                          masm->isolate()),
        3,
        1);
    return;
  }

  // Update the map of the object.
  __ mov(scratch1, Immediate(transition));
  __ mov(FieldOperand(receiver_reg, HeapObject::kMapOffset), scratch1);

  // Update the write barrier for the map field.
  __ RecordWriteField(receiver_reg,
                      HeapObject::kMapOffset,
                      scratch1,
                      scratch2,
                      kDontSaveFPRegs,
                      OMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);

  if (details.type() == CONSTANT) {
    ASSERT(value_reg.is(eax));
    __ ret(0);
    return;
  }

  int index = transition->instance_descriptors()->GetFieldIndex(
      transition->LastAdded());

  // Adjust for the number of properties stored in the object. Even in the
  // face of a transition we can use the old map here because the size of the
  // object and the number of in-object properties is not going to change.
  index -= object->map()->inobject_properties();

  SmiCheck smi_check = representation.IsTagged()
      ? INLINE_SMI_CHECK : OMIT_SMI_CHECK;
  // TODO(verwaest): Share this code as a code stub.
  if (index < 0) {
    // Set the property straight into the object.
    int offset = object->map()->instance_size() + (index * kPointerSize);
    if (FLAG_track_double_fields && representation.IsDouble()) {
      __ mov(FieldOperand(receiver_reg, offset), storage_reg);
    } else {
      __ mov(FieldOperand(receiver_reg, offset), value_reg);
    }

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Update the write barrier for the array address.
      if (!FLAG_track_double_fields || !representation.IsDouble()) {
        __ mov(storage_reg, value_reg);
      }
      __ RecordWriteField(receiver_reg,
                          offset,
                          storage_reg,
                          scratch1,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  } else {
    // Write to the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    // Get the properties array (optimistically).
    __ mov(scratch1, FieldOperand(receiver_reg, JSObject::kPropertiesOffset));
    if (FLAG_track_double_fields && representation.IsDouble()) {
      __ mov(FieldOperand(scratch1, offset), storage_reg);
    } else {
      __ mov(FieldOperand(scratch1, offset), value_reg);
    }

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Update the write barrier for the array address.
      if (!FLAG_track_double_fields || !representation.IsDouble()) {
        __ mov(storage_reg, value_reg);
      }
      __ RecordWriteField(scratch1,
                          offset,
                          storage_reg,
                          receiver_reg,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  }

  // Return the value (register eax).
  ASSERT(value_reg.is(eax));
  __ ret(0);
}


// Both name_reg and receiver_reg are preserved on jumps to miss_label,
// but may be destroyed if store is successful.
void StoreStubCompiler::GenerateStoreField(MacroAssembler* masm,
                                           Handle<JSObject> object,
                                           LookupResult* lookup,
                                           Register receiver_reg,
                                           Register name_reg,
                                           Register value_reg,
                                           Register scratch1,
                                           Register scratch2,
                                           Label* miss_label) {
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
      int offset = object->map()->instance_size() + (index * kPointerSize);
      __ mov(scratch1, FieldOperand(receiver_reg, offset));
    } else {
      __ mov(scratch1, FieldOperand(receiver_reg, JSObject::kPropertiesOffset));
      int offset = index * kPointerSize + FixedArray::kHeaderSize;
      __ mov(scratch1, FieldOperand(scratch1, offset));
    }

    // Store the value into the storage.
    Label do_store, heap_number;
    __ JumpIfNotSmi(value_reg, &heap_number);
    __ SmiUntag(value_reg);
    if (CpuFeatures::IsSupported(SSE2)) {
      CpuFeatureScope use_sse2(masm, SSE2);
      __ Cvtsi2sd(xmm0, value_reg);
    } else {
      __ push(value_reg);
      __ fild_s(Operand(esp, 0));
      __ pop(value_reg);
    }
    __ SmiTag(value_reg);
    __ jmp(&do_store);
    __ bind(&heap_number);
    __ CheckMap(value_reg, masm->isolate()->factory()->heap_number_map(),
                miss_label, DONT_DO_SMI_CHECK);
    if (CpuFeatures::IsSupported(SSE2)) {
      CpuFeatureScope use_sse2(masm, SSE2);
      __ movsd(xmm0, FieldOperand(value_reg, HeapNumber::kValueOffset));
    } else {
      __ fld_d(FieldOperand(value_reg, HeapNumber::kValueOffset));
    }
    __ bind(&do_store);
    if (CpuFeatures::IsSupported(SSE2)) {
      CpuFeatureScope use_sse2(masm, SSE2);
      __ movsd(FieldOperand(scratch1, HeapNumber::kValueOffset), xmm0);
    } else {
      __ fstp_d(FieldOperand(scratch1, HeapNumber::kValueOffset));
    }
    // Return the value (register eax).
    ASSERT(value_reg.is(eax));
    __ ret(0);
    return;
  }

  ASSERT(!FLAG_track_double_fields || !representation.IsDouble());
  // TODO(verwaest): Share this code as a code stub.
  SmiCheck smi_check = representation.IsTagged()
      ? INLINE_SMI_CHECK : OMIT_SMI_CHECK;
  if (index < 0) {
    // Set the property straight into the object.
    int offset = object->map()->instance_size() + (index * kPointerSize);
    __ mov(FieldOperand(receiver_reg, offset), value_reg);

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Update the write barrier for the array address.
      // Pass the value being stored in the now unused name_reg.
      __ mov(name_reg, value_reg);
      __ RecordWriteField(receiver_reg,
                          offset,
                          name_reg,
                          scratch1,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  } else {
    // Write to the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    // Get the properties array (optimistically).
    __ mov(scratch1, FieldOperand(receiver_reg, JSObject::kPropertiesOffset));
    __ mov(FieldOperand(scratch1, offset), value_reg);

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Update the write barrier for the array address.
      // Pass the value being stored in the now unused name_reg.
      __ mov(name_reg, value_reg);
      __ RecordWriteField(scratch1,
                          offset,
                          name_reg,
                          receiver_reg,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  }

  // Return the value (register eax).
  ASSERT(value_reg.is(eax));
  __ ret(0);
}


void StubCompiler::GenerateTailCall(MacroAssembler* masm, Handle<Code> code) {
  __ jmp(code, RelocInfo::CODE_TARGET);
}


#undef __
#define __ ACCESS_MASM(masm())


Register StubCompiler::CheckPrototypes(Handle<HeapType> type,
                                       Register object_reg,
                                       Handle<JSObject> holder,
                                       Register holder_reg,
                                       Register scratch1,
                                       Register scratch2,
                                       Handle<Name> name,
                                       int save_at_depth,
                                       Label* miss,
                                       PrototypeCheckType check) {
  Handle<Map> receiver_map(IC::TypeToMap(*type, isolate()));
  // Make sure that the type feedback oracle harvests the receiver map.
  // TODO(svenpanne) Remove this hack when all ICs are reworked.
  __ mov(scratch1, receiver_map);

  // Make sure there's no overlap between holder and object registers.
  ASSERT(!scratch1.is(object_reg) && !scratch1.is(holder_reg));
  ASSERT(!scratch2.is(object_reg) && !scratch2.is(holder_reg)
         && !scratch2.is(scratch1));

  // Keep track of the current object in register reg.
  Register reg = object_reg;
  int depth = 0;

  const int kHolderIndex = FunctionCallbackArguments::kHolderIndex + 1;
  if (save_at_depth == depth) {
    __ mov(Operand(esp, kHolderIndex * kPointerSize), reg);
  }

  Handle<JSObject> current = Handle<JSObject>::null();
  if (type->IsConstant()) current = Handle<JSObject>::cast(type->AsConstant());
  Handle<JSObject> prototype = Handle<JSObject>::null();
  Handle<Map> current_map = receiver_map;
  Handle<Map> holder_map(holder->map());
  // Traverse the prototype chain and check the maps in the prototype chain for
  // fast and global objects or do negative lookup for normal objects.
  while (!current_map.is_identical_to(holder_map)) {
    ++depth;

    // Only global objects and objects that do not require access
    // checks are allowed in stubs.
    ASSERT(current_map->IsJSGlobalProxyMap() ||
           !current_map->is_access_check_needed());

    prototype = handle(JSObject::cast(current_map->prototype()));
    if (current_map->is_dictionary_map() &&
        !current_map->IsJSGlobalObjectMap() &&
        !current_map->IsJSGlobalProxyMap()) {
      if (!name->IsUniqueName()) {
        ASSERT(name->IsString());
        name = factory()->InternalizeString(Handle<String>::cast(name));
      }
      ASSERT(current.is_null() ||
             current->property_dictionary()->FindEntry(*name) ==
             NameDictionary::kNotFound);

      GenerateDictionaryNegativeLookup(masm(), miss, reg, name,
                                       scratch1, scratch2);

      __ mov(scratch1, FieldOperand(reg, HeapObject::kMapOffset));
      reg = holder_reg;  // From now on the object will be in holder_reg.
      __ mov(reg, FieldOperand(scratch1, Map::kPrototypeOffset));
    } else {
      bool in_new_space = heap()->InNewSpace(*prototype);
      if (depth != 1 || check == CHECK_ALL_MAPS) {
        __ CheckMap(reg, current_map, miss, DONT_DO_SMI_CHECK);
      }

      // Check access rights to the global object.  This has to happen after
      // the map check so that we know that the object is actually a global
      // object.
      if (current_map->IsJSGlobalProxyMap()) {
        __ CheckAccessGlobalProxy(reg, scratch1, scratch2, miss);
      } else if (current_map->IsJSGlobalObjectMap()) {
        GenerateCheckPropertyCell(
            masm(), Handle<JSGlobalObject>::cast(current), name,
            scratch2, miss);
      }

      if (in_new_space) {
        // Save the map in scratch1 for later.
        __ mov(scratch1, FieldOperand(reg, HeapObject::kMapOffset));
      }

      reg = holder_reg;  // From now on the object will be in holder_reg.

      if (in_new_space) {
        // The prototype is in new space; we cannot store a reference to it
        // in the code.  Load it from the map.
        __ mov(reg, FieldOperand(scratch1, Map::kPrototypeOffset));
      } else {
        // The prototype is in old space; load it directly.
        __ mov(reg, prototype);
      }
    }

    if (save_at_depth == depth) {
      __ mov(Operand(esp, kHolderIndex * kPointerSize), reg);
    }

    // Go to the next object in the prototype chain.
    current = prototype;
    current_map = handle(current->map());
  }

  // Log the check depth.
  LOG(isolate(), IntEvent("check-maps-depth", depth + 1));

  if (depth != 0 || check == CHECK_ALL_MAPS) {
    // Check the holder map.
    __ CheckMap(reg, current_map, miss, DONT_DO_SMI_CHECK);
  }

  // Perform security check for access to the global object.
  ASSERT(current_map->IsJSGlobalProxyMap() ||
         !current_map->is_access_check_needed());
  if (current_map->IsJSGlobalProxyMap()) {
    __ CheckAccessGlobalProxy(reg, scratch1, scratch2, miss);
  }

  // Return the register containing the holder.
  return reg;
}


void LoadStubCompiler::HandlerFrontendFooter(Handle<Name> name, Label* miss) {
  if (!miss->is_unused()) {
    Label success;
    __ jmp(&success);
    __ bind(miss);
    TailCallBuiltin(masm(), MissBuiltin(kind()));
    __ bind(&success);
  }
}


void StoreStubCompiler::HandlerFrontendFooter(Handle<Name> name, Label* miss) {
  if (!miss->is_unused()) {
    Label success;
    __ jmp(&success);
    GenerateRestoreName(masm(), miss, name);
    TailCallBuiltin(masm(), MissBuiltin(kind()));
    __ bind(&success);
  }
}


Register LoadStubCompiler::CallbackHandlerFrontend(
    Handle<HeapType> type,
    Register object_reg,
    Handle<JSObject> holder,
    Handle<Name> name,
    Handle<Object> callback) {
  Label miss;

  Register reg = HandlerFrontendHeader(type, object_reg, holder, name, &miss);

  if (!holder->HasFastProperties() && !holder->IsJSGlobalObject()) {
    ASSERT(!reg.is(scratch2()));
    ASSERT(!reg.is(scratch3()));
    Register dictionary = scratch1();
    bool must_preserve_dictionary_reg = reg.is(dictionary);

    // Load the properties dictionary.
    if (must_preserve_dictionary_reg) {
      __ push(dictionary);
    }
    __ mov(dictionary, FieldOperand(reg, JSObject::kPropertiesOffset));

    // Probe the dictionary.
    Label probe_done, pop_and_miss;
    NameDictionaryLookupStub::GeneratePositiveLookup(masm(),
                                                     &pop_and_miss,
                                                     &probe_done,
                                                     dictionary,
                                                     this->name(),
                                                     scratch2(),
                                                     scratch3());
    __ bind(&pop_and_miss);
    if (must_preserve_dictionary_reg) {
      __ pop(dictionary);
    }
    __ jmp(&miss);
    __ bind(&probe_done);

    // If probing finds an entry in the dictionary, scratch2 contains the
    // index into the dictionary. Check that the value is the callback.
    Register index = scratch2();
    const int kElementsStartOffset =
        NameDictionary::kHeaderSize +
        NameDictionary::kElementsStartIndex * kPointerSize;
    const int kValueOffset = kElementsStartOffset + kPointerSize;
    __ mov(scratch3(),
           Operand(dictionary, index, times_4, kValueOffset - kHeapObjectTag));
    if (must_preserve_dictionary_reg) {
      __ pop(dictionary);
    }
    __ cmp(scratch3(), callback);
    __ j(not_equal, &miss);
  }

  HandlerFrontendFooter(name, &miss);
  return reg;
}


void LoadStubCompiler::GenerateLoadField(Register reg,
                                         Handle<JSObject> holder,
                                         PropertyIndex field,
                                         Representation representation) {
  if (!reg.is(receiver())) __ mov(receiver(), reg);
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


void LoadStubCompiler::GenerateLoadCallback(
    const CallOptimization& call_optimization) {
  GenerateFastApiCall(
      masm(), call_optimization, receiver(), scratch1(),
      scratch2(), name(), 0, NULL);
}


void LoadStubCompiler::GenerateLoadCallback(
    Register reg,
    Handle<ExecutableAccessorInfo> callback) {
  // Insert additional parameters into the stack frame above return address.
  ASSERT(!scratch3().is(reg));
  __ pop(scratch3());  // Get return address to place it below.

  STATIC_ASSERT(PropertyCallbackArguments::kHolderIndex == 0);
  STATIC_ASSERT(PropertyCallbackArguments::kIsolateIndex == 1);
  STATIC_ASSERT(PropertyCallbackArguments::kReturnValueDefaultValueIndex == 2);
  STATIC_ASSERT(PropertyCallbackArguments::kReturnValueOffset == 3);
  STATIC_ASSERT(PropertyCallbackArguments::kDataIndex == 4);
  STATIC_ASSERT(PropertyCallbackArguments::kThisIndex == 5);
  __ push(receiver());  // receiver
  // Push data from ExecutableAccessorInfo.
  if (isolate()->heap()->InNewSpace(callback->data())) {
    ASSERT(!scratch2().is(reg));
    __ mov(scratch2(), Immediate(callback));
    __ push(FieldOperand(scratch2(), ExecutableAccessorInfo::kDataOffset));
  } else {
    __ push(Immediate(Handle<Object>(callback->data(), isolate())));
  }
  __ push(Immediate(isolate()->factory()->undefined_value()));  // ReturnValue
  // ReturnValue default value
  __ push(Immediate(isolate()->factory()->undefined_value()));
  __ push(Immediate(reinterpret_cast<int>(isolate())));
  __ push(reg);  // holder

  // Save a pointer to where we pushed the arguments. This will be
  // passed as the const PropertyAccessorInfo& to the C++ callback.
  __ push(esp);

  __ push(name());  // name
  __ mov(ebx, esp);  // esp points to reference to name (handler).

  __ push(scratch3());  // Restore return address.

  // array for v8::Arguments::values_, handler for name and pointer
  // to the values (it considered as smi in GC).
  const int kStackSpace = PropertyCallbackArguments::kArgsLength + 2;
  // Allocate space for opional callback address parameter in case
  // CPU profiler is active.
  const int kApiArgc = 2 + 1;

  Address getter_address = v8::ToCData<Address>(callback->getter());
  __ PrepareCallApiFunction(kApiArgc);
  __ mov(ApiParameterOperand(0), ebx);  // name.
  __ add(ebx, Immediate(kPointerSize));
  __ mov(ApiParameterOperand(1), ebx);  // arguments pointer.

  // Emitting a stub call may try to allocate (if the code is not
  // already generated).  Do not allow the assembler to perform a
  // garbage collection but instead return the allocation failure
  // object.

  Address thunk_address = FUNCTION_ADDR(&InvokeAccessorGetterCallback);

  __ CallApiFunctionAndReturn(getter_address,
                              thunk_address,
                              ApiParameterOperand(2),
                              kStackSpace,
                              Operand(ebp, 7 * kPointerSize),
                              NULL);
}


void LoadStubCompiler::GenerateLoadConstant(Handle<Object> value) {
  // Return the constant value.
  __ LoadObject(eax, value);
  __ ret(0);
}


void LoadStubCompiler::GenerateLoadInterceptor(
    Register holder_reg,
    Handle<Object> object,
    Handle<JSObject> interceptor_holder,
    LookupResult* lookup,
    Handle<Name> name) {
  ASSERT(interceptor_holder->HasNamedInterceptor());
  ASSERT(!interceptor_holder->GetNamedInterceptor()->getter()->IsUndefined());

  // So far the most popular follow ups for interceptor loads are FIELD
  // and CALLBACKS, so inline only them, other cases may be added
  // later.
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
    bool must_preserve_receiver_reg = !receiver().is(holder_reg) &&
        (lookup->type() == CALLBACKS || must_perfrom_prototype_check);

    // Save necessary data before invoking an interceptor.
    // Requires a frame to make GC aware of pushed pointers.
    {
      FrameScope frame_scope(masm(), StackFrame::INTERNAL);

      if (must_preserve_receiver_reg) {
        __ push(receiver());
      }
      __ push(holder_reg);
      __ push(this->name());

      // Invoke an interceptor.  Note: map checks from receiver to
      // interceptor's holder has been compiled before (see a caller
      // of this method.)
      CompileCallLoadPropertyWithInterceptor(
          masm(), receiver(), holder_reg, this->name(), interceptor_holder,
          IC::kLoadPropertyWithInterceptorOnly);

      // Check if interceptor provided a value for property.  If it's
      // the case, return immediately.
      Label interceptor_failed;
      __ cmp(eax, factory()->no_interceptor_result_sentinel());
      __ j(equal, &interceptor_failed);
      frame_scope.GenerateLeaveFrame();
      __ ret(0);

      // Clobber registers when generating debug-code to provoke errors.
      __ bind(&interceptor_failed);
      if (FLAG_debug_code) {
        __ mov(receiver(), Immediate(BitCast<int32_t>(kZapValue)));
        __ mov(holder_reg, Immediate(BitCast<int32_t>(kZapValue)));
        __ mov(this->name(), Immediate(BitCast<int32_t>(kZapValue)));
      }

      __ pop(this->name());
      __ pop(holder_reg);
      if (must_preserve_receiver_reg) {
        __ pop(receiver());
      }

      // Leave the internal frame.
    }

    GenerateLoadPostInterceptor(holder_reg, interceptor_holder, name, lookup);
  } else {  // !compile_followup_inline
    // Call the runtime system to load the interceptor.
    // Check that the maps haven't changed.
    __ pop(scratch2());  // save old return address
    PushInterceptorArguments(masm(), receiver(), holder_reg,
                             this->name(), interceptor_holder);
    __ push(scratch2());  // restore old return address

    ExternalReference ref =
        ExternalReference(IC_Utility(IC::kLoadPropertyWithInterceptorForLoad),
                          isolate());
    __ TailCallExternalReference(ref, StubCache::kInterceptorArgsLength, 1);
  }
}


void CallStubCompiler::GenerateNameCheck(Handle<Name> name, Label* miss) {
  if (kind_ == Code::KEYED_CALL_IC) {
    __ cmp(ecx, Immediate(name));
    __ j(not_equal, miss);
  }
}


void CallStubCompiler::GenerateFunctionCheck(Register function,
                                             Register scratch,
                                             Label* miss) {
  __ JumpIfSmi(function, miss);
  __ CmpObjectType(function, JS_FUNCTION_TYPE, scratch);
  __ j(not_equal, miss);
}


void CallStubCompiler::GenerateLoadFunctionFromCell(
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Label* miss) {
  // Get the value from the cell.
  if (Serializer::enabled()) {
    __ mov(edi, Immediate(cell));
    __ mov(edi, FieldOperand(edi, Cell::kValueOffset));
  } else {
    __ mov(edi, Operand::ForCell(cell));
  }

  // Check that the cell contains the same function.
  if (isolate()->heap()->InNewSpace(*function)) {
    // We can't embed a pointer to a function in new space so we have
    // to verify that the shared function info is unchanged. This has
    // the nice side effect that multiple closures based on the same
    // function can all use this call IC. Before we load through the
    // function, we have to verify that it still is a function.
    GenerateFunctionCheck(edi, ebx, miss);

    // Check the shared function info. Make sure it hasn't changed.
    __ cmp(FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset),
           Immediate(Handle<SharedFunctionInfo>(function->shared())));
  } else {
    __ cmp(edi, Immediate(function));
  }
  __ j(not_equal, miss);
}


void CallStubCompiler::GenerateMissBranch() {
  Handle<Code> code =
      isolate()->stub_cache()->ComputeCallMiss(arguments().immediate(),
                                               kind_,
                                               extra_state());
  __ jmp(code, RelocInfo::CODE_TARGET);
}


Handle<Code> CallStubCompiler::CompileCallField(Handle<JSObject> object,
                                                Handle<JSObject> holder,
                                                PropertyIndex index,
                                                Handle<Name> name) {
  Label miss;

  Register reg = HandlerFrontendHeader(
      object, holder, name, RECEIVER_MAP_CHECK, &miss);

  GenerateFastPropertyLoad(
      masm(), edi, reg, index.is_inobject(holder),
      index.translate(holder), Representation::Tagged());
  GenerateJumpFunction(object, edi, &miss);

  HandlerFrontendFooter(&miss);

  // Return the generated code.
  return GetCode(Code::FAST, name);
}


Handle<Code> CallStubCompiler::CompileFastApiCall(
    const CallOptimization& optimization,
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
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

  // Get the receiver from the stack.
  const int argc = arguments().immediate();
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(edx, &miss_before_stack_reserved);

  Counters* counters = isolate()->counters();
  __ IncrementCounter(counters->call_const(), 1);
  __ IncrementCounter(counters->call_const_fast_api(), 1);

  // Allocate space for v8::Arguments implicit values. Must be initialized
  // before calling any runtime function.
  __ sub(esp, Immediate(kFastApiCallArguments * kPointerSize));

  // Check that the maps haven't changed and find a Holder as a side effect.
  CheckPrototypes(IC::CurrentTypeOf(object, isolate()), edx, holder,
                  ebx, eax, edi, name, depth, &miss);

  // Move the return address on top of the stack.
  __ mov(eax, Operand(esp, kFastApiCallArguments * kPointerSize));
  __ mov(Operand(esp, 0 * kPointerSize), eax);

  // esp[2 * kPointerSize] is uninitialized, esp[3 * kPointerSize] contains
  // duplicate of return address and will be overwritten.
  GenerateFastApiCall(masm(), optimization, argc);

  __ bind(&miss);
  __ add(esp, Immediate(kFastApiCallArguments * kPointerSize));

  HandlerFrontendFooter(&miss_before_stack_reserved);

  // Return the generated code.
  return GetCode(function);
}


void StubCompiler::GenerateBooleanCheck(Register object, Label* miss) {
  Label success;
  // Check that the object is a boolean.
  __ cmp(object, factory()->true_value());
  __ j(equal, &success);
  __ cmp(object, factory()->false_value());
  __ j(not_equal, miss);
  __ bind(&success);
}


void CallStubCompiler::PatchImplicitReceiver(Handle<Object> object) {
  if (object->IsGlobalObject()) {
    const int argc = arguments().immediate();
    const int receiver_offset = (argc + 1) * kPointerSize;
    __ mov(Operand(esp, receiver_offset),
           isolate()->factory()->undefined_value());
  }
}


Register CallStubCompiler::HandlerFrontendHeader(Handle<Object> object,
                                                 Handle<JSObject> holder,
                                                 Handle<Name> name,
                                                 CheckType check,
                                                 Label* miss) {
  GenerateNameCheck(name, miss);

  Register reg = edx;

  const int argc = arguments().immediate();
  const int receiver_offset = (argc + 1) * kPointerSize;
  __ mov(reg, Operand(esp, receiver_offset));

  // Check that the receiver isn't a smi.
  if (check != NUMBER_CHECK) {
    __ JumpIfSmi(reg, miss);
  }

  // Make sure that it's okay not to patch the on stack receiver
  // unless we're doing a receiver map check.
  ASSERT(!object->IsGlobalObject() || check == RECEIVER_MAP_CHECK);
  switch (check) {
    case RECEIVER_MAP_CHECK:
      __ IncrementCounter(isolate()->counters()->call_const(), 1);

      // Check that the maps haven't changed.
      reg = CheckPrototypes(IC::CurrentTypeOf(object, isolate()), reg, holder,
                            ebx, eax, edi, name, miss);

      break;

    case STRING_CHECK: {
      // Check that the object is a string.
      __ CmpObjectType(reg, FIRST_NONSTRING_TYPE, eax);
      __ j(above_equal, miss);
      // Check that the maps starting from the prototype haven't changed.
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::STRING_FUNCTION_INDEX, eax, miss);
      break;
    }
    case SYMBOL_CHECK: {
      // Check that the object is a symbol.
      __ CmpObjectType(reg, SYMBOL_TYPE, eax);
      __ j(not_equal, miss);
      // Check that the maps starting from the prototype haven't changed.
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::SYMBOL_FUNCTION_INDEX, eax, miss);
      break;
    }
    case NUMBER_CHECK: {
      Label fast;
      // Check that the object is a smi or a heap number.
      __ JumpIfSmi(reg, &fast);
      __ CmpObjectType(reg, HEAP_NUMBER_TYPE, eax);
      __ j(not_equal, miss);
      __ bind(&fast);
      // Check that the maps starting from the prototype haven't changed.
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::NUMBER_FUNCTION_INDEX, eax, miss);
      break;
    }
    case BOOLEAN_CHECK: {
      GenerateBooleanCheck(reg, miss);
      // Check that the maps starting from the prototype haven't changed.
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::BOOLEAN_FUNCTION_INDEX, eax, miss);
      break;
    }
  }

  if (check != RECEIVER_MAP_CHECK) {
    Handle<Object> prototype(object->GetPrototype(isolate()), isolate());
    reg = CheckPrototypes(
        IC::CurrentTypeOf(prototype, isolate()),
        eax, holder, ebx, edx, edi, name, miss);
  }

  return reg;
}


void CallStubCompiler::GenerateJumpFunction(Handle<Object> object,
                                            Register function,
                                            Label* miss) {
  // Check that the function really is a function.
  GenerateFunctionCheck(function, ebx, miss);

  if (!function.is(edi)) __ mov(edi, function);
  PatchImplicitReceiver(object);

  // Invoke the function.
  __ InvokeFunction(edi, arguments(), JUMP_FUNCTION, NullCallWrapper());
}


Handle<Code> CallStubCompiler::CompileCallInterceptor(Handle<JSObject> object,
                                                      Handle<JSObject> holder,
                                                      Handle<Name> name) {
  Label miss;

  GenerateNameCheck(name, &miss);

  // Get the number of arguments.
  const int argc = arguments().immediate();

  LookupResult lookup(isolate());
  LookupPostInterceptor(holder, name, &lookup);

  // Get the receiver from the stack.
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));

  CallInterceptorCompiler compiler(this, arguments(), ecx);
  compiler.Compile(masm(), object, holder, name, &lookup, edx, ebx, edi, eax,
                   &miss);

  // Restore receiver.
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));

  GenerateJumpFunction(object, eax, &miss);

  HandlerFrontendFooter(&miss);

  // Return the generated code.
  return GetCode(Code::FAST, name);
}


Handle<Code> CallStubCompiler::CompileCallGlobal(
    Handle<JSObject> object,
    Handle<GlobalObject> holder,
    Handle<PropertyCell> cell,
    Handle<JSFunction> function,
    Handle<Name> name) {
  if (HasCustomCallGenerator(function)) {
    Handle<Code> code = CompileCustomCall(
        object, holder, cell, function, Handle<String>::cast(name),
        Code::NORMAL);
    // A null handle means bail out to the regular compiler code below.
    if (!code.is_null()) return code;
  }

  Label miss;
  HandlerFrontendHeader(object, holder, name, RECEIVER_MAP_CHECK, &miss);
  // Potentially loads a closure that matches the shared function info of the
  // function, rather than function.
  GenerateLoadFunctionFromCell(cell, function, &miss);
  GenerateJumpFunction(object, edi, function);

  HandlerFrontendFooter(&miss);

  // Return the generated code.
  return GetCode(Code::NORMAL, name);
}


Handle<Code> StoreStubCompiler::CompileStoreCallback(
    Handle<JSObject> object,
    Handle<JSObject> holder,
    Handle<Name> name,
    Handle<ExecutableAccessorInfo> callback) {
  Register holder_reg = HandlerFrontend(
      IC::CurrentTypeOf(object, isolate()), receiver(), holder, name);

  __ pop(scratch1());  // remove the return address
  __ push(receiver());
  __ push(holder_reg);
  __ Push(callback);
  __ Push(name);
  __ push(value());
  __ push(scratch1());  // restore return address

  // Do tail-call to the runtime system.
  ExternalReference store_callback_property =
      ExternalReference(IC_Utility(IC::kStoreCallbackProperty), isolate());
  __ TailCallExternalReference(store_callback_property, 5, 1);

  // Return the generated code.
  return GetCode(kind(), Code::FAST, name);
}


Handle<Code> StoreStubCompiler::CompileStoreCallback(
    Handle<JSObject> object,
    Handle<JSObject> holder,
    Handle<Name> name,
    const CallOptimization& call_optimization) {
  HandlerFrontend(IC::CurrentTypeOf(object, isolate()),
                  receiver(), holder, name);

  Register values[] = { value() };
  GenerateFastApiCall(
      masm(), call_optimization, receiver(), scratch1(),
      scratch2(), this->name(), 1, values);

  // Return the generated code.
  return GetCode(kind(), Code::FAST, name);
}


#undef __
#define __ ACCESS_MASM(masm)


void StoreStubCompiler::GenerateStoreViaSetter(
    MacroAssembler* masm,
    Handle<JSFunction> setter) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : name
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Save value register, so we can restore it later.
    __ push(eax);

    if (!setter.is_null()) {
      // Call the JavaScript setter with receiver and value on the stack.
      __ push(edx);
      __ push(eax);
      ParameterCount actual(1);
      ParameterCount expected(setter);
      __ InvokeFunction(setter, expected, actual,
                        CALL_FUNCTION, NullCallWrapper());
    } else {
      // If we generate a global code snippet for deoptimization only, remember
      // the place to continue after deoptimization.
      masm->isolate()->heap()->SetSetterStubDeoptPCOffset(masm->pc_offset());
    }

    // We have to return the passed value, not the return value of the setter.
    __ pop(eax);

    // Restore context register.
    __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));
  }
  __ ret(0);
}


#undef __
#define __ ACCESS_MASM(masm())


Handle<Code> StoreStubCompiler::CompileStoreInterceptor(
    Handle<JSObject> object,
    Handle<Name> name) {
  __ pop(scratch1());  // remove the return address
  __ push(receiver());
  __ push(this->name());
  __ push(value());
  __ push(scratch1());  // restore return address

  // Do tail-call to the runtime system.
  ExternalReference store_ic_property =
      ExternalReference(IC_Utility(IC::kStoreInterceptorProperty), isolate());
  __ TailCallExternalReference(store_ic_property, 3, 1);

  // Return the generated code.
  return GetCode(kind(), Code::FAST, name);
}


Handle<Code> KeyedStoreStubCompiler::CompileStorePolymorphic(
    MapHandleList* receiver_maps,
    CodeHandleList* handler_stubs,
    MapHandleList* transitioned_maps) {
  Label miss;
  __ JumpIfSmi(receiver(), &miss, Label::kNear);
  __ mov(scratch1(), FieldOperand(receiver(), HeapObject::kMapOffset));
  for (int i = 0; i < receiver_maps->length(); ++i) {
    __ cmp(scratch1(), receiver_maps->at(i));
    if (transitioned_maps->at(i).is_null()) {
      __ j(equal, handler_stubs->at(i));
    } else {
      Label next_map;
      __ j(not_equal, &next_map, Label::kNear);
      __ mov(transition_map(), Immediate(transitioned_maps->at(i)));
      __ jmp(handler_stubs->at(i), RelocInfo::CODE_TARGET);
      __ bind(&next_map);
    }
  }
  __ bind(&miss);
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  return GetICCode(
      kind(), Code::NORMAL, factory()->empty_string(), POLYMORPHIC);
}


Handle<Code> LoadStubCompiler::CompileLoadNonexistent(Handle<HeapType> type,
                                                      Handle<JSObject> last,
                                                      Handle<Name> name) {
  NonexistentHandlerFrontend(type, last, name);

  // Return undefined if maps of the full prototype chain are still the
  // same and no global property with this name contains a value.
  __ mov(eax, isolate()->factory()->undefined_value());
  __ ret(0);

  // Return the generated code.
  return GetCode(kind(), Code::FAST, name);
}


Register* LoadStubCompiler::registers() {
  // receiver, name, scratch1, scratch2, scratch3, scratch4.
  static Register registers[] = { edx, ecx, ebx, eax, edi, no_reg };
  return registers;
}


Register* KeyedLoadStubCompiler::registers() {
  // receiver, name, scratch1, scratch2, scratch3, scratch4.
  static Register registers[] = { edx, ecx, ebx, eax, edi, no_reg };
  return registers;
}


Register* StoreStubCompiler::registers() {
  // receiver, name, value, scratch1, scratch2, scratch3.
  static Register registers[] = { edx, ecx, eax, ebx, edi, no_reg };
  return registers;
}


Register* KeyedStoreStubCompiler::registers() {
  // receiver, name, value, scratch1, scratch2, scratch3.
  static Register registers[] = { edx, ecx, eax, ebx, edi, no_reg };
  return registers;
}


#undef __
#define __ ACCESS_MASM(masm)


void LoadStubCompiler::GenerateLoadViaGetter(MacroAssembler* masm,
                                             Register receiver,
                                             Handle<JSFunction> getter) {
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    if (!getter.is_null()) {
      // Call the JavaScript getter with the receiver on the stack.
      __ push(receiver);
      ParameterCount actual(0);
      ParameterCount expected(getter);
      __ InvokeFunction(getter, expected, actual,
                        CALL_FUNCTION, NullCallWrapper());
    } else {
      // If we generate a global code snippet for deoptimization only, remember
      // the place to continue after deoptimization.
      masm->isolate()->heap()->SetGetterStubDeoptPCOffset(masm->pc_offset());
    }

    // Restore context register.
    __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));
  }
  __ ret(0);
}


#undef __
#define __ ACCESS_MASM(masm())


Handle<Code> LoadStubCompiler::CompileLoadGlobal(
    Handle<HeapType> type,
    Handle<GlobalObject> global,
    Handle<PropertyCell> cell,
    Handle<Name> name,
    bool is_dont_delete) {
  Label miss;

  HandlerFrontendHeader(type, receiver(), global, name, &miss);
  // Get the value from the cell.
  if (Serializer::enabled()) {
    __ mov(eax, Immediate(cell));
    __ mov(eax, FieldOperand(eax, PropertyCell::kValueOffset));
  } else {
    __ mov(eax, Operand::ForCell(cell));
  }

  // Check for deleted property if property can actually be deleted.
  if (!is_dont_delete) {
    __ cmp(eax, factory()->the_hole_value());
    __ j(equal, &miss);
  } else if (FLAG_debug_code) {
    __ cmp(eax, factory()->the_hole_value());
    __ Check(not_equal, kDontDeleteCellsCannotContainTheHole);
  }

  HandlerFrontendFooter(name, &miss);

  Counters* counters = isolate()->counters();
  __ IncrementCounter(counters->named_load_global_stub(), 1);
  // The code above already loads the result into the return register.
  __ ret(0);

  // Return the generated code.
  return GetCode(kind(), Code::NORMAL, name);
}


Handle<Code> BaseLoadStoreStubCompiler::CompilePolymorphicIC(
    TypeHandleList* types,
    CodeHandleList* handlers,
    Handle<Name> name,
    Code::StubType type,
    IcCheckType check) {
  Label miss;

  if (check == PROPERTY &&
      (kind() == Code::KEYED_LOAD_IC || kind() == Code::KEYED_STORE_IC)) {
    __ cmp(this->name(), Immediate(name));
    __ j(not_equal, &miss);
  }

  Label number_case;
  Label* smi_target = IncludesNumberType(types) ? &number_case : &miss;
  __ JumpIfSmi(receiver(), smi_target);

  Register map_reg = scratch1();
  __ mov(map_reg, FieldOperand(receiver(), HeapObject::kMapOffset));
  int receiver_count = types->length();
  int number_of_handled_maps = 0;
  for (int current = 0; current < receiver_count; ++current) {
    Handle<HeapType> type = types->at(current);
    Handle<Map> map = IC::TypeToMap(*type, isolate());
    if (!map->is_deprecated()) {
      number_of_handled_maps++;
      __ cmp(map_reg, map);
      if (type->Is(HeapType::Number())) {
        ASSERT(!number_case.is_unused());
        __ bind(&number_case);
      }
      __ j(equal, handlers->at(current));
    }
  }
  ASSERT(number_of_handled_maps != 0);

  __ bind(&miss);
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  InlineCacheState state =
      number_of_handled_maps > 1 ? POLYMORPHIC : MONOMORPHIC;
  return GetICCode(kind(), type, name, state);
}


#undef __
#define __ ACCESS_MASM(masm)


void KeyedLoadStubCompiler::GenerateLoadDictionaryElement(
    MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- ecx    : key
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------
  Label slow, miss;

  // This stub is meant to be tail-jumped to, the receiver must already
  // have been verified by the caller to not be a smi.
  __ JumpIfNotSmi(ecx, &miss);
  __ mov(ebx, ecx);
  __ SmiUntag(ebx);
  __ mov(eax, FieldOperand(edx, JSObject::kElementsOffset));

  // Push receiver on the stack to free up a register for the dictionary
  // probing.
  __ push(edx);
  __ LoadFromNumberDictionary(&slow, eax, ecx, ebx, edx, edi, eax);
  // Pop receiver before returning.
  __ pop(edx);
  __ ret(0);

  __ bind(&slow);
  __ pop(edx);

  // ----------- S t a t e -------------
  //  -- ecx    : key
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------
  TailCallBuiltin(masm, Builtins::kKeyedLoadIC_Slow);

  __ bind(&miss);
  // ----------- S t a t e -------------
  //  -- ecx    : key
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------
  TailCallBuiltin(masm, Builtins::kKeyedLoadIC_Miss);
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_IA32
