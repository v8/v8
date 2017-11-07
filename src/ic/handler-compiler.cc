// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/handler-compiler.h"

#include "src/assembler-inl.h"
#include "src/field-type.h"
#include "src/ic/call-optimization.h"
#include "src/ic/handler-configuration-inl.h"
#include "src/ic/ic-inl.h"
#include "src/ic/ic.h"
#include "src/isolate-inl.h"

namespace v8 {
namespace internal {

Handle<Code> PropertyHandlerCompiler::GetCode(Handle<Name> name) {
  // Create code object in the heap.
  CodeDesc desc;
  masm()->GetCode(isolate(), &desc);
  Handle<Code> code = factory()->NewCode(
      desc, Code::STUB, masm()->CodeObject(), MaybeHandle<HandlerTable>(),
      MaybeHandle<ByteArray>(), MaybeHandle<DeoptimizationData>(),
      CodeStub::NoCacheKey());
  DCHECK(code->is_stub());
#ifdef ENABLE_DISASSEMBLER
  if (FLAG_print_code_stubs) {
    char* raw_name = !name.is_null() && name->IsString()
                         ? String::cast(*name)->ToCString().get()
                         : nullptr;
    CodeTracer::Scope trace_scope(isolate()->GetCodeTracer());
    OFStream os(trace_scope.file());
    code->Disassemble(raw_name, os);
  }
#endif

  PROFILE(isolate(), CodeCreateEvent(CodeEventListener::HANDLER_TAG,
                                     AbstractCode::cast(*code), *name));

#ifdef DEBUG
  code->VerifyEmbeddedObjects();
#endif
  return code;
}


#define __ ACCESS_MASM(masm())

// Frontend for store uses the name register. It has to be restored before a
// miss.
Register NamedStoreHandlerCompiler::FrontendHeader(Register object_reg,
                                                   Handle<Name> name,
                                                   Label* miss) {
  if (map()->IsJSGlobalProxyMap()) {
    Handle<Context> native_context = isolate()->native_context();
    Handle<WeakCell> weak_cell(native_context->self_weak_cell(), isolate());
    GenerateAccessCheck(weak_cell, scratch1(), scratch2(), miss, false);
  }

  return CheckPrototypes(object_reg, this->name(), scratch1(), scratch2(), name,
                         miss);
}

// The ICs that don't pass slot and vector through the stack have to
// save/restore them in the dispatcher.
bool PropertyHandlerCompiler::ShouldPushPopSlotAndVector() {
  switch (type()) {
    case LOAD:
      return true;
    case STORE:
      return !StoreWithVectorDescriptor::kPassLastArgsOnStack;
  }
  UNREACHABLE();
  return false;
}

Register PropertyHandlerCompiler::Frontend(Handle<Name> name) {
  Label miss;
  if (ShouldPushPopSlotAndVector()) PushVectorAndSlot();
  Register reg = FrontendHeader(receiver(), name, &miss);
  FrontendFooter(name, &miss);
  // The footer consumes the vector and slot from the stack if miss occurs.
  if (ShouldPushPopSlotAndVector()) DiscardVectorAndSlot();
  return reg;
}

#undef __

}  // namespace internal
}  // namespace v8
