// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "test/cctest/compiler/function-tester.h"

using namespace v8::internal;
using namespace v8::internal::compiler;

template <typename U>
static void TypedArrayLoadHelper(const char* array_type) {
  static const uint32_t kValues[] = {
      0x00000000, 0x00000001, 0x00000023, 0x00000042, 0x12345678, 0x87654321,
      0x0000003f, 0x0000007f, 0x00003fff, 0x00007fff, 0x3fffffff, 0x7fffffff,
      0x000000ff, 0x00000080, 0x0000ffff, 0x00008000, 0xffffffff, 0x80000000};
  EmbeddedVector<char, 1024> values_buffer;
  StringBuilder values_builder(values_buffer.start(), values_buffer.length());
  for (size_t i = 0; i < arraysize(kValues); ++i) {
    values_builder.AddFormatted("a[%d] = 0x%08x;", i, kValues[i]);
  }

  // Note that below source creates two different typed arrays with distinct
  // elements kind to get coverage for both access patterns:
  // - IsFixedTypedArrayElementsKind(x)
  // - IsExternalArrayElementsKind(y)
  const char* source =
      "(function(a) {"
      "  var x = (a = new %sArray(%d)); %s;"
      "  var y = (a = new %sArray(%d)); %s; %%TypedArrayGetBuffer(y);"
      "  if (!%%HasFixed%sElements(x)) %%AbortJS('x');"
      "  if (!%%HasExternal%sElements(y)) %%AbortJS('y');"
      "  function f(a,b) {"
      "    a = a | 0; b = b | 0;"
      "    return x[a] + y[b];"
      "  }"
      "  return f;"
      "})()";
  EmbeddedVector<char, 1024> source_buffer;
  SNPrintF(source_buffer, source, array_type, arraysize(kValues),
           values_buffer.start(), array_type, arraysize(kValues),
           values_buffer.start(), array_type, array_type);

  FunctionTester T(
      source_buffer.start(),
      CompilationInfo::kContextSpecializing | CompilationInfo::kTypingEnabled);
  for (size_t i = 0; i < arraysize(kValues); ++i) {
    for (size_t j = 0; j < arraysize(kValues); ++j) {
      double value_a = static_cast<U>(kValues[i]);
      double value_b = static_cast<U>(kValues[j]);
      double expected = value_a + value_b;
      T.CheckCall(T.Val(expected), T.Val(static_cast<double>(i)),
                  T.Val(static_cast<double>(j)));
    }
  }
}


TEST(TypedArrayLoad) {
  FLAG_typed_array_max_size_in_heap = 256;
  TypedArrayLoadHelper<int8_t>("Int8");
  TypedArrayLoadHelper<uint8_t>("Uint8");
  TypedArrayLoadHelper<int16_t>("Int16");
  TypedArrayLoadHelper<uint16_t>("Uint16");
  TypedArrayLoadHelper<int32_t>("Int32");
  TypedArrayLoadHelper<uint32_t>("Uint32");
  TypedArrayLoadHelper<double>("Float64");
  // TODO(mstarzinger): Add tests for Float32.
  // TODO(mstarzinger): Add tests for ClampedUint8.
}
