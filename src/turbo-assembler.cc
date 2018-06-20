// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/turbo-assembler.h"

#include "src/builtins/builtins.h"
#include "src/builtins/constants-table-builder.h"
#include "src/heap/heap-inl.h"
#include "src/snapshot/serializer-common.h"

namespace v8 {
namespace internal {

TurboAssemblerBase::TurboAssemblerBase(Isolate* isolate, const Options& options,
                                       void* buffer, int buffer_size,
                                       CodeObjectRequired create_code_object)
    : Assembler(options, buffer, buffer_size), isolate_(isolate) {
  if (create_code_object == CodeObjectRequired::kYes) {
    code_object_ = Handle<HeapObject>::New(
        isolate->heap()->self_reference_marker(), isolate);
  }
}

#ifdef V8_EMBEDDED_BUILTINS
void TurboAssemblerBase::IndirectLoadConstant(Register destination,
                                              Handle<HeapObject> object) {
  CHECK(isolate()->ShouldLoadConstantsFromRootList());
  CHECK(root_array_available_);

  // Before falling back to the (fairly slow) lookup from the constants table,
  // check if any of the fast paths can be applied.

  int builtin_index;
  Heap::RootListIndex root_index;
  if (isolate()->heap()->IsRootHandle(object, &root_index)) {
    // Roots are loaded relative to the root register.
    LoadRoot(destination, root_index);
  } else if (isolate()->builtins()->IsBuiltinHandle(object, &builtin_index)) {
    // Similar to roots, builtins may be loaded from the builtins table.
    LoadBuiltin(destination, builtin_index);
  } else if (object.is_identical_to(code_object_) &&
             Builtins::IsBuiltinId(maybe_builtin_index_)) {
    // The self-reference loaded through Codevalue() may also be a builtin
    // and thus viable for a fast load.
    LoadBuiltin(destination, maybe_builtin_index_);
  } else {
    // Ensure the given object is in the builtins constants table and fetch its
    // index.
    BuiltinsConstantsTableBuilder* builder =
        isolate()->builtins_constants_table_builder();
    uint32_t index = builder->AddObject(object);

    // Slow load from the constants table.
    LoadFromConstantsTable(destination, index);
  }
}

void TurboAssemblerBase::IndirectLoadExternalReference(
    Register destination, ExternalReference reference) {
  CHECK(isolate()->ShouldLoadConstantsFromRootList());
  CHECK(root_array_available_);

  if (reference.IsAddressableThroughRootRegister(isolate())) {
    // Some external references can be efficiently loaded as an offset from
    // kRootRegister.
    intptr_t offset = reference.OffsetFromRootRegister(isolate());
    LoadRootRegisterOffset(destination, offset);
  } else {
    // Otherwise, do a memory load from the external reference table.

    // Encode as an index into the external reference table stored on the
    // isolate.
    ExternalReferenceEncoder encoder(isolate());
    ExternalReferenceEncoder::Value v = encoder.Encode(reference.address());
    CHECK(!v.is_from_api());

    LoadExternalReference(destination, v.index());
  }
}
#endif  // V8_EMBEDDED_BUILTINS

}  // namespace internal
}  // namespace v8
