// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/web-snapshot/web-snapshot.h"
#include "test/cctest/cctest-utils.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

TEST(Minimal) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::Context> context = isolate->GetCurrentContext();

  CompileRun("var foo = {'key': 'lol'}");
  WebSnapshotData snapshot_data;
  {
    std::vector<std::string> exports;
    exports.push_back("foo");
    WebSnapshotSerializer serializer(isolate);
    CHECK(serializer.TakeSnapshot(context, exports, snapshot_data));
    CHECK(!serializer.has_error());
    CHECK_NOT_NULL(snapshot_data.buffer);
    // Strings: 'foo', 'key', 'lol'
    CHECK_EQ(3, serializer.string_count());
    CHECK_EQ(1, serializer.map_count());
    CHECK_EQ(1, serializer.object_count());
    CHECK_EQ(0, serializer.function_count());
  }

  {
    v8::Local<v8::Context> new_context = CcTest::NewContext();
    v8::Context::Scope context_scope(new_context);
    WebSnapshotDeserializer deserializer(isolate);
    CHECK(deserializer.UseWebSnapshot(snapshot_data.buffer,
                                      snapshot_data.buffer_size));
    CHECK(!deserializer.has_error());
    v8::Local<v8::String> result = CompileRun("foo.key").As<v8::String>();
    CHECK(result->Equals(new_context, v8_str("lol")).FromJust());
    CHECK_EQ(3, deserializer.string_count());
    CHECK_EQ(1, deserializer.map_count());
    CHECK_EQ(1, deserializer.object_count());
    CHECK_EQ(0, deserializer.function_count());
  }
}

TEST(Function) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::Context> context = isolate->GetCurrentContext();

  CompileRun("var foo = {'key': function() { return '11525'; }}");
  WebSnapshotData snapshot_data;
  {
    std::vector<std::string> exports;
    exports.push_back("foo");
    WebSnapshotSerializer serializer(isolate);
    CHECK(serializer.TakeSnapshot(context, exports, snapshot_data));
    CHECK(!serializer.has_error());
    CHECK_NOT_NULL(snapshot_data.buffer);
    // Strings: 'foo', 'key', function source code
    CHECK_EQ(3, serializer.string_count());
    CHECK_EQ(1, serializer.map_count());
    CHECK_EQ(1, serializer.object_count());
    CHECK_EQ(1, serializer.function_count());
  }

  {
    v8::Local<v8::Context> new_context = CcTest::NewContext();
    v8::Context::Scope context_scope(new_context);
    WebSnapshotDeserializer deserializer(isolate);
    CHECK(deserializer.UseWebSnapshot(snapshot_data.buffer,
                                      snapshot_data.buffer_size));
    CHECK(!deserializer.has_error());
    v8::Local<v8::Function> function = CompileRun("foo.key").As<v8::Function>();
    v8::Local<v8::Value> result =
        function->Call(new_context, new_context->Global(), 0, nullptr)
            .ToLocalChecked();
    CHECK(result->Equals(new_context, v8_str("11525")).FromJust());
    CHECK_EQ(3, deserializer.string_count());
    CHECK_EQ(1, deserializer.map_count());
    CHECK_EQ(1, deserializer.object_count());
    CHECK_EQ(1, deserializer.function_count());
  }
}

}  // namespace internal
}  // namespace v8
