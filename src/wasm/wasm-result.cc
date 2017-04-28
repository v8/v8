// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-result.h"

#include "src/factory.h"
#include "src/heap/heap.h"
#include "src/isolate-inl.h"
#include "src/objects.h"

#include "src/base/platform/platform.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

PRINTF_FORMAT(3, 0)
void VPrintFToString(std::string& str, size_t str_offset, const char* format,
                     va_list args) {
  DCHECK_LE(str_offset, str.size());
  size_t len = str_offset + strlen(format);
  // Allocate increasingly large buffers until the message fits.
  for (;; len = base::bits::RoundUpToPowerOfTwo64(len + 1)) {
    DCHECK_GE(kMaxInt, len);
    str.resize(len);
    int written = VSNPrintF(Vector<char>(&str.front() + str_offset,
                                         static_cast<int>(len - str_offset)),
                            format, args);
    if (written < 0) continue;  // not enough space.
    str.resize(str_offset + written);
    return;
  }
}

}  // namespace

void ResultBase::error(uint32_t offset, std::string error_msg) {
  // The error message must not be empty, otherwise Result::failed() will be
  // false.
  DCHECK(!error_msg.empty());
  error_offset_ = offset;
  error_msg_ = std::move(error_msg);
}

void ResultBase::verror(const char* format, va_list args) {
  VPrintFToString(error_msg_, 0, format, args);
  // Assign default message such that ok() and failed() work.
  if (error_msg_.empty() == 0) error_msg_.assign("Error");
}

void ErrorThrower::Format(i::Handle<i::JSFunction> constructor,
                          const char* format, va_list args) {
  // Only report the first error.
  if (error()) return;

  constexpr int kMaxErrorMessageLength = 256;
  EmbeddedVector<char, kMaxErrorMessageLength> buffer;

  int context_len = 0;
  if (context_) {
    context_len = SNPrintF(buffer, "%s: ", context_);
    CHECK_LE(0, context_len);  // check for overflow.
  }

  int message_len =
      VSNPrintF(buffer.SubVector(context_len, buffer.length()), format, args);
  CHECK_LE(0, message_len);  // check for overflow.

  Vector<char> whole_message = buffer.SubVector(0, context_len + message_len);
  i::Handle<i::String> message =
      isolate_->factory()
          ->NewStringFromOneByte(Vector<uint8_t>::cast(whole_message))
          .ToHandleChecked();
  exception_ = isolate_->factory()->NewError(constructor, message);
}

void ErrorThrower::TypeError(const char* format, ...) {
  if (error()) return;
  va_list arguments;
  va_start(arguments, format);
  Format(isolate_->type_error_function(), format, arguments);
  va_end(arguments);
}

void ErrorThrower::RangeError(const char* format, ...) {
  if (error()) return;
  va_list arguments;
  va_start(arguments, format);
  Format(isolate_->range_error_function(), format, arguments);
  va_end(arguments);
}

void ErrorThrower::CompileError(const char* format, ...) {
  if (error()) return;
  wasm_error_ = true;
  va_list arguments;
  va_start(arguments, format);
  Format(isolate_->wasm_compile_error_function(), format, arguments);
  va_end(arguments);
}

void ErrorThrower::LinkError(const char* format, ...) {
  if (error()) return;
  wasm_error_ = true;
  va_list arguments;
  va_start(arguments, format);
  Format(isolate_->wasm_link_error_function(), format, arguments);
  va_end(arguments);
}

void ErrorThrower::RuntimeError(const char* format, ...) {
  if (error()) return;
  wasm_error_ = true;
  va_list arguments;
  va_start(arguments, format);
  Format(isolate_->wasm_runtime_error_function(), format, arguments);
  va_end(arguments);
}

ErrorThrower::~ErrorThrower() {
  if (error() && !isolate_->has_pending_exception()) {
    isolate_->ScheduleThrow(*exception_);
  }
}
}  // namespace wasm
}  // namespace internal
}  // namespace v8
