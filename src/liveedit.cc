// Copyright 2010 the V8 project authors. All rights reserved.
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

#include "liveedit.h"
#include "compiler.h"
#include "oprofile-agent.h"
#include "scopes.h"
#include "global-handles.h"
#include "debug.h"

namespace v8 {
namespace internal {


class FunctionInfoListener {
 public:
  void FunctionStarted(FunctionLiteral* fun) {
    // Implementation follows.
  }

  void FunctionDone() {
    // Implementation follows.
  }

  void FunctionScope(Scope* scope) {
    // Implementation follows.
  }

  void FunctionCode(Handle<Code> function_code) {
    // Implementation follows.
  }
};

static FunctionInfoListener* active_function_info_listener = NULL;

LiveEditFunctionTracker::LiveEditFunctionTracker(FunctionLiteral* fun) {
  if (active_function_info_listener != NULL) {
    active_function_info_listener->FunctionStarted(fun);
  }
}
LiveEditFunctionTracker::~LiveEditFunctionTracker() {
  if (active_function_info_listener != NULL) {
    active_function_info_listener->FunctionDone();
  }
}
void LiveEditFunctionTracker::RecordFunctionCode(Handle<Code> code) {
  if (active_function_info_listener != NULL) {
    active_function_info_listener->FunctionCode(code);
  }
}
void LiveEditFunctionTracker::RecordFunctionScope(Scope* scope) {
  if (active_function_info_listener != NULL) {
    active_function_info_listener->FunctionScope(scope);
  }
}
bool LiveEditFunctionTracker::IsActive() {
  return active_function_info_listener != NULL;
}

// Unwraps JSValue object, returning its field "value"
static Handle<Object> UnwrapJSValue(Handle<JSValue> jsValue) {
  return Handle<Object>(jsValue->value());
}

// Wraps any object into a OpaqueReference, that will hide the object
// from JavaScript.
static Handle<JSValue> WrapInJSValue(Object* object) {
  Handle<JSFunction> constructor = Top::opaque_reference_function();
  Handle<JSValue> result =
      Handle<JSValue>::cast(Factory::NewJSObject(constructor));
  result->set_value(object);
  return result;
}

// Simple helper class that creates more or less typed structures over
// JSArray object. This is an adhoc method of passing structures from C++
// to JavaScript.
template<typename S>
class JSArrayBasedStruct {
 public:
  static S Create() {
    Handle<JSArray> array = Factory::NewJSArray(S::kSize_);
    return S(array);
  }
  static S cast(Object* object) {
    JSArray* array = JSArray::cast(object);
    Handle<JSArray> array_handle(array);
    return S(array_handle);
  }
  explicit JSArrayBasedStruct(Handle<JSArray> array) : array_(array) {
  }
  Handle<JSArray> GetJSArray() {
    return array_;
  }
 protected:
  void SetField(int field_position, Handle<Object> value) {
    SetElement(array_, field_position, value);
  }
  void SetSmiValueField(int field_position, int value) {
    SetElement(array_, field_position, Handle<Smi>(Smi::FromInt(value)));
  }
  Object* GetField(int field_position) {
    return array_->GetElement(field_position);
  }
  int GetSmiValueField(int field_position) {
    Object* res = GetField(field_position);
    return Smi::cast(res)->value();
  }
 private:
  Handle<JSArray> array_;
};


// Represents some function compilation details. This structure will be used
// from JavaScript. It contains Code object, which is kept wrapped
// into a BlindReference for sanitizing reasons.
class FunctionInfoWrapper : public JSArrayBasedStruct<FunctionInfoWrapper> {
 public:
  explicit FunctionInfoWrapper(Handle<JSArray> array)
      : JSArrayBasedStruct<FunctionInfoWrapper>(array) {
  }
  void SetInitialProperties(Handle<String> name, int start_position,
                            int end_position, int param_num, int parent_index) {
    HandleScope scope;
    this->SetField(kFunctionNameOffset_, name);
    this->SetSmiValueField(kStartPositionOffset_, start_position);
    this->SetSmiValueField(kEndPositionOffset_, end_position);
    this->SetSmiValueField(kParamNumOffset_, param_num);
    this->SetSmiValueField(kParentIndexOffset_, parent_index);
  }
  void SetFunctionCode(Handle<Code> function_code) {
    Handle<JSValue> wrapper = WrapInJSValue(*function_code);
    this->SetField(kCodeOffset_, wrapper);
  }
  void SetScopeInfo(Handle<JSArray> scope_info_array) {
    this->SetField(kScopeInfoOffset_, scope_info_array);
  }
  int GetParentIndex() {
    return this->GetSmiValueField(kParentIndexOffset_);
  }
  Handle<Code> GetFunctionCode() {
    Handle<Object> raw_result = UnwrapJSValue(Handle<JSValue>(
        JSValue::cast(this->GetField(kCodeOffset_))));
    return Handle<Code>::cast(raw_result);
  }
  int GetStartPosition() {
    return this->GetSmiValueField(kStartPositionOffset_);
  }
  int GetEndPosition() {
    return this->GetSmiValueField(kEndPositionOffset_);
  }

 private:
  static const int kFunctionNameOffset_ = 0;
  static const int kStartPositionOffset_ = 1;
  static const int kEndPositionOffset_ = 2;
  static const int kParamNumOffset_ = 3;
  static const int kCodeOffset_ = 4;
  static const int kScopeInfoOffset_ = 5;
  static const int kParentIndexOffset_ = 6;
  static const int kSize_ = 7;
};

// Wraps SharedFunctionInfo along with some of its fields for passing it
// back to JavaScript. SharedFunctionInfo object itself is additionally
// wrapped into BlindReference for sanitizing reasons.
class SharedInfoWrapper : public JSArrayBasedStruct<SharedInfoWrapper> {
 public:
  explicit SharedInfoWrapper(Handle<JSArray> array)
      : JSArrayBasedStruct<SharedInfoWrapper>(array) {
  }

  void SetProperties(Handle<String> name, int start_position, int end_position,
                     Handle<SharedFunctionInfo> info) {
    HandleScope scope;
    this->SetField(kFunctionNameOffset_, name);
    Handle<JSValue> info_holder = WrapInJSValue(*info);
    this->SetField(kSharedInfoOffset_, info_holder);
    this->SetSmiValueField(kStartPositionOffset_, start_position);
    this->SetSmiValueField(kEndPositionOffset_, end_position);
  }
  Handle<SharedFunctionInfo> GetInfo() {
    Object* element = this->GetField(kSharedInfoOffset_);
    Handle<JSValue> value_wrapper(JSValue::cast(element));
    Handle<Object> raw_result = UnwrapJSValue(value_wrapper);
    return Handle<SharedFunctionInfo>::cast(raw_result);
  }

 private:
  static const int kFunctionNameOffset_ = 0;
  static const int kStartPositionOffset_ = 1;
  static const int kEndPositionOffset_ = 2;
  static const int kSharedInfoOffset_ = 3;
  static const int kSize_ = 4;
};



} }  // namespace v8::internal
