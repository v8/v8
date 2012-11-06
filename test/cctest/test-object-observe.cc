// Copyright 2012 the V8 project authors. All rights reserved.
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

#include "cctest.h"

using namespace v8;

namespace {
// Need to create a new isolate when FLAG_harmony_observation is on.
class HarmonyIsolate {
 public:
  HarmonyIsolate() {
    i::FLAG_harmony_observation = true;
    isolate_ = Isolate::New();
    isolate_->Enter();
  }

  ~HarmonyIsolate() {
    isolate_->Exit();
    isolate_->Dispose();
  }

 private:
  Isolate* isolate_;
};
}

TEST(PerIsolateState) {
  HarmonyIsolate isolate;
  HandleScope scope;
  LocalContext context1;
  CompileRun(
      "var count = 0;"
      "var calls = 0;"
      "var observer = function(records) { count = records.length; calls++ };"
      "var obj = {};"
      "Object.observe(obj, observer);"
      "Object.notify(obj, {type: 'a'});");
  Handle<Value> observer = CompileRun("observer");
  Handle<Value> obj = CompileRun("obj");
  {
    LocalContext context2;
    context2->Global()->Set(String::New("obj"), obj);
    CompileRun("Object.notify(obj, {type: 'b'});");
  }
  {
    LocalContext context3;
    context3->Global()->Set(String::New("obj"), obj);
    CompileRun("Object.notify(obj, {type: 'c'});");
  }
  {
    LocalContext context4;
    context4->Global()->Set(String::New("observer"), observer);
    CompileRun("Object.deliverChangeRecords(observer)");
  }
  CHECK_EQ(1, CompileRun("calls")->Int32Value());
  CHECK_EQ(3, CompileRun("count")->Int32Value());
}
