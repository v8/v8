// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "include/v8.h"
#include "src/factory.h"
#include "src/objects-inl.h"
#include "src/objects.h"
#include "src/regexp/jsregexp.h"
#include "test/fuzzer/fuzzer-support.h"

namespace i = v8::internal;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());
  v8::TryCatch try_catch(isolate);

  i::FLAG_harmony_unicode_regexps = true;
  i::FLAG_harmony_regexp_lookbehind = true;

  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::Factory* factory = i_isolate->factory();

  if (size > INT_MAX) return 0;
  i::MaybeHandle<i::String> maybe_source = factory->NewStringFromOneByte(
      i::Vector<const uint8_t>(data, static_cast<int>(size)));
  i::Handle<i::String> source;
  if (!maybe_source.ToHandle(&source)) return 0;

  static const int kAllFlags = i::JSRegExp::kGlobal | i::JSRegExp::kIgnoreCase |
                               i::JSRegExp::kMultiline | i::JSRegExp::kSticky |
                               i::JSRegExp::kUnicode;

  const uint8_t one_byte_array[6] = {'f', 'o', 'o', 'b', 'a', 'r'};
  const i::uc16 two_byte_array[6] = {'f', 0xD83D, 0xDCA9, 'b', 'a', 0x2603};

  i::Handle<i::JSArray> results_array = factory->NewJSArray(4);
  i::Handle<i::String> one_byte =
      factory->NewStringFromOneByte(i::Vector<const uint8_t>(one_byte_array, 6))
          .ToHandleChecked();
  i::Handle<i::String> two_byte =
      factory->NewStringFromTwoByte(i::Vector<const i::uc16>(two_byte_array, 6))
          .ToHandleChecked();

  for (int flags = 0; flags <= kAllFlags; flags++) {
    v8::TryCatch try_catch(isolate);
    i::MaybeHandle<i::JSRegExp> maybe_regexp =
        i::JSRegExp::New(source, static_cast<i::JSRegExp::Flags>(flags));
    i::Handle<i::JSRegExp> regexp;
    if (!maybe_regexp.ToHandle(&regexp)) continue;
    USE(i::RegExpImpl::Exec(regexp, one_byte, 0, results_array).is_null() &&
        i::RegExpImpl::Exec(regexp, two_byte, 0, results_array).is_null());
  }

  return 0;
}
