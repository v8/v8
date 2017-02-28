// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/conversions.h"
#include "src/counters.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

#define CHECK_IS_NOT_SHARED_ARRAY_BUFFER(name, method)                      \
  if (name->is_shared()) {                                                  \
    THROW_NEW_ERROR_RETURN_FAILURE(                                         \
        isolate,                                                            \
        NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,          \
                     isolate->factory()->NewStringFromAsciiChecked(method), \
                     name));                                                \
  }

// -----------------------------------------------------------------------------
// ES6 section 21.1 ArrayBuffer Objects

// ES6 section 24.1.2.1 ArrayBuffer ( length ) for the [[Call]] case.
BUILTIN(ArrayBufferConstructor) {
  HandleScope scope(isolate);
  Handle<JSFunction> target = args.target();
  DCHECK(*target == target->native_context()->array_buffer_fun() ||
         *target == target->native_context()->shared_array_buffer_fun());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kConstructorNotFunction,
                            handle(target->shared()->name(), isolate)));
}

// ES6 section 24.1.2.1 ArrayBuffer ( length ) for the [[Construct]] case.
BUILTIN(ArrayBufferConstructor_ConstructStub) {
  HandleScope scope(isolate);
  Handle<JSFunction> target = args.target();
  Handle<JSReceiver> new_target = Handle<JSReceiver>::cast(args.new_target());
  Handle<Object> length = args.atOrUndefined(isolate, 1);
  DCHECK(*target == target->native_context()->array_buffer_fun() ||
         *target == target->native_context()->shared_array_buffer_fun());
  Handle<Object> number_length;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, number_length,
                                     Object::ToInteger(isolate, length));
  if (number_length->Number() < 0.0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidArrayBufferLength));
  }
  Handle<JSObject> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result,
                                     JSObject::New(target, new_target));
  size_t byte_length;
  if (!TryNumberToSize(*number_length, &byte_length)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidArrayBufferLength));
  }
  SharedFlag shared_flag =
      (*target == target->native_context()->array_buffer_fun())
          ? SharedFlag::kNotShared
          : SharedFlag::kShared;
  if (!JSArrayBuffer::SetupAllocatingData(Handle<JSArrayBuffer>::cast(result),
                                          isolate, byte_length, true,
                                          shared_flag)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kArrayBufferAllocationFailed));
  }
  return *result;
}

// ES6 section 24.1.4.1 get ArrayBuffer.prototype.byteLength
BUILTIN(ArrayBufferPrototypeGetByteLength) {
  const char* const kMethodName = "get ArrayBuffer.prototype.byteLength";
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSArrayBuffer, array_buffer, kMethodName);
  CHECK_IS_NOT_SHARED_ARRAY_BUFFER(array_buffer, kMethodName);
  // TODO(franzih): According to the ES6 spec, we should throw a TypeError
  // here if the JSArrayBuffer is detached.
  return array_buffer->byte_length();
}

// ES6 section 24.1.3.1 ArrayBuffer.isView ( arg )
BUILTIN(ArrayBufferIsView) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(2, args.length());
  Object* arg = args[1];
  return isolate->heap()->ToBoolean(arg->IsJSArrayBufferView());
}

// ES #sec-arraybuffer.prototype.slice
// ArrayBuffer.prototype.slice ( start, end )
BUILTIN(ArrayBufferPrototypeSlice) {
  const char* const kMethodName = "ArrayBuffer.prototype.slice";
  HandleScope scope(isolate);
  Handle<Object> start = args.at(1);
  Handle<Object> end = args.atOrUndefined(isolate, 2);

  // 2. If Type(O) is not Object, throw a TypeError exception.
  // 3. If O does not have an [[ArrayBufferData]] internal slot, throw a
  //    TypeError exception.
  CHECK_RECEIVER(JSArrayBuffer, array_buffer, kMethodName);
  // 4. If IsSharedArrayBuffer(O) is true, throw a TypeError exception.
  CHECK_IS_NOT_SHARED_ARRAY_BUFFER(array_buffer, kMethodName);

  // 5. If IsDetachedBuffer(buffer) is true, throw a TypeError exception.
  if (array_buffer->was_neutered()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kDetachedOperation,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  kMethodName)));
  }

  // 6. Let len be O.[[ArrayBufferByteLength]].
  double const len = array_buffer->byte_length()->Number();

  // 7. Let relativeStart be ? ToInteger(start).
  Handle<Object> relative_start;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, relative_start,
                                     Object::ToInteger(isolate, start));

  // 8. If relativeStart < 0, let first be max((len + relativeStart), 0); else
  //    let first be min(relativeStart, len).
  double const first = (relative_start->Number() < 0)
                           ? Max(len + relative_start->Number(), 0.0)
                           : Min(relative_start->Number(), len);
  Handle<Object> first_obj = isolate->factory()->NewNumber(first);

  // 9. If end is undefined, let relativeEnd be len; else let relativeEnd be ?
  //    ToInteger(end).
  double relative_end;
  if (end->IsUndefined(isolate)) {
    relative_end = len;
  } else {
    Handle<Object> relative_end_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, relative_end_obj,
                                       Object::ToInteger(isolate, end));
    relative_end = relative_end_obj->Number();
  }

  // 10. If relativeEnd < 0, let final be max((len + relativeEnd), 0); else let
  //     final be min(relativeEnd, len).
  double const final_ = (relative_end < 0) ? Max(len + relative_end, 0.0)
                                           : Min(relative_end, len);

  // 11. Let newLen be max(final-first, 0).
  double const new_len = Max(final_ - first, 0.0);
  Handle<Object> new_len_obj = isolate->factory()->NewNumber(new_len);

  // 12. Let ctor be ? SpeciesConstructor(O, %ArrayBuffer%).
  Handle<JSFunction> arraybuffer_fun = isolate->array_buffer_fun();
  Handle<Object> ctor;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, ctor,
      Object::SpeciesConstructor(
          isolate, Handle<JSReceiver>::cast(args.receiver()), arraybuffer_fun));

  // 13. Let new be ? Construct(ctor, newLen).
  Handle<JSReceiver> new_;
  {
    const int argc = 1;

    ScopedVector<Handle<Object>> argv(argc);
    argv[0] = new_len_obj;

    Handle<Object> new_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, new_obj,
        Execution::New(Handle<JSFunction>::cast(ctor), argc, argv.start()));

    new_ = Handle<JSReceiver>::cast(new_obj);
  }

  // 14. If new does not have an [[ArrayBufferData]] internal slot, throw a
  //     TypeError exception.
  if (!new_->IsJSArrayBuffer()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                     isolate->factory()->NewStringFromAsciiChecked(kMethodName),
                     new_));
  }

  // 15. If IsSharedArrayBuffer(new) is true, throw a TypeError exception.
  Handle<JSArrayBuffer> new_array_buffer = Handle<JSArrayBuffer>::cast(new_);
  CHECK_IS_NOT_SHARED_ARRAY_BUFFER(new_array_buffer, kMethodName);

  // 16. If IsDetachedBuffer(new) is true, throw a TypeError exception.
  if (new_array_buffer->was_neutered()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kDetachedOperation,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  kMethodName)));
  }

  // 17. If SameValue(new, O) is true, throw a TypeError exception.
  if (new_->SameValue(*args.receiver())) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kArrayBufferSpeciesThis));
  }

  // 18. If new.[[ArrayBufferByteLength]] < newLen, throw a TypeError exception.
  if (new_array_buffer->byte_length()->Number() < new_len) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kArrayBufferTooShort));
  }

  // 19. NOTE: Side-effects of the above steps may have detached O.
  // 20. If IsDetachedBuffer(O) is true, throw a TypeError exception.
  if (array_buffer->was_neutered()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kDetachedOperation,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  kMethodName)));
  }

  // 21. Let fromBuf be O.[[ArrayBufferData]].
  // 22. Let toBuf be new.[[ArrayBufferData]].
  // 23. Perform CopyDataBlockBytes(toBuf, 0, fromBuf, first, newLen).
  size_t first_size = 0, new_len_size = 0;
  CHECK(TryNumberToSize(*first_obj, &first_size));
  CHECK(TryNumberToSize(*new_len_obj, &new_len_size));
  DCHECK(NumberToSize(new_array_buffer->byte_length()) >= new_len_size);

  if (new_len_size != 0) {
    size_t from_byte_length = NumberToSize(array_buffer->byte_length());
    USE(from_byte_length);
    DCHECK(first_size <= from_byte_length);
    DCHECK(from_byte_length - first_size >= new_len_size);
    uint8_t* from_data =
        reinterpret_cast<uint8_t*>(array_buffer->backing_store());
    uint8_t* to_data =
        reinterpret_cast<uint8_t*>(new_array_buffer->backing_store());
    CopyBytes(to_data, from_data + first_size, new_len_size);
  }

  return *new_;
}

}  // namespace internal
}  // namespace v8
