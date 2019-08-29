// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/protectors.h"

#include "src/execution/isolate-inl.h"
#include "src/execution/protectors-inl.h"
#include "src/handles/handles-inl.h"
#include "src/objects/contexts.h"
#include "src/objects/property-cell.h"
#include "src/objects/smi.h"
#include "src/tracing/trace-event.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

#define INVALIDATE_PROTECTOR_DEFINITION(name, cell)                         \
  void Protectors::Invalidate##name(Isolate* isolate,                       \
                                    Handle<NativeContext> native_context) { \
    DCHECK_EQ(*native_context, isolate->raw_native_context());              \
    DCHECK(native_context->cell().value().IsSmi());                         \
    DCHECK(Is##name##Intact(native_context));                               \
    Handle<PropertyCell> species_cell(native_context->cell(), isolate);     \
    PropertyCell::SetValueWithInvalidation(                                 \
        isolate, #cell, species_cell,                                       \
        handle(Smi::FromInt(kProtectorInvalid), isolate));                  \
    DCHECK(!Is##name##Intact(native_context));                              \
  }
DECLARED_PROTECTORS(INVALIDATE_PROTECTOR_DEFINITION)
#undef INVALIDATE_PROTECTOR_DEFINITION

}  // namespace internal
}  // namespace v8
