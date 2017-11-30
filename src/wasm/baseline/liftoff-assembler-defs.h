// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_DEFS_H_
#define V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_DEFS_H_

#include "src/reglist.h"

#if V8_TARGET_ARCH_IA32

#include "src/ia32/assembler-ia32.h"

namespace v8 {
namespace internal {
namespace wasm {

static constexpr bool kLiftoffAssemblerImplementedOnThisPlatform = true;

static constexpr RegList kLiftoffAssemblerGpCacheRegs =
    Register::ListOf<eax, ecx, edx, ebx, esi, edi>();

// TODO(clemensh): Fix this once we support float operations.
static constexpr RegList kLiftoffAssemblerFpCacheRegs = 0xff;

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#elif V8_TARGET_ARCH_X64

#include "src/x64/assembler-x64.h"

namespace v8 {
namespace internal {
namespace wasm {

static constexpr bool kLiftoffAssemblerImplementedOnThisPlatform = true;

static constexpr RegList kLiftoffAssemblerGpCacheRegs =
    Register::ListOf<rax, rcx, rdx, rbx, rsi, rdi>();

// TODO(clemensh): Fix this once we support float operations.
static constexpr RegList kLiftoffAssemblerFpCacheRegs = 0xff;

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#else

namespace v8 {
namespace internal {
namespace wasm {

static constexpr bool kLiftoffAssemblerImplementedOnThisPlatform = false;

static constexpr RegList kLiftoffAssemblerGpCacheRegs = 0xff;

static constexpr RegList kLiftoffAssemblerFpCacheRegs = 0xff;

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif

#endif  // V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_DEFS_H_
