// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8CONFIG_H_
#define V8CONFIG_H_

// -----------------------------------------------------------------------------
// Operating system detection
//
//  V8_OS_ANDROID       - Android
//  V8_OS_BSD           - BSDish (Mac OS X, Net/Free/Open/DragonFlyBSD)
//  V8_OS_CYGWIN        - Cygwin
//  V8_OS_DRAGONFLYBSD  - DragonFlyBSD
//  V8_OS_FREEBSD       - FreeBSD
//  V8_OS_LINUX         - Linux
//  V8_OS_MACOSX        - Mac OS X
//  V8_OS_NACL          - Native Client
//  V8_OS_NETBSD        - NetBSD
//  V8_OS_OPENBSD       - OpenBSD
//  V8_OS_POSIX         - POSIX compatible (mostly everything except Windows)
//  V8_OS_SOLARIS       - Sun Solaris and OpenSolaris
//  V8_OS_WIN           - Microsoft Windows

#if defined(__ANDROID__)
# define V8_OS_ANDROID 1
# define V8_OS_LINUX 1
# define V8_OS_POSIX 1
#elif defined(__APPLE__)
# define V8_OS_BSD 1
# define V8_OS_MACOSX 1
# define V8_OS_POSIX 1
#elif defined(__native_client__)
# define V8_OS_NACL 1
# define V8_OS_POSIX 1
#elif defined(__CYGWIN__)
# define V8_OS_CYGWIN 1
# define V8_OS_POSIX 1
#elif defined(__linux__)
# define V8_OS_LINUX 1
# define V8_OS_POSIX 1
#elif defined(__sun)
# define V8_OS_POSIX 1
# define V8_OS_SOLARIS 1
#elif defined(__FreeBSD__)
# define V8_OS_BSD 1
# define V8_OS_FREEBSD 1
# define V8_OS_POSIX 1
#elif defined(__DragonFly__)
# define V8_OS_BSD 1
# define V8_OS_DRAGONFLYBSD 1
# define V8_OS_POSIX 1
#elif defined(__NetBSD__)
# define V8_OS_BSD 1
# define V8_OS_NETBSD 1
# define V8_OS_POSIX 1
#elif defined(__OpenBSD__)
# define V8_OS_BSD 1
# define V8_OS_OPENBSD 1
# define V8_OS_POSIX 1
#elif defined(_WIN32)
# define V8_OS_WIN 1
#endif


// -----------------------------------------------------------------------------
// Compiler detection
//
//  V8_CC_CLANG   - Clang
//  V8_CC_GNU     - GNU C++
//  V8_CC_MINGW   - Minimalist GNU for Windows
//  V8_CC_MSVC    - Microsoft Visual C/C++
//
// C++11 feature detection
//
//  V8_HAS_CXX11_ALIGNAS        - alignas specifier supported
//  V8_HAS_CXX11_STATIC_ASSERT  - static_assert() supported
//  V8_HAS_CXX11_DELETE         - deleted functions supported
//  V8_HAS_CXX11_FINAL          - final marker supported
//  V8_HAS_CXX11_OVERRIDE       - override marker supported
//
// Compiler-specific feature detection
//
//  V8_HAS_ATTRIBUTE___ALIGNED__    - __attribute__((__aligned__(n))) supported
//  V8_HAS_ATTRIBUTE_ALWAYS_INLINE  - __attribute__((always_inline)) supported
//  V8_HAS_ATTRIBUTE_DEPRECATED     - __attribute__((deprecated)) supported
//  V8_HAS_ATTRIBUTE_VISIBILITY     - __attribute__((visibility)) supported
//  V8_HAS_BUILTIN_EXPECT           - __builtin_expect() supported
//  V8_HAS_DECLSPEC_ALIGN           - __declspec(align(n)) supported
//  V8_HAS_DECLSPEC_DEPRECATED      - __declspec(deprecated) supported
//  V8_HAS___FINAL                  - __final supported in non-C++11 mode
//  V8_HAS___FORCEINLINE            - __forceinline supported
//  V8_HAS_SEALED                   - MSVC style sealed marker supported

#if defined(__clang__)

// Don't treat clang as GCC.
# define V8_GNUC_PREREQ(major, minor, patchlevel) 0

# define V8_CC_CLANG 1

# define V8_HAS_ATTRIBUTE___ALIGNED__ (__has_attribute(__aligned__))
# define V8_HAS_ATTRIBUTE_ALWAYS_INLINE (__has_attribute(always_inline))
# define V8_HAS_ATTRIBUTE_DEPRECATED (__has_attribute(deprecated))
# define V8_HAS_ATTRIBUTE_VISIBILITY (__has_attribute(visibility))

# define V8_HAS_BUILTIN_EXPECT (__has_builtin(__builtin_expect))

# define V8_HAS_CXX11_ALIGNAS (__has_feature(cxx_alignas))
# define V8_HAS_CXX11_STATIC_ASSERT (__has_feature(cxx_static_assert))
# define V8_HAS_CXX11_DELETE (__has_feature(cxx_deleted_functions))
# define V8_HAS_CXX11_FINAL (__has_feature(cxx_override_control))
# define V8_HAS_CXX11_OVERRIDE (__has_feature(cxx_override_control))

#elif defined(__GNUC__)

# define V8_GNUC_PREREQ(major, minor, patchlevel)                         \
    ((__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__) >=   \
     ((major) * 10000 + (minor) * 100 + (patchlevel)))

# define V8_CC_GNU 1
# if defined(__MINGW32__)
#  define V8_CC_MINGW 1
# endif

# define V8_HAS_ATTRIBUTE___ALIGNED__ (V8_GNUC_PREREQ(2, 95, 0))
// always_inline is available in gcc 4.0 but not very reliable until 4.4.
// Works around "sorry, unimplemented: inlining failed" build errors with
// older compilers.
# define V8_HAS_ATTRIBUTE_ALWAYS_INLINE (V8_GNUC_PREREQ(4, 4, 0))
# define V8_HAS_ATTRIBUTE_DEPRECATED (V8_GNUC_PREREQ(3, 4, 0))
# define V8_HAS_ATTRIBUTE_VISIBILITY (V8_GNUC_PREREQ(4, 3, 0))

# define V8_HAS_BUILTIN_EXPECT (V8_GNUC_PREREQ(2, 96, 0))

// g++ requires -std=c++0x or -std=gnu++0x to support C++11 functionality
// without warnings (functionality used by the macros below).  These modes
// are detectable by checking whether __GXX_EXPERIMENTAL_CXX0X__ is defined or,
// more standardly, by checking whether __cplusplus has a C++11 or greater
// value. Current versions of g++ do not correctly set __cplusplus, so we check
// both for forward compatibility.
# if defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus >= 201103L
#  define V8_HAS_CXX11_ALIGNAS (V8_GNUC_PREREQ(4, 8, 0))
#  define V8_HAS_CXX11_STATIC_ASSERT (V8_GNUC_PREREQ(4, 3, 0))
#  define V8_HAS_CXX11_DELETE (V8_GNUC_PREREQ(4, 4, 0))
#  define V8_HAS_CXX11_OVERRIDE (V8_GNUC_PREREQ(4, 7, 0))
#  define V8_HAS_CXX11_FINAL (V8_GNUC_PREREQ(4, 7, 0))
# else
// '__final' is a non-C++11 GCC synonym for 'final', per GCC r176655.
#  define V8_HAS___FINAL (V8_GNUC_PREREQ(4, 7, 0))
# endif

#elif defined(_MSC_VER)

# define V8_GNUC_PREREQ(major, minor, patchlevel) 0

# define V8_CC_MSVC 1

// Override control was added with Visual Studio 2005, but
// Visual Studio 2010 and earlier spell "final" as "sealed".
# define V8_HAS_CXX11_FINAL (_MSC_VER >= 1700)
# define V8_HAS_CXX11_OVERRIDE (_MSC_VER >= 1400)
# define V8_HAS_SEALED (_MSC_VER >= 1400)

# define V8_HAS_DECLSPEC_ALIGN 1
# define V8_HAS_DECLSPEC_DEPRECATED (_MSC_VER >= 1300)

# define V8_HAS___FORCEINLINE 1

#endif


// -----------------------------------------------------------------------------
// Helper macros

// A macro used to make better inlining. Don't bother for debug builds.
#if !defined(DEBUG) && V8_HAS_ATTRIBUTE_ALWAYS_INLINE
# define V8_INLINE(declarator) inline __attribute__((always_inline)) declarator
#elif !defined(DEBUG) && V8_HAS___FORCEINLINE
# define V8_INLINE(declarator) __forceinline declarator
#else
# define V8_INLINE(declarator) inline declarator
#endif


// A macro to mark classes or functions as deprecated.
#if !V8_DISABLE_DEPRECATIONS && V8_HAS_ATTRIBUTE_DEPRECATED
# define V8_DEPRECATED(declarator) declarator __attribute__((deprecated))
#elif !V8_DISABLE_DEPRECATIONS && V8_HAS_DECLSPEC_DEPRECATED
# define V8_DEPRECATED(declarator) __declspec(deprecated) declarator
#else
# define V8_DEPRECATED(declarator) declarator
#endif


// A macro to provide the compiler with branch prediction information.
#if V8_HAS_BUILTIN_EXPECT
# define V8_UNLIKELY(condition) (__builtin_expect(!!(condition), 0))
# define V8_LIKELY(condition) (__builtin_expect(!!(condition), 1))
#else
# define V8_UNLIKELY(condition) (condition)
# define V8_LIKELY(condition) (condition)
#endif


// A macro to specify that a method is deleted from the corresponding class.
// Any attempt to use the method will always produce an error at compile time
// when this macro can be implemented (i.e. if the compiler supports C++11).
// If the current compiler does not support C++11, use of the annotated method
// will still cause an error, but the error will most likely occur at link time
// rather than at compile time. As a backstop, method declarations using this
// macro should be private.
// Use like:
//   class A {
//    private:
//     A(const A& other) V8_DELETE;
//     A& operator=(const A& other) V8_DELETE;
//   };
#if V8_HAS_CXX11_DELETE
# define V8_DELETE = delete
#else
# define V8_DELETE /* NOT SUPPORTED */
#endif


// Annotate a virtual method indicating it must be overriding a virtual
// method in the parent class.
// Use like:
//   virtual void bar() V8_OVERRIDE;
#if V8_HAS_CXX11_OVERRIDE
# define V8_OVERRIDE override
#else
# define V8_OVERRIDE /* NOT SUPPORTED */
#endif


// Annotate a virtual method indicating that subclasses must not override it,
// or annotate a class to indicate that it cannot be subclassed.
// Use like:
//   class B V8_FINAL : public A {};
//   virtual void bar() V8_FINAL;
#if V8_HAS_CXX11_FINAL
# define V8_FINAL final
#elif V8_HAS___FINAL
# define V8_FINAL __final
#elif V8_HAS_SEALED
# define V8_FINAL sealed
#else
# define V8_FINAL /* NOT SUPPORTED */
#endif


// This macro allows to specify memory alignment for structs, classes, etc.
// Use like:
//   class V8_ALIGNAS(16) MyClass { ... };
//   V8_ALIGNAS(32) int array[42];
#if V8_HAS_CXX11_ALIGNAS
# define V8_ALIGNAS(n) alignas(n)
#elif V8_HAS_ATTRIBUTE___ALIGNED__
# define V8_ALIGNAS(n) __attribute__((__aligned__(n)))
#elif V8_HAS_DECLSPEC_ALIGN
# define V8_ALIGNAS(n) __declspec(align(n))
#else
# define V8_ALIGNAS(n) /* NOT SUPPORTED */
#endif

#endif  // V8CONFIG_H_
