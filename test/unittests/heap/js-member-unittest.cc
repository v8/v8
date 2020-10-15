// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-cppgc.h"
#include "src/heap/cppgc/visitor.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using JSMemberTest = TestWithIsolate;

TEST_F(JSMemberTest, ResetFromLocal) {
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  v8::JSMember<v8::Object> member;
  {
    v8::HandleScope handles(v8_isolate());
    v8::Local<v8::Object> local =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    EXPECT_TRUE(member.IsEmpty());
    EXPECT_NE(member, local);
    member.Set(v8_isolate(), local);
    EXPECT_FALSE(member.IsEmpty());
    EXPECT_EQ(member, local);
  }
}

TEST_F(JSMemberTest, ConstructFromLocal) {
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  {
    v8::HandleScope handles(v8_isolate());
    v8::Local<v8::Object> local =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> member(v8_isolate(), local);
    EXPECT_FALSE(member.IsEmpty());
    EXPECT_EQ(member, local);
  }
}

TEST_F(JSMemberTest, Reset) {
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  {
    v8::HandleScope handles(v8_isolate());
    v8::Local<v8::Object> local =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> member(v8_isolate(), local);
    EXPECT_FALSE(member.IsEmpty());
    EXPECT_EQ(member, local);
    member.Reset();
    EXPECT_TRUE(member.IsEmpty());
    EXPECT_NE(member, local);
  }
}

TEST_F(JSMemberTest, Copy) {
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  {
    v8::HandleScope handles(v8_isolate());
    v8::Local<v8::Object> local =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> member(v8_isolate(), local);
    v8::JSMember<v8::Object> member_copy1(member);
    v8::JSMember<v8::Object> member_copy2 = member;
    EXPECT_EQ(member, local);
    EXPECT_EQ(member_copy1, local);
    EXPECT_EQ(member_copy2, local);
  }
}

TEST_F(JSMemberTest, CopyHeterogenous) {
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  {
    v8::HandleScope handles(v8_isolate());
    v8::Local<v8::Object> local =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> member(v8_isolate(), local);
    v8::JSMember<v8::Value> member_copy1(member);
    v8::JSMember<v8::Value> member_copy2 = member;
    EXPECT_EQ(member, local);
    EXPECT_EQ(member_copy1, local);
    EXPECT_EQ(member_copy2, local);
  }
}

TEST_F(JSMemberTest, Move) {
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  {
    v8::HandleScope handles(v8_isolate());
    v8::Local<v8::Object> local =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> member(v8_isolate(), local);
    v8::JSMember<v8::Object> member_moved1(std::move(member));
    v8::JSMember<v8::Object> member_moved2 = std::move(member_moved1);
    EXPECT_TRUE(member.IsEmpty());
    EXPECT_TRUE(member_moved1.IsEmpty());
    EXPECT_EQ(member_moved2, local);
  }
}

TEST_F(JSMemberTest, MoveHeterogenous) {
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  {
    v8::HandleScope handles(v8_isolate());
    v8::Local<v8::Object> local =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> member1(v8_isolate(), local);
    v8::JSMember<v8::Value> member_moved1(std::move(member1));
    v8::JSMember<v8::Object> member2(v8_isolate(), local);
    v8::JSMember<v8::Object> member_moved2 = std::move(member2);
    EXPECT_TRUE(member1.IsEmpty());
    EXPECT_EQ(member_moved1, local);
    EXPECT_TRUE(member2.IsEmpty());
    EXPECT_EQ(member_moved2, local);
  }
}

TEST_F(JSMemberTest, Equality) {
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  {
    v8::HandleScope handles(v8_isolate());
    v8::Local<v8::Object> local1 =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> member1(v8_isolate(), local1);
    v8::JSMember<v8::Object> member2(v8_isolate(), local1);
    EXPECT_EQ(member1, member2);
    EXPECT_EQ(member2, member1);
    v8::Local<v8::Object> local2 =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> member3(v8_isolate(), local2);
    EXPECT_NE(member2, member3);
    EXPECT_NE(member3, member2);
  }
}

TEST_F(JSMemberTest, EqualityHeterogenous) {
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  {
    v8::HandleScope handles(v8_isolate());
    v8::Local<v8::Object> local1 =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> member1(v8_isolate(), local1);
    v8::JSMember<v8::Value> member2(v8_isolate(), local1);
    EXPECT_EQ(member1, member2);
    EXPECT_EQ(member2, member1);
    v8::Local<v8::Object> local2 =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> member3(v8_isolate(), local2);
    EXPECT_NE(member2, member3);
    EXPECT_NE(member3, member2);
  }
}

namespace {

// Must be used on stack.
class JSVisitorForTesting final : public JSVisitor {
 public:
  explicit JSVisitorForTesting(v8::Local<v8::Object> expected_object)
      : JSVisitor(cppgc::internal::VisitorFactory::CreateKey()),
        expected_object_(expected_object) {}

  void Visit(const internal::JSMemberBase& ref) final {
    EXPECT_EQ(ref, expected_object_);
    visit_count_++;
  }

  size_t visit_count() const { return visit_count_; }

 private:
  v8::Local<v8::Object> expected_object_;
  size_t visit_count_ = 0;
};

}  // namespace

TEST_F(JSMemberTest, JSMemberTrace) {
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate());
  v8::Context::Scope context_scope(context);
  {
    v8::HandleScope handles(v8_isolate());
    v8::Local<v8::Object> local =
        v8::Local<v8::Object>::New(v8_isolate(), v8::Object::New(v8_isolate()));
    v8::JSMember<v8::Object> js_member(v8_isolate(), local);
    JSVisitorForTesting visitor(local);
    // Cast to cppgc::Visitor to ensure that we dispatch through the base
    // visitor and use traits.
    static_cast<cppgc::Visitor&>(visitor).Trace(js_member);
    EXPECT_EQ(1u, visitor.visit_count());
  }
}

}  // namespace internal
}  // namespace v8
