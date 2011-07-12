// Copyright 2011 the V8 project authors. All rights reserved.
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

#include "v8.h"

#include "api.h"
#include "debug.h"
#include "execution.h"
#include "factory.h"
#include "macro-assembler.h"
#include "objects.h"
#include "global-handles.h"
#include "cctest.h"

using namespace v8::internal;

static Handle<ObjectDictionary> NewObjectDictionary(int at_least_space_for) {
  ASSERT(0 <= at_least_space_for);
  CALL_HEAP_FUNCTION(Isolate::Current(),
                     ObjectDictionary::Allocate(at_least_space_for),
                     ObjectDictionary);
}

TEST(ObjectDictionary) {
  v8::HandleScope scope;
  LocalContext context;
  Handle<ObjectDictionary> dict = NewObjectDictionary(23);
  Handle<JSObject> a = FACTORY->NewJSArray(7);
  Handle<JSObject> b = FACTORY->NewJSArray(11);
  MaybeObject* result = dict->AddChecked(*a, *b);
  CHECK(!result->IsFailure());
  CHECK_NE(dict->FindEntry(*a), ObjectDictionary::kNotFound);
  CHECK_EQ(dict->FindEntry(*b), ObjectDictionary::kNotFound);

  // Keys still have to be valid after objects were moved.
  HEAP->CollectGarbage(NEW_SPACE);
  CHECK_NE(dict->FindEntry(*a), ObjectDictionary::kNotFound);
  CHECK_EQ(dict->FindEntry(*b), ObjectDictionary::kNotFound);
}
