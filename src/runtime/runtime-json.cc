// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/char-predicates-inl.h"
#include "src/isolate-inl.h"
#include "src/json-parser.h"
#include "src/json-stringifier.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_QuoteJSONString) {
  HandleScope scope(isolate);
  CONVERT_ARG_HANDLE_CHECKED(String, string, 0);
  DCHECK(args.length() == 1);
  RETURN_RESULT_OR_FAILURE(
      isolate, BasicJsonStringifier::StringifyString(isolate, string));
}

RUNTIME_FUNCTION(Runtime_BasicJSONStringify) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);
  CONVERT_ARG_HANDLE_CHECKED(Object, object, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, replacer, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, gap, 2);
  RETURN_RESULT_OR_FAILURE(
      isolate, BasicJsonStringifier(isolate).Stringify(object, replacer, gap));
}

RUNTIME_FUNCTION(Runtime_ParseJson) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, object, 0);
  Handle<String> source;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, source,
                                     Object::ToString(isolate, object));
  source = String::Flatten(source);
  // Optimized fast case where we only have Latin1 characters.
  RETURN_RESULT_OR_FAILURE(isolate, source->IsSeqOneByteString()
                                        ? JsonParser<true>::Parse(source)
                                        : JsonParser<false>::Parse(source));
}

}  // namespace internal
}  // namespace v8
