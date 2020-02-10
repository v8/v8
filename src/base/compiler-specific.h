// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_COMPILER_SPECIFIC_H_
#define V8_BASE_COMPILER_SPECIFIC_H_

#include "include/v8config.h"

// Annotate a  using ALLOW_UNUSED_TYPE = or function indicating it's ok if it's
// not used. Use like:
//    using Bar = Foo;
#if V8_HAS_ATTRIBUTE_UNUSED
#define ALLOW_UNUSED_TYPE __attribute__((unused))
#else
#define ALLOW_UNUSED_TYPE
#endif

// Tell the compiler a function is using a printf-style format string.
// |format_param| is the one-based index of the format string parameter;
// |dots_param| is the one-based index of the "..." parameter.
// For v*printf functions (which take a va_list), pass 0 for dots_param.
// (This is undocumented but matches what the system C headers do.)
#if defined(__GNUC__)
#define PRINTF_FORMAT(format_param, dots_param) \
  __attribute__((format(printf, format_param, dots_param)))
#else
#define PRINTF_FORMAT(format_param, dots_param)
#endif

// The C++ standard requires that static const members have an out-of-class
// definition (in a single compilation unit), but MSVC chokes on this (when
// language extensions, which are required, are enabled). (You're only likely to
// notice the need for a definition if you take the address of the member or,
// more commonly, pass it to a function that takes it as a reference argument --
// probably an STL function.) This macro makes MSVC do the right thing. See
// http://msdn.microsoft.com/en-us/library/34h23df8(v=vs.100).aspx for more
// information. Use like:
//
// In .h file:
//   struct Foo {
//     static const int kBar = 5;
//   };
//
// In .cc file:
//   STATIC_CONST_MEMBER_DEFINITION const int Foo::kBar;
#if V8_HAS_DECLSPEC_SELECTANY
#define STATIC_CONST_MEMBER_DEFINITION __declspec(selectany)
#else
#define STATIC_CONST_MEMBER_DEFINITION
#endif

#if V8_CC_MSVC

#include <sal.h>

// Macros for suppressing and disabling warnings on MSVC.
//
// Warning numbers are enumerated at:
// http://msdn.microsoft.com/en-us/library/8x5x43k7(VS.80).aspx
//
// The warning pragma:
// http://msdn.microsoft.com/en-us/library/2c8f766e(VS.80).aspx
//
// Using __pragma instead of #pragma inside macros:
// http://msdn.microsoft.com/en-us/library/d9x1s805.aspx

// MSVC_SUPPRESS_WARNING disables warning |n| for the remainder of the line and
// for the next line of the source file.
#define MSVC_SUPPRESS_WARNING(n) __pragma(warning(suppress : n))

// Allows exporting a class that inherits from a non-exported base class.
// This uses suppress instead of push/pop because the delimiter after the
// declaration (either "," or "{") has to be placed before the pop macro.
//
// Example usage:
// class EXPORT_API Foo : NON_EXPORTED_BASE(public Bar) {
//
// MSVC Compiler warning C4275:
// non dll-interface class 'Bar' used as base for dll-interface class 'Foo'.
// Note that this is intended to be used only when no access to the base class'
// static data is done through derived classes or inline methods. For more info,
// see http://msdn.microsoft.com/en-us/library/3tdb471s(VS.80).aspx
#define NON_EXPORTED_BASE(code) \
  MSVC_SUPPRESS_WARNING(4275)   \
  code

#else  // Not MSVC

#define MSVC_SUPPRESS_WARNING(n)
#define NON_EXPORTED_BASE(code) code

#endif  // V8_CC_MSVC

// Allowing the use of noexcept by removing the keyword on older compilers that
// do not support adding noexcept to default members.
// Disabled on MSVC because constructors of standard containers are not noexcept
// there.
#if ((!defined(V8_CC_GNU) && !defined(V8_CC_MSVC) &&                      \
      !defined(V8_TARGET_ARCH_MIPS) && !defined(V8_TARGET_ARCH_MIPS64) && \
      !defined(V8_TARGET_ARCH_PPC) && !defined(V8_TARGET_ARCH_PPC64)) ||  \
     (defined(__clang__) && __cplusplus > 201300L))
#define V8_NOEXCEPT noexcept
#else
#define V8_NOEXCEPT
#endif

#if defined(__clang__)

#if __has_attribute(uninitialized)
// Attribute "uninitialized" disables -ftrivial-auto-var-init=pattern for
// the specified variable.
// Library-wide alternative is
// 'configs -= [ "//build/config/compiler:default_init_stack_vars" ]' in .gn
// file.
//
// See "init_stack_vars" in build/config/compiler/BUILD.gn and
// http://crbug.com/977230
// "init_stack_vars" is enabled for non-official builds and we hope to enable it
// in official build in 2020 as well. The flag writes fixed pattern into
// uninitialized parts of all local variables. In rare cases such initialization
// is undesirable and attribute can be used:
//   1. Degraded performance
// In most cases compiler is able to remove additional stores. E.g. if memory is
// never accessed or properly initialized later. Preserved stores mostly will
// not affect program performance. However if compiler failed on some
// performance critical code we can get a visible regression in a benchmark.
//   2. memset, memcpy calls
// Compiler may replace some memory writes with memset or memcpy calls. This is
// not -ftrivial-auto-var-init specific, but it can happen more likely with the
// flag. It can be a problem if code is not linked with C run-time library.
//
// Note: The flag is security risk mitigation feature. So in future the
// attribute uses should be avoided when possible. However to enable this
// mitigation on the most of the code we need to be less strict now and minimize
// number of exceptions later. So if in doubt feel free to use attribute, but
// please document the problem for someone who is going to cleanup it later.
// E.g. platform, bot, benchmark or test name in patch description or next to
// the attribute.
#define V8_STACK_UNINITIALIZED __attribute__((uninitialized))
#else  // No attribute uninitialized
#define V8_STACK_UNINITIALIZED
#endif  // attribute uninitialized

#else  // Not clang
#define V8_STACK_UNINITIALIZED
#endif  // clang

#endif  // V8_BASE_COMPILER_SPECIFIC_H_
