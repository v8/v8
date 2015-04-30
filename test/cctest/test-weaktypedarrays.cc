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

#include <stdlib.h>

#include "src/v8.h"
#include "test/cctest/cctest.h"

#include "src/api.h"
#include "src/heap/heap.h"
#include "src/objects.h"

using namespace v8::internal;

static Isolate* GetIsolateFrom(LocalContext* context) {
  return reinterpret_cast<Isolate*>((*context)->GetIsolate());
}


static int CountArrayBuffersInWeakList(Heap* heap) {
  int count = 0;
  for (Object* o = heap->array_buffers_list();
       !o->IsUndefined();
       o = JSArrayBuffer::cast(o)->weak_next()) {
    count++;
  }
  return count;
}


static bool HasArrayBufferInWeakList(Heap* heap, JSArrayBuffer* ab) {
  for (Object* o = heap->array_buffers_list();
       !o->IsUndefined();
       o = JSArrayBuffer::cast(o)->weak_next()) {
    if (ab == o) return true;
  }
  return false;
}


TEST(WeakArrayBuffersFromScript) {
  v8::V8::Initialize();
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  int start = CountArrayBuffersInWeakList(isolate->heap());

  for (int i = 1; i <= 3; i++) {
    // Create 3 array buffers, make i-th of them garbage,
    // validate correct state of array buffer weak list.
    CHECK_EQ(start, CountArrayBuffersInWeakList(isolate->heap()));
    {
      v8::HandleScope scope(context->GetIsolate());

      {
        v8::HandleScope s1(context->GetIsolate());
        CompileRun("var ab1 = new ArrayBuffer(256);"
                   "var ab2 = new ArrayBuffer(256);"
                   "var ab3 = new ArrayBuffer(256);");
        v8::Handle<v8::ArrayBuffer> ab1 =
            v8::Handle<v8::ArrayBuffer>::Cast(CompileRun("ab1"));
        v8::Handle<v8::ArrayBuffer> ab2 =
            v8::Handle<v8::ArrayBuffer>::Cast(CompileRun("ab2"));
        v8::Handle<v8::ArrayBuffer> ab3 =
            v8::Handle<v8::ArrayBuffer>::Cast(CompileRun("ab3"));

        CHECK_EQ(3, CountArrayBuffersInWeakList(isolate->heap()) - start);
        CHECK(HasArrayBufferInWeakList(isolate->heap(),
              *v8::Utils::OpenHandle(*ab1)));
        CHECK(HasArrayBufferInWeakList(isolate->heap(),
              *v8::Utils::OpenHandle(*ab2)));
        CHECK(HasArrayBufferInWeakList(isolate->heap(),
              *v8::Utils::OpenHandle(*ab3)));
      }

      i::ScopedVector<char> source(1024);
      i::SNPrintF(source, "ab%d = null;", i);
      CompileRun(source.start());
      isolate->heap()->CollectAllGarbage();

      CHECK_EQ(2, CountArrayBuffersInWeakList(isolate->heap()) - start);

      {
        v8::HandleScope s2(context->GetIsolate());
        for (int j = 1; j <= 3; j++) {
          if (j == i) continue;
          i::SNPrintF(source, "ab%d", j);
          v8::Handle<v8::ArrayBuffer> ab =
              v8::Handle<v8::ArrayBuffer>::Cast(CompileRun(source.start()));
          CHECK(HasArrayBufferInWeakList(isolate->heap(),
                *v8::Utils::OpenHandle(*ab)));
          }
      }

      CompileRun("ab1 = null; ab2 = null; ab3 = null;");
    }

    isolate->heap()->CollectAllGarbage();
    CHECK_EQ(start, CountArrayBuffersInWeakList(isolate->heap()));
  }
}
