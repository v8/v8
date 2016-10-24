// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_HANDLER_CONFIGURATION_INL_H_
#define V8_IC_HANDLER_CONFIGURATION_INL_H_

#include "src/ic/handler-configuration.h"

#include "src/field-index-inl.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

Handle<Object> LoadHandler::LoadField(Isolate* isolate,
                                      FieldIndex field_index) {
  int config = KindBits::encode(kForFields) |
               IsInobjectBits::encode(field_index.is_inobject()) |
               IsDoubleBits::encode(field_index.is_double()) |
               FieldOffsetBits::encode(field_index.offset());
  return handle(Smi::FromInt(config), isolate);
}

Handle<Object> LoadHandler::LoadConstant(Isolate* isolate, int descriptor) {
  int config = KindBits::encode(kForConstants) |
               DescriptorValueIndexBits::encode(
                   DescriptorArray::ToValueIndex(descriptor));
  return handle(Smi::FromInt(config), isolate);
}

Handle<Object> LoadHandler::LoadElement(Isolate* isolate,
                                        ElementsKind elements_kind,
                                        bool convert_hole_to_undefined,
                                        bool is_js_array) {
  int config = KindBits::encode(kForElements) |
               ElementsKindBits::encode(elements_kind) |
               ConvertHoleBits::encode(convert_hole_to_undefined) |
               IsJsArrayBits::encode(is_js_array);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Object> StoreHandler::StoreField(Isolate* isolate, int descriptor,
                                        FieldIndex field_index,
                                        Representation representation) {
  StoreHandler::FieldRepresentation field_rep;
  switch (representation.kind()) {
    case Representation::kSmi:
      field_rep = StoreHandler::kSmi;
      break;
    case Representation::kDouble:
      field_rep = StoreHandler::kDouble;
      break;
    case Representation::kHeapObject:
      field_rep = StoreHandler::kHeapObject;
      break;
    case Representation::kTagged:
      field_rep = StoreHandler::kTagged;
      break;
    default:
      UNREACHABLE();
      return Handle<Object>::null();
  }
  int value_index = DescriptorArray::ToValueIndex(descriptor);

  int config = StoreHandler::KindBits::encode(StoreHandler::kForFields) |
               StoreHandler::IsInobjectBits::encode(field_index.is_inobject()) |
               StoreHandler::FieldRepresentationBits::encode(field_rep) |
               StoreHandler::DescriptorValueIndexBits::encode(value_index) |
               StoreHandler::FieldOffsetBits::encode(field_index.offset());
  return handle(Smi::FromInt(config), isolate);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_HANDLER_CONFIGURATION_INL_H_
