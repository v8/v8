// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/fuzzer/wasm-section-fuzzers.h"

#include "include/v8.h"
#include "src/isolate.h"
#include "src/wasm/encoder.h"
#include "src/wasm/wasm-module.h"
#include "src/zone.h"
#include "test/common/wasm/wasm-module-runner.h"
#include "test/fuzzer/fuzzer-support.h"

using namespace v8::internal::wasm;

int fuzz_wasm_section(WasmSection::Code section, const uint8_t* data,
                      size_t size) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();
  v8::internal::Isolate* i_isolate =
      reinterpret_cast<v8::internal::Isolate*>(isolate);

  // Clear any pending exceptions from a prior run.
  if (i_isolate->has_pending_exception()) {
    i_isolate->clear_pending_exception();
  }

  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());
  v8::TryCatch try_catch(isolate);

  v8::base::AccountingAllocator allocator;
  v8::internal::Zone zone(&allocator);

  ZoneBuffer buffer(&zone);
  buffer.write_u32(kWasmMagic);
  buffer.write_u32(kWasmVersion);
  const char* name = WasmSection::getName(section);
  size_t length = WasmSection::getNameLength(section);
  buffer.write_size(length);  // Section name string size.
  buffer.write(reinterpret_cast<const uint8_t*>(name), length);
  buffer.write_u32v(static_cast<uint32_t>(size));
  buffer.write(data, size);

  ErrorThrower thrower(i_isolate, "decoder");

  std::unique_ptr<const WasmModule> module(testing::DecodeWasmModuleForTesting(
      i_isolate, &zone, thrower, buffer.begin(), buffer.end(), kWasmOrigin));

  return 0;
}
