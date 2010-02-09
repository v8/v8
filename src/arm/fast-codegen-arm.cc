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

#include "codegen-inl.h"
#include "fast-codegen.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm())

Register FastCodeGenerator::accumulator0() { return r0; }
Register FastCodeGenerator::accumulator1() { return r1; }
Register FastCodeGenerator::scratch0() { return r3; }
Register FastCodeGenerator::scratch1() { return r4; }
Register FastCodeGenerator::receiver_reg() { return r2; }
Register FastCodeGenerator::context_reg() { return cp; }


void FastCodeGenerator::EmitLoadReceiver() {
  // Offset 2 is due to return address and saved frame pointer.
  int index = 2 + scope()->num_parameters();
  __ ldr(receiver_reg(), MemOperand(sp, index * kPointerSize));
}


void FastCodeGenerator::EmitGlobalVariableLoad(Handle<Object> cell) {
  ASSERT(cell->IsJSGlobalPropertyCell());
  __ mov(accumulator0(), Operand(cell));
  __ ldr(accumulator0(),
         FieldMemOperand(accumulator0(), JSGlobalPropertyCell::kValueOffset));
  if (FLAG_debug_code) {
    __ mov(ip, Operand(Factory::the_hole_value()));
    __ cmp(accumulator0(), ip);
    __ Check(ne, "DontDelete cells can't contain the hole");
  }
}


void FastCodeGenerator::EmitThisPropertyStore(Handle<String> name) {
  LookupResult lookup;
  info()->receiver()->Lookup(*name, &lookup);

  ASSERT(lookup.holder() == *info()->receiver());
  ASSERT(lookup.type() == FIELD);
  Handle<Map> map(Handle<HeapObject>::cast(info()->receiver())->map());
  int index = lookup.GetFieldIndex() - map->inobject_properties();
  int offset = index * kPointerSize;

  // Negative offsets are inobject properties.
  if (offset < 0) {
    offset += map->instance_size();
    __ mov(scratch0(), receiver_reg());  // Copy receiver for write barrier.
  } else {
    offset += FixedArray::kHeaderSize;
    __ ldr(scratch0(),
           FieldMemOperand(receiver_reg(), JSObject::kPropertiesOffset));
  }
  // Perform the store.
  __ str(accumulator0(), FieldMemOperand(scratch0(), offset));
  __ mov(scratch1(), Operand(offset));
  __ RecordWrite(scratch0(), scratch1(), ip);
}


void FastCodeGenerator::Generate(CompilationInfo* compilation_info) {
  ASSERT(info_ == NULL);
  info_ = compilation_info;

  // Save the caller's frame pointer and set up our own.
  Comment prologue_cmnt(masm(), ";; Prologue");
  __ stm(db_w, sp, r1.bit() | cp.bit() | fp.bit() | lr.bit());
  __ add(fp, sp, Operand(2 * kPointerSize));
  // Note that we keep a live register reference to cp (context) at
  // this point.

  // Receiver (this) is allocated to a fixed register.
  if (info()->has_this_properties()) {
    Comment cmnt(masm(), ";; MapCheck(this)");
    if (FLAG_print_ir) {
      PrintF("MapCheck(this)\n");
    }
    ASSERT(info()->has_receiver() && info()->receiver()->IsHeapObject());
    Handle<HeapObject> object = Handle<HeapObject>::cast(info()->receiver());
    Handle<Map> map(object->map());
    EmitLoadReceiver();
    __ CheckMap(receiver_reg(), scratch0(), map, bailout(), false);
  }

  // If there is a global variable access check if the global object is the
  // same as at lazy-compilation time.
  if (info()->has_globals()) {
    Comment cmnt(masm(), ";; MapCheck(GLOBAL)");
    if (FLAG_print_ir) {
      PrintF("MapCheck(GLOBAL)\n");
    }
    ASSERT(info()->has_global_object());
    Handle<Map> map(info()->global_object()->map());
    __ ldr(scratch0(), CodeGenerator::GlobalObject());
    __ CheckMap(scratch0(), scratch1(), map, bailout(), true);
  }

  VisitStatements(function()->body());

  Comment return_cmnt(masm(), ";; Return(<undefined>)");
  if (FLAG_print_ir) {
    PrintF("Return(<undefined>)\n");
  }
  __ LoadRoot(r0, Heap::kUndefinedValueRootIndex);
  __ mov(sp, fp);
  __ ldm(ia_w, sp, fp.bit() | lr.bit());
  int32_t sp_delta = (scope()->num_parameters() + 1) * kPointerSize;
  __ add(sp, sp, Operand(sp_delta));
  __ Jump(lr);

  __ bind(&bailout_);
}


#undef __


} }  // namespace v8::internal
