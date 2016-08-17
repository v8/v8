// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/builtins/builtins-utils.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 24.2 DataView Objects

// ES6 section 24.2.2 The DataView Constructor for the [[Call]] case.
BUILTIN(DataViewConstructor) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate,
      NewTypeError(MessageTemplate::kConstructorNotFunction,
                   isolate->factory()->NewStringFromAsciiChecked("DataView")));
}

// ES6 section 24.2.2 The DataView Constructor for the [[Construct]] case.
BUILTIN(DataViewConstructor_ConstructStub) {
  HandleScope scope(isolate);
  Handle<JSFunction> target = args.target<JSFunction>();
  Handle<JSReceiver> new_target = Handle<JSReceiver>::cast(args.new_target());
  Handle<Object> buffer = args.atOrUndefined(isolate, 1);
  Handle<Object> byte_offset = args.atOrUndefined(isolate, 2);
  Handle<Object> byte_length = args.atOrUndefined(isolate, 3);

  // 2. If Type(buffer) is not Object, throw a TypeError exception.
  // 3. If buffer does not have an [[ArrayBufferData]] internal slot, throw a
  //    TypeError exception.
  if (!buffer->IsJSArrayBuffer()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kDataViewNotArrayBuffer));
  }
  Handle<JSArrayBuffer> array_buffer = Handle<JSArrayBuffer>::cast(buffer);

  // 4. Let numberOffset be ? ToNumber(byteOffset).
  Handle<Object> number_offset;
  if (byte_offset->IsUndefined(isolate)) {
    // We intentionally violate the specification at this point to allow
    // for new DataView(buffer) invocations to be equivalent to the full
    // new DataView(buffer, 0) invocation.
    number_offset = handle(Smi::FromInt(0), isolate);
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, number_offset,
                                       Object::ToNumber(byte_offset));
  }

  // 5. Let offset be ToInteger(numberOffset).
  Handle<Object> offset;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, offset,
                                     Object::ToInteger(isolate, number_offset));

  // 6. If numberOffset ≠ offset or offset < 0, throw a RangeError exception.
  if (number_offset->Number() != offset->Number() || offset->Number() < 0.0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidDataViewOffset));
  }

  // 7. If IsDetachedBuffer(buffer) is true, throw a TypeError exception.
  // We currently violate the specification at this point.

  // 8. Let bufferByteLength be the value of buffer's [[ArrayBufferByteLength]]
  // internal slot.
  double const buffer_byte_length = array_buffer->byte_length()->Number();

  // 9. If offset > bufferByteLength, throw a RangeError exception
  if (offset->Number() > buffer_byte_length) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidDataViewOffset));
  }

  Handle<Object> view_byte_length;
  if (byte_length->IsUndefined(isolate)) {
    // 10. If byteLength is undefined, then
    //       a. Let viewByteLength be bufferByteLength - offset.
    view_byte_length =
        isolate->factory()->NewNumber(buffer_byte_length - offset->Number());
  } else {
    // 11. Else,
    //       a. Let viewByteLength be ? ToLength(byteLength).
    //       b. If offset+viewByteLength > bufferByteLength, throw a RangeError
    //          exception
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, view_byte_length,
                                       Object::ToLength(isolate, byte_length));
    if (offset->Number() + view_byte_length->Number() > buffer_byte_length) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewRangeError(MessageTemplate::kInvalidDataViewLength));
    }
  }

  // 12. Let O be ? OrdinaryCreateFromConstructor(NewTarget,
  //     "%DataViewPrototype%", «[[DataView]], [[ViewedArrayBuffer]],
  //     [[ByteLength]], [[ByteOffset]]»).
  // 13. Set O's [[DataView]] internal slot to true.
  Handle<JSObject> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result,
                                     JSObject::New(target, new_target));
  for (int i = 0; i < ArrayBufferView::kInternalFieldCount; ++i) {
    Handle<JSDataView>::cast(result)->SetInternalField(i, Smi::FromInt(0));
  }

  // 14. Set O's [[ViewedArrayBuffer]] internal slot to buffer.
  Handle<JSDataView>::cast(result)->set_buffer(*array_buffer);

  // 15. Set O's [[ByteLength]] internal slot to viewByteLength.
  Handle<JSDataView>::cast(result)->set_byte_length(*view_byte_length);

  // 16. Set O's [[ByteOffset]] internal slot to offset.
  Handle<JSDataView>::cast(result)->set_byte_offset(*offset);

  // 17. Return O.
  return *result;
}

// ES6 section 24.2.4.1 get DataView.prototype.buffer
BUILTIN(DataViewPrototypeGetBuffer) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDataView, data_view, "get DataView.prototype.buffer");
  return data_view->buffer();
}

// ES6 section 24.2.4.2 get DataView.prototype.byteLength
BUILTIN(DataViewPrototypeGetByteLength) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDataView, data_view, "get DataView.prototype.byteLength");
  // TODO(bmeurer): According to the ES6 spec, we should throw a TypeError
  // here if the JSArrayBuffer of the {data_view} was neutered.
  return data_view->byte_length();
}

// ES6 section 24.2.4.3 get DataView.prototype.byteOffset
BUILTIN(DataViewPrototypeGetByteOffset) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSDataView, data_view, "get DataView.prototype.byteOffset");
  // TODO(bmeurer): According to the ES6 spec, we should throw a TypeError
  // here if the JSArrayBuffer of the {data_view} was neutered.
  return data_view->byte_offset();
}

}  // namespace internal
}  // namespace v8
