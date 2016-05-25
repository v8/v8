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
