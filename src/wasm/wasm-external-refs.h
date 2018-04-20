// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_EXTERNAL_REFS_H_
#define V8_WASM_WASM_EXTERNAL_REFS_H_

#include <stdint.h>

#include "src/globals.h"

namespace v8 {
namespace internal {
namespace wasm {

void f32_trunc_wrapper(float* param);

void f32_floor_wrapper(float* param);

void f32_ceil_wrapper(float* param);

void f32_nearest_int_wrapper(float* param);

void f64_trunc_wrapper(double* param);

void f64_floor_wrapper(double* param);

void f64_ceil_wrapper(double* param);

void f64_nearest_int_wrapper(double* param);

void int64_to_float32_wrapper(Address data);

void uint64_to_float32_wrapper(Address data);

void int64_to_float64_wrapper(Address data);

void uint64_to_float64_wrapper(Address data);

int32_t float32_to_int64_wrapper(Address data);

int32_t float32_to_uint64_wrapper(Address data);

int32_t float64_to_int64_wrapper(Address data);

int32_t float64_to_uint64_wrapper(Address data);

int32_t int64_div_wrapper(int64_t* dst, int64_t* src);

int32_t int64_mod_wrapper(int64_t* dst, int64_t* src);

int32_t uint64_div_wrapper(uint64_t* dst, uint64_t* src);

int32_t uint64_mod_wrapper(uint64_t* dst, uint64_t* src);

uint32_t word32_ctz_wrapper(Address data);

uint32_t word64_ctz_wrapper(Address data);

uint32_t word32_popcnt_wrapper(Address data);

uint32_t word64_popcnt_wrapper(Address data);

uint32_t word32_rol_wrapper(Address data);

uint32_t word32_ror_wrapper(Address data);

void float64_pow_wrapper(double* param0, double* param1);

void set_thread_in_wasm_flag();
void clear_thread_in_wasm_flag();

typedef void (*WasmTrapCallbackForTesting)();

void set_trap_callback_for_testing(WasmTrapCallbackForTesting callback);

void call_trap_callback_for_testing();

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_EXTERNAL_REFS_H_
