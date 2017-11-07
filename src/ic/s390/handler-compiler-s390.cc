// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_S390

#include "src/ic/handler-compiler.h"

#include "src/api-arguments.h"
#include "src/field-type.h"
#include "src/ic/call-optimization.h"
#include "src/ic/ic.h"
#include "src/isolate-inl.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void NamedStoreHandlerCompiler::GenerateStoreViaSetter(
    MacroAssembler* masm, Handle<Map> map, Register receiver, Register holder,
    int accessor_index, int expected_arguments, Register scratch) {
  // ----------- S t a t e -------------
  //  -- lr    : return address
  // -----------------------------------
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    DCHECK(holder != scratch);
    DCHECK(receiver != scratch);
    DCHECK(value() != scratch);
    // Call the JavaScript setter with receiver and value on the stack.
    if (map->IsJSGlobalObjectMap()) {
      // Swap in the global receiver.
      __ LoadP(scratch,
               FieldMemOperand(receiver, JSGlobalObject::kGlobalProxyOffset));
      receiver = scratch;
    }
    __ Push(receiver, value());
    __ LoadAccessor(r3, holder, accessor_index, ACCESSOR_SETTER);
    __ LoadImmP(r2, Operand(1));
    __ Call(masm->isolate()->builtins()->CallFunction(
                ConvertReceiverMode::kNotNullOrUndefined),
            RelocInfo::CODE_TARGET);
  }
  __ Ret();
}

void PropertyHandlerCompiler::PushVectorAndSlot(Register vector,
                                                Register slot) {
  MacroAssembler* masm = this->masm();
  STATIC_ASSERT(LoadWithVectorDescriptor::kSlot <
                LoadWithVectorDescriptor::kVector);
  STATIC_ASSERT(StoreWithVectorDescriptor::kSlot <
                StoreWithVectorDescriptor::kVector);
  STATIC_ASSERT(StoreTransitionDescriptor::kSlot <
                StoreTransitionDescriptor::kVector);
  __ Push(slot, vector);
}

void PropertyHandlerCompiler::PopVectorAndSlot(Register vector, Register slot) {
  MacroAssembler* masm = this->masm();
  __ Pop(slot, vector);
}

void PropertyHandlerCompiler::DiscardVectorAndSlot() {
  MacroAssembler* masm = this->masm();
  // Remove vector and slot.
  __ la(sp, MemOperand(sp, 2 * kPointerSize));
}

void PropertyHandlerCompiler::GenerateDictionaryNegativeLookup(
    MacroAssembler* masm, Label* miss_label, Register receiver,
    Handle<Name> name, Register scratch0, Register scratch1) {
  DCHECK(name->IsUniqueName());
  DCHECK(receiver != scratch0);
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->negative_lookups(), 1, scratch0, scratch1);
  __ IncrementCounter(counters->negative_lookups_miss(), 1, scratch0, scratch1);

  Label done;

  const int kInterceptorOrAccessCheckNeededMask =
      (1 << Map::kHasNamedInterceptor) | (1 << Map::kIsAccessCheckNeeded);

  // Bail out if the receiver has a named interceptor or requires access checks.
  Register map = scratch1;
  __ LoadP(map, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ LoadlB(scratch0, FieldMemOperand(map, Map::kBitFieldOffset));
  __ AndP(r0, scratch0, Operand(kInterceptorOrAccessCheckNeededMask));
  __ bne(miss_label);

  // Check that receiver is a JSObject.
  // TODO(joransiu): Merge into SI compare
  __ LoadlB(scratch0, FieldMemOperand(map, Map::kInstanceTypeOffset));
  __ CmpP(scratch0, Operand(FIRST_JS_RECEIVER_TYPE));
  __ blt(miss_label);

  // Load properties array.
  Register properties = scratch0;
  __ LoadP(properties,
           FieldMemOperand(receiver, JSObject::kPropertiesOrHashOffset));
  // Check that the properties array is a dictionary.
  __ LoadP(map, FieldMemOperand(properties, HeapObject::kMapOffset));
  __ CompareRoot(map, Heap::kNameDictionaryMapRootIndex);
  __ bne(miss_label);

  // Restore the temporarily used register.
  __ LoadP(properties,
           FieldMemOperand(receiver, JSObject::kPropertiesOrHashOffset));

  NameDictionaryLookupStub::GenerateNegativeLookup(
      masm, miss_label, &done, receiver, properties, name, scratch1);
  __ bind(&done);
  __ DecrementCounter(counters->negative_lookups_miss(), 1, scratch0, scratch1);
}

// Generate code to check that a global property cell is empty. Create
// the property cell at compilation time if no cell exists for the
// property.
void PropertyHandlerCompiler::GenerateCheckPropertyCell(
    MacroAssembler* masm, Handle<JSGlobalObject> global, Handle<Name> name,
    Register scratch, Label* miss) {
  Handle<PropertyCell> cell = JSGlobalObject::EnsureEmptyPropertyCell(
      global, name, PropertyCellType::kInvalidated);
  Isolate* isolate = masm->isolate();
  DCHECK(cell->value()->IsTheHole(isolate));
  Handle<WeakCell> weak_cell = isolate->factory()->NewWeakCell(cell);
  __ LoadWeakValue(scratch, weak_cell, miss);
  __ LoadP(scratch, FieldMemOperand(scratch, PropertyCell::kValueOffset));
  __ CompareRoot(scratch, Heap::kTheHoleValueRootIndex);
  __ bne(miss);
}

#undef __
#define __ ACCESS_MASM(masm())

void NamedStoreHandlerCompiler::GenerateRestoreName(Label* label,
                                                    Handle<Name> name) {
  if (!label->is_unused()) {
    __ bind(label);
    __ mov(this->name(), Operand(name));
  }
}

void PropertyHandlerCompiler::GenerateAccessCheck(
    Handle<WeakCell> native_context_cell, Register scratch1, Register scratch2,
    Label* miss, bool compare_native_contexts_only) {
  Label done;
  // Load current native context.
  __ LoadP(scratch1, NativeContextMemOperand());
  // Load expected native context.
  __ LoadWeakValue(scratch2, native_context_cell, miss);
  __ CmpP(scratch1, scratch2);

  if (!compare_native_contexts_only) {
    __ beq(&done);

    // Compare security tokens of current and expected native contexts.
    __ LoadP(scratch1,
             ContextMemOperand(scratch1, Context::SECURITY_TOKEN_INDEX));
    __ LoadP(scratch2,
             ContextMemOperand(scratch2, Context::SECURITY_TOKEN_INDEX));
    __ CmpP(scratch1, scratch2);
  }
  __ bne(miss);

  __ bind(&done);
}

Register PropertyHandlerCompiler::CheckPrototypes(
    Register object_reg, Register holder_reg, Register scratch1,
    Register scratch2, Handle<Name> name, Label* miss) {
  Handle<Map> receiver_map = map();

  // Make sure there's no overlap between holder and object registers.
  DCHECK(scratch1 != object_reg && scratch1 != holder_reg);
  DCHECK(scratch2 != object_reg && scratch2 != holder_reg &&
         scratch2 != scratch1);

  Handle<Cell> validity_cell =
      Map::GetOrCreatePrototypeChainValidityCell(receiver_map, isolate());
  if (!validity_cell.is_null()) {
    DCHECK_EQ(Smi::FromInt(Map::kPrototypeChainValid), validity_cell->value());
    __ mov(scratch1, Operand(validity_cell));
    __ LoadP(scratch1, FieldMemOperand(scratch1, Cell::kValueOffset));
    __ CmpSmiLiteral(scratch1, Smi::FromInt(Map::kPrototypeChainValid), r0);
    __ bne(miss);
  }

  // Keep track of the current object in register reg.
  Register reg = object_reg;
  int depth = 0;

  Handle<JSObject> current = Handle<JSObject>::null();
  if (receiver_map->IsJSGlobalObjectMap()) {
    current = isolate()->global_object();
  }

  Handle<Map> current_map(receiver_map->GetPrototypeChainRootMap(isolate()),
                          isolate());
  Handle<Map> holder_map(holder()->map());
  // Traverse the prototype chain and check the maps in the prototype chain for
  // fast and global objects or do negative lookup for normal objects.
  while (!current_map.is_identical_to(holder_map)) {
    ++depth;

    if (current_map->IsJSGlobalObjectMap()) {
      GenerateCheckPropertyCell(masm(), Handle<JSGlobalObject>::cast(current),
                                name, scratch2, miss);
    } else if (current_map->is_dictionary_map()) {
      DCHECK(!current_map->IsJSGlobalProxyMap());  // Proxy maps are fast.
      DCHECK(name->IsUniqueName());
      DCHECK(current.is_null() ||
             current->property_dictionary()->FindEntry(name) ==
                 NameDictionary::kNotFound);

      if (depth > 1) {
        Handle<WeakCell> weak_cell =
            Map::GetOrCreatePrototypeWeakCell(current, isolate());
        __ LoadWeakValue(reg, weak_cell, miss);
      }
      GenerateDictionaryNegativeLookup(masm(), miss, reg, name, scratch1,
                                       scratch2);
    }

    reg = holder_reg;  // From now on the object will be in holder_reg.
    // Go to the next object in the prototype chain.
    current = handle(JSObject::cast(current_map->prototype()));
    current_map = handle(current->map());
  }

  DCHECK(!current_map->IsJSGlobalProxyMap());

  // Log the check depth.
  LOG(isolate(), IntEvent("check-maps-depth", depth + 1));

  if (depth != 0) {
    Handle<WeakCell> weak_cell =
        Map::GetOrCreatePrototypeWeakCell(current, isolate());
    __ LoadWeakValue(reg, weak_cell, miss);
  }

  // Return the register containing the holder.
  return reg;
}

void NamedStoreHandlerCompiler::FrontendFooter(Handle<Name> name, Label* miss) {
  if (!miss->is_unused()) {
    Label success;
    __ b(&success);
    GenerateRestoreName(miss, name);
    PopVectorAndSlot();
    TailCallBuiltin(masm(), Builtins::kStoreIC_Miss);
    __ bind(&success);
  }
}

void NamedStoreHandlerCompiler::ZapStackArgumentsRegisterAliases() {
  STATIC_ASSERT(!StoreWithVectorDescriptor::kPassLastArgsOnStack);
}

Handle<Code> NamedStoreHandlerCompiler::CompileStoreCallback(
    Handle<JSObject> object, Handle<Name> name, Handle<AccessorInfo> callback,
    LanguageMode language_mode) {
  Register holder_reg = Frontend(name);

  __ Push(receiver(), holder_reg);  // receiver

  // If the callback cannot leak, then push the callback directly,
  // otherwise wrap it in a weak cell.
  if (callback->data()->IsUndefined(isolate()) || callback->data()->IsSmi()) {
    __ mov(ip, Operand(callback));
  } else {
    Handle<WeakCell> cell = isolate()->factory()->NewWeakCell(callback);
    __ mov(ip, Operand(cell));
  }
  __ Push(ip);
  __ mov(ip, Operand(name));
  __ Push(ip, value());
  __ Push(Smi::FromEnum(language_mode));

  // Do tail-call to the runtime system.
  __ TailCallRuntime(Runtime::kStoreCallbackProperty);

  // Return the generated code.
  return GetCode(name);
}

Register NamedStoreHandlerCompiler::value() {
  return StoreDescriptor::ValueRegister();
}

#undef __
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM
