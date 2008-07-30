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

#include "ic-inl.h"
#include "codegen-inl.h"
#include "stub-cache.h"

namespace v8 { namespace internal {

#define __ masm->


static void ProbeTable(MacroAssembler* masm,
                       Code::Flags flags,
                       StubCache::Table table,
                       Register name,
                       Register offset) {
  ExternalReference key_offset(SCTableReference::keyReference(table));
  ExternalReference value_offset(SCTableReference::valueReference(table));

  Label miss;

  // Save the offset on the stack.
  __ push(offset);

  // Check that the key in the entry matches the name.
  __ mov(ip, Operand(key_offset));
  __ ldr(ip, MemOperand(ip, offset, LSL, 1));
  __ cmp(name, Operand(ip));
  __ b(ne, &miss);

  // Get the code entry from the cache.
  __ mov(ip, Operand(value_offset));
  __ ldr(offset, MemOperand(ip, offset, LSL, 1));

  // Check that the flags match what we're looking for.
  __ ldr(offset, FieldMemOperand(offset, Code::kFlagsOffset));
  __ and_(offset, offset, Operand(~Code::kFlagsTypeMask));
  __ cmp(offset, Operand(flags));
  __ b(ne, &miss);

  // Restore offset and re-load code entry from cache.
  __ pop(offset);
  __ mov(ip, Operand(value_offset));
  __ ldr(offset, MemOperand(ip, offset, LSL, 1));

  // Jump to the first instruction in the code stub.
  __ add(offset, offset, Operand(Code::kHeaderSize - kHeapObjectTag));
  __ Jump(offset);

  // Miss: Restore offset and fall through.
  __ bind(&miss);
  __ pop(offset);
}


void StubCache::GenerateProbe(MacroAssembler* masm,
                              Code::Flags flags,
                              Register receiver,
                              Register name,
                              Register scratch) {
  Label miss;

  // Make sure that code is valid. The shifting code relies on the
  // entry size being 8.
  ASSERT(sizeof(Entry) == 8);

  // Make sure the flags does not name a specific type.
  ASSERT(Code::ExtractTypeFromFlags(flags) == 0);

  // Make sure that there are no register conflicts.
  ASSERT(!scratch.is(receiver));
  ASSERT(!scratch.is(name));

  // Check that the receiver isn't a smi.
  __ tst(receiver, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Get the map of the receiver and compute the hash.
  __ ldr(scratch, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ ldr(ip, FieldMemOperand(name, String::kLengthOffset));
  __ add(scratch, scratch, Operand(ip));
  __ eor(scratch, scratch, Operand(flags));
  __ and_(scratch,
          scratch,
          Operand((kPrimaryTableSize - 1) << kHeapObjectTagSize));

  // Probe the primary table.
  ProbeTable(masm, flags, kPrimary, name, scratch);

  // Primary miss: Compute hash for secondary probe.
  __ sub(scratch, scratch, Operand(name));
  __ add(scratch, scratch, Operand(flags));
  __ and_(scratch,
          scratch,
          Operand((kSecondaryTableSize - 1) << kHeapObjectTagSize));

  // Probe the secondary table.
  ProbeTable(masm, flags, kSecondary, name, scratch);

  // Cache miss: Fall-through and let caller handle the miss by
  // entering the runtime system.
  __ bind(&miss);
}


void StubCompiler::GenerateLoadGlobalFunctionPrototype(MacroAssembler* masm,
                                                       int index,
                                                       Register prototype) {
  // Load the global or builtins object from the current context.
  __ ldr(prototype, MemOperand(cp, Context::SlotOffset(Context::GLOBAL_INDEX)));
  // Load the global context from the global or builtins object.
  __ ldr(prototype,
         FieldMemOperand(prototype, GlobalObject::kGlobalContextOffset));
  // Load the function from the global context.
  __ ldr(prototype, MemOperand(prototype, Context::SlotOffset(index)));
  // Load the initial map.  The global functions all have initial maps.
  __ ldr(prototype,
         FieldMemOperand(prototype, JSFunction::kPrototypeOrInitialMapOffset));
  // Load the prototype from the initial map.
  __ ldr(prototype, FieldMemOperand(prototype, Map::kPrototypeOffset));
}


#undef __

#define __ masm()->


Object* StubCompiler::CompileLazyCompile(Code::Flags flags) {
  HandleScope scope;

  // Enter the JS frame but don't add additional arguments.
  __ EnterJSFrame(0);

  // Push the function on the stack and call the runtime function.
  __ Push(MemOperand(pp, 0));
  __ CallRuntime(Runtime::kLazyCompile, 1);

  // Move result to r1 and restore number of arguments.
  __ mov(r1, Operand(r0));
  __ ldr(r0, MemOperand(fp, JavaScriptFrameConstants::kArgsLengthOffset));

  __ ExitJSFrame(DO_NOT_RETURN);

  // Do a tail-call of the compiled function.
  __ add(r1, r1, Operand(Code::kHeaderSize - kHeapObjectTag));
  __ Jump(r1);

  return GetCodeWithFlags(flags);
}


Object* CallStubCompiler::CompileCallField(Object* object,
                                           JSObject* holder,
                                           int index) {
  // ----------- S t a t e -------------
  //  -- r0: number of arguments
  //  -- r1: receiver
  //  -- lr: return address
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Check that the receiver isn't a smi.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Do the right check and compute the holder register.
  Register reg =
      __ CheckMaps(JSObject::cast(object), r1, holder, r3, r2, &miss);

  // Get the properties array of the holder and get the function from the field.
  int offset = index * kPointerSize + Array::kHeaderSize;
  __ ldr(r3, FieldMemOperand(reg, JSObject::kPropertiesOffset));
  __ ldr(r3, FieldMemOperand(r3, offset));

  // Check that the function really is a function.
  __ tst(r3, Operand(kSmiTagMask));
  __ b(eq, &miss);
  // Get the map.
  __ ldr(r2, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
  __ cmp(r2, Operand(JS_FUNCTION_TYPE));
  __ b(ne, &miss);

  // Patch the function on the stack; 1 ~ receiver.
  __ add(ip, sp, Operand(r0, LSL, kPointerSizeLog2));
  __ str(r3, MemOperand(ip, 1 * kPointerSize));

  // Setup the context and jump to the call code of the function (tail call).
  __ ldr(cp, FieldMemOperand(r3, JSFunction::kContextOffset));
  __ ldr(r2, FieldMemOperand(r3, JSFunction::kSharedFunctionInfoOffset));
  __ ldr(r2, FieldMemOperand(r2, SharedFunctionInfo::kCodeOffset));
  __ add(r2, r2, Operand(Code::kHeaderSize - kHeapObjectTag));
  __ Jump(r2);

  // Handle call cache miss.
  __ bind(&miss);
  Handle<Code> ic = ComputeCallMiss(arguments().immediate());
  __ Jump(ic, code_target);

  // Return the generated code.
  return GetCode(FIELD);
}


Object* CallStubCompiler::CompileCallConstant(Object* object,
                                              JSObject* holder,
                                              JSFunction* function,
                                              CheckType check) {
  // ----------- S t a t e -------------
  //  -- r0: number of arguments
  //  -- r1: receiver
  //  -- lr: return address
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Check that the receiver isn't a smi.
  if (check != NUMBER_CHECK) {
    __ tst(r1, Operand(kSmiTagMask));
    __ b(eq, &miss);
  }

  switch (check) {
    case RECEIVER_MAP_CHECK:
      // Check that the maps haven't changed.
      __ CheckMaps(JSObject::cast(object), r1, holder, r3, r2, &miss);
      break;

    case STRING_CHECK:
      // Check that the object is a two-byte string or a symbol.
      __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
      __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
      __ cmp(r2, Operand(FIRST_NONSTRING_TYPE));
      __ b(hs, &miss);
      // Check that the maps starting from the prototype haven't changed.
      GenerateLoadGlobalFunctionPrototype(masm(),
                                          Context::STRING_FUNCTION_INDEX,
                                          r2);
      __ CheckMaps(JSObject::cast(object->GetPrototype()),
                   r2, holder, r3, r1, &miss);
      break;

    case NUMBER_CHECK: {
      Label fast;
      // Check that the object is a smi or a heap number.
      __ tst(r1, Operand(kSmiTagMask));
      __ b(eq, &fast);
      __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
      __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
      __ cmp(r2, Operand(HEAP_NUMBER_TYPE));
      __ b(ne, &miss);
      __ bind(&fast);
      // Check that the maps starting from the prototype haven't changed.
      GenerateLoadGlobalFunctionPrototype(masm(),
                                          Context::NUMBER_FUNCTION_INDEX,
                                          r2);
      __ CheckMaps(JSObject::cast(object->GetPrototype()),
                   r2, holder, r3, r1, &miss);
      break;
    }

    case BOOLEAN_CHECK: {
      Label fast;
      // Check that the object is a boolean.
      __ cmp(r1, Operand(Factory::true_value()));
      __ b(eq, &fast);
      __ cmp(r1, Operand(Factory::false_value()));
      __ b(ne, &miss);
      __ bind(&fast);
      // Check that the maps starting from the prototype haven't changed.
      GenerateLoadGlobalFunctionPrototype(masm(),
                                          Context::BOOLEAN_FUNCTION_INDEX,
                                          r2);
      __ CheckMaps(JSObject::cast(object->GetPrototype()),
                   r2, holder, r3, r1, &miss);
      break;
    }

    case JSARRAY_HAS_FAST_ELEMENTS_CHECK:
      __ CheckMaps(JSObject::cast(object), r1, holder, r3, r2, &miss);
      // Make sure object->elements()->map() != Heap::hash_table_map()
      // Get the elements array of the object.
      __ ldr(r3, FieldMemOperand(r1, JSObject::kElementsOffset));
      // Check that the object is in fast mode (not dictionary).
      __ ldr(r2, FieldMemOperand(r3, HeapObject::kMapOffset));
      __ cmp(r2, Operand(Factory::hash_table_map()));
      __ b(eq, &miss);
      break;

    default:
      UNREACHABLE();
  }

  // Get the function and setup the context.
  __ mov(r3, Operand(Handle<JSFunction>(function)));
  __ ldr(cp, FieldMemOperand(r3, JSFunction::kContextOffset));

  // Patch the function on the stack; 1 ~ receiver.
  __ add(ip, sp, Operand(r0, LSL, kPointerSizeLog2));
  __ str(r3, MemOperand(ip, 1 * kPointerSize));

  // Jump to the cached code (tail call).
  Handle<Code> code(function->code());
  __ Jump(code, code_target);

  // Handle call cache miss.
  __ bind(&miss);
  Handle<Code> ic = ComputeCallMiss(arguments().immediate());
  __ Jump(ic, code_target);

  // Return the generated code.
  return GetCode(CONSTANT_FUNCTION);
}


Object* CallStubCompiler::CompileCallInterceptor(Object* object,
                                                 JSObject* holder,
                                                 String* name) {
  // ----------- S t a t e -------------
  //  -- r0: number of arguments
  //  -- r1: receiver
  //  -- lr: return address
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // TODO(1224669): Implement.

  // Handle call cache miss.
  __ bind(&miss);
  Handle<Code> ic = ComputeCallMiss(arguments().immediate());
  __ Jump(ic, code_target);

  // Return the generated code.
  return GetCode(INTERCEPTOR);
}


Object* StoreStubCompiler::CompileStoreField(JSObject* object,
                                             int index,
                                             Map* transition,
                                             String* name) {
  // ----------- S t a t e -------------
  //  -- r0    : value
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss, exit;

  // Get the receiver from the stack.
  __ ldr(r3, MemOperand(sp, 0 * kPointerSize));

  // Check that the receiver isn't a smi.
  __ tst(r3, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the map of the receiver hasn't changed.
  __ ldr(r1, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ cmp(r1, Operand(Handle<Map>(object->map())));
  __ b(ne, &miss);

  // Perform global security token check if needed.
  if (object->IsJSGlobalObject()) {
    __ CheckAccessGlobal(r3, r1, &miss);
  }

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalObject() || !object->IsAccessCheckNeeded());

  // Get the properties array
  __ ldr(r1, FieldMemOperand(r3, JSObject::kPropertiesOffset));

  // Perform map transition for the receiver if necessary.
  if (transition != NULL) {
    // Update the map of the object; no write barrier updating is
    // needed because the map is never in new space.
    __ mov(ip, Operand(Handle<Map>(transition)));
    __ str(ip, FieldMemOperand(r3, HeapObject::kMapOffset));
  }

  // Write to the properties array.
  int offset = index * kPointerSize + Array::kHeaderSize;
  __ str(r0, FieldMemOperand(r1, offset));

  // Skip updating write barrier if storing a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &exit);

  // Update the write barrier for the array address.
  __ mov(r3, Operand(offset));
  __ RecordWrite(r1, r3, r2);  // OK to clobber r2, since we return

  // Return the value (register r0).
  __ bind(&exit);
  __ Ret();

  // Handle store cache miss.
  __ bind(&miss);
  __ mov(r2, Operand(Handle<String>(name)));  // restore name
  Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Miss));
  __ Jump(ic, code_target);

  // Return the generated code.
  return GetCode(transition == NULL ? FIELD : MAP_TRANSITION);
}


Object* StoreStubCompiler::CompileStoreCallback(JSObject* object,
                                                AccessorInfo* callback,
                                                String* name) {
  // ----------- S t a t e -------------
  //  -- r0    : value
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Get the object from the stack.
  __ ldr(r3, MemOperand(sp, 0 * kPointerSize));

  // Check that the object isn't a smi.
  __ tst(r3, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the map of the object hasn't changed.
  __ ldr(r1, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ cmp(r1, Operand(Handle<Map>(object->map())));
  __ b(ne, &miss);

  // Perform global security token check if needed.
  if (object->IsJSGlobalObject()) {
    __ CheckAccessGlobal(r3, r1, &miss);
  }

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalObject() || !object->IsAccessCheckNeeded());

  __ ldr(ip, MemOperand(sp));  // receiver
  __ push(ip);
  __ mov(ip, Operand(Handle<AccessorInfo>(callback)));  // callback info
  __ push(ip);
  __ push(r2);  // name
  __ push(r0);  // value

  // Do tail-call to the C builtin.
  __ mov(r0, Operand(3));  // not counting receiver
  __ JumpToBuiltin(ExternalReference(IC_Utility(IC::kStoreCallbackProperty)));

  // Handle store cache miss.
  __ bind(&miss);
  __ mov(r2, Operand(Handle<String>(name)));  // restore name
  Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Miss));
  __ Jump(ic, code_target);

  // Return the generated code.
  return GetCode(CALLBACKS);
}


Object* StoreStubCompiler::CompileStoreInterceptor(JSObject* receiver,
                                                   String* name) {
  // ----------- S t a t e -------------
  //  -- r0    : value
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Get the object from the stack.
  __ ldr(r3, MemOperand(sp, 0 * kPointerSize));

  // Check that the object isn't a smi.
  __ tst(r3, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the map of the object hasn't changed.
  __ ldr(r1, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ cmp(r1, Operand(Handle<Map>(receiver->map())));
  __ b(ne, &miss);

  // Perform global security token check if needed.
  if (receiver->IsJSGlobalObject()) {
    __ CheckAccessGlobal(r3, r1, &miss);
  }

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(receiver->IsJSGlobalObject() || !receiver->IsAccessCheckNeeded());

  __ ldr(ip, MemOperand(sp));  // receiver
  __ push(ip);
  __ push(r2);  // name
  __ push(r0);  // value

  // Do tail-call to the C builtin.
  __ mov(r0, Operand(2));  // not counting receiver
  ExternalReference store_interceptor =
      ExternalReference(IC_Utility(IC::kStoreInterceptorProperty));
  __ JumpToBuiltin(store_interceptor);

  // Handle store cache miss.
  __ bind(&miss);
  __ mov(r2, Operand(Handle<String>(name)));  // restore name
  Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Miss));
  __ Jump(ic, code_target);

  // Return the generated code.
  return GetCode(INTERCEPTOR);
}


Object* LoadStubCompiler::CompileLoadField(JSObject* object,
                                           JSObject* holder,
                                           int index) {
  // ----------- S t a t e -------------
  //  -- r0    : receiver
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Check that the receiver isn't a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the maps haven't changed.
  Register reg = __ CheckMaps(object, r0, holder, r3, r1, &miss);

  // Get the properties array of the holder.
  __ ldr(r3, FieldMemOperand(reg, JSObject::kPropertiesOffset));

  // Return the value from the properties array.
  int offset = index * kPointerSize + Array::kHeaderSize;
  __ ldr(r0, FieldMemOperand(r3, offset));
  __ Ret();

  // Handle load cache miss.
  __ bind(&miss);
  __ ldr(r0, MemOperand(sp));  // restore receiver
  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Miss));
  __ Jump(ic, code_target);

  // Return the generated code.
  return GetCode(FIELD);
}


Object* LoadStubCompiler::CompileLoadCallback(JSObject* object,
                                              JSObject* holder,
                                              AccessorInfo* callback) {
  // ----------- S t a t e -------------
  //  -- r0    : receiver
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Check that the receiver isn't a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the maps haven't changed.
  Register reg = __ CheckMaps(object, r0, holder, r3, r1, &miss);

  // Push the arguments on the JS stack of the caller.
  __ push(r0);  // receiver
  __ mov(ip, Operand(Handle<AccessorInfo>(callback)));  // callback data
  __ push(ip);
  __ push(r2);  // name
  __ push(reg);  // holder

  // Do tail-call to the C builtin.
  __ mov(r0, Operand(3));  // not counting receiver
  __ JumpToBuiltin(ExternalReference(IC_Utility(IC::kLoadCallbackProperty)));

  // Handle load cache miss.
  __ bind(&miss);
  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Miss));
  __ Jump(ic, code_target);

  // Return the generated code.
  return GetCode(CALLBACKS);
}


Object* LoadStubCompiler::CompileLoadConstant(JSObject* object,
                                              JSObject* holder,
                                              Object* value) {
  // ----------- S t a t e -------------
  //  -- r0    : receiver
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp] : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Check that the receiver isn't a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the maps haven't changed.
  Register reg = __ CheckMaps(object, r0, holder, r3, r1, &miss);

  // Return the constant value.
  __ mov(r0, Operand(Handle<Object>(value)));
  __ Ret();

  // Handle load cache miss.
  __ bind(&miss);
  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Miss));
  __ Jump(ic, code_target);

  // Return the generated code.
  return GetCode(CONSTANT_FUNCTION);
}


Object* LoadStubCompiler::CompileLoadInterceptor(JSObject* object,
                                                 JSObject* holder,
                                                 String* name) {
  // ----------- S t a t e -------------
  //  -- r0    : receiver
  //  -- r2    : name
  //  -- lr    : return address
  //  -- [sp]  : receiver
  // -----------------------------------

  HandleScope scope;
  Label miss;

  // Check that the receiver isn't a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the maps haven't changed.
  Register reg = __ CheckMaps(object, r0, holder, r3, r1, &miss);

  // Push the arguments on the JS stack of the caller.
  __ push(r0);  // receiver
  __ push(reg);  // holder
  __ push(r2);  // name

  // Do tail-call to the C builtin.
  __ mov(r0, Operand(2));  // not counting receiver
  __ JumpToBuiltin(ExternalReference(IC_Utility(IC::kLoadInterceptorProperty)));

  // Handle load cache miss.
  __ bind(&miss);
  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Miss));
  __ Jump(ic, code_target);

  // Return the generated code.
  return GetCode(INTERCEPTOR);
}


// TODO(1224671): IC stubs for keyed loads have not been implemented
// for ARM.
Object* KeyedLoadStubCompiler::CompileLoadField(String* name,
                                                JSObject* receiver,
                                                JSObject* holder,
                                                int index) {
  UNIMPLEMENTED();
  return Heap::undefined_value();
}


Object* KeyedLoadStubCompiler::CompileLoadCallback(String* name,
                                                   JSObject* receiver,
                                                   JSObject* holder,
                                                   AccessorInfo* callback) {
  UNIMPLEMENTED();
  return Heap::undefined_value();
}


Object* KeyedLoadStubCompiler::CompileLoadConstant(String* name,
                                                   JSObject* receiver,
                                                   JSObject* holder,
                                                   Object* value) {
  UNIMPLEMENTED();
  return Heap::undefined_value();
}


Object* KeyedLoadStubCompiler::CompileLoadInterceptor(JSObject* receiver,
                                                      JSObject* holder,
                                                      String* name) {
  UNIMPLEMENTED();
  return Heap::undefined_value();
}


Object* KeyedLoadStubCompiler::CompileLoadArrayLength(String* name) {
  UNIMPLEMENTED();
  return Heap::undefined_value();
}


Object* KeyedLoadStubCompiler::CompileLoadShortStringLength(String* name) {
  UNIMPLEMENTED();
  return Heap::undefined_value();
}


Object* KeyedLoadStubCompiler::CompileLoadMediumStringLength(String* name) {
  UNIMPLEMENTED();
  return Heap::undefined_value();
}


Object* KeyedLoadStubCompiler::CompileLoadLongStringLength(String* name) {
  UNIMPLEMENTED();
  return Heap::undefined_value();
}


Object* KeyedLoadStubCompiler::CompileLoadFunctionPrototype(String* name) {
  UNIMPLEMENTED();
  return Heap::undefined_value();
}


Object* KeyedStoreStubCompiler::CompileStoreField(JSObject* object,
                                                  int index,
                                                  Map* transition,
                                                  String* name) {
  UNIMPLEMENTED();
  return Heap::undefined_value();
}



#undef __

} }  // namespace v8::internal
