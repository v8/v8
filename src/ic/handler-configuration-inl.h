// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_HANDLER_CONFIGURATION_INL_H_
#define V8_IC_HANDLER_CONFIGURATION_INL_H_

#include "src/ic/handler-configuration.h"

#include "src/field-index-inl.h"

namespace v8 {
namespace internal {

Handle<Object> SmiHandler::MakeLoadFieldHandler(Isolate* isolate,
                                                FieldIndex field_index) {
  int config = LoadHandlerTypeBit::encode(kLoadICHandlerForProperties) |
               FieldOffsetIsInobject::encode(field_index.is_inobject()) |
               FieldOffsetIsDouble::encode(field_index.is_double()) |
               FieldOffsetOffset::encode(field_index.offset());
  return handle(Smi::FromInt(config), isolate);
}

Handle<Object> SmiHandler::MakeKeyedLoadHandler(Isolate* isolate,
                                                ElementsKind elements_kind,
                                                bool convert_hole_to_undefined,
                                                bool is_js_array) {
  int config = LoadHandlerTypeBit::encode(kLoadICHandlerForElements) |
               KeyedLoadElementsKind::encode(elements_kind) |
               KeyedLoadConvertHole::encode(convert_hole_to_undefined) |
               KeyedLoadIsJsArray::encode(is_js_array);
  return handle(Smi::FromInt(config), isolate);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_HANDLER_CONFIGURATION_INL_H_
