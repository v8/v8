// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_ALLOCATOR_H_
#define V8_INSPECTOR_ALLOCATOR_H_

#include <cstddef>
#include <cstdint>

enum NotNullTagEnum { NotNullLiteral };

#define V8_INSPECTOR_DISALLOW_NEW()                           \
 private:                                                     \
  void* operator new(size_t) = delete;                        \
  void* operator new(size_t, NotNullTagEnum, void*) = delete; \
  void* operator new(size_t, void*) = delete;                 \
                                                              \
 public:

#define V8_INSPECTOR_DISALLOW_COPY(ClassName) \
 private:                                     \
  ClassName(const ClassName&) = delete;       \
  ClassName& operator=(const ClassName&) = delete

// Macro that returns a compile time constant with the length of an array, but
// gives an error if passed a non-array.
template <typename T, std::size_t Size>
char (&ArrayLengthHelperFunction(T (&)[Size]))[Size];
// GCC needs some help to deduce a 0 length array.
#if defined(__GNUC__)
template <typename T>
char (&ArrayLengthHelperFunction(T (&)[0]))[0];
#endif
#define V8_INSPECTOR_ARRAY_LENGTH(array) \
  sizeof(::ArrayLengthHelperFunction(array))

#endif  // V8_INSPECTOR_ALLOCATOR_H_
