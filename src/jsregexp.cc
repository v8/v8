// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#include "execution.h"
#include "factory.h"
#include "jsregexp.h"
#include "third_party/jscre/pcre.h"
#include "platform.h"
#include "runtime.h"
#include "top.h"

namespace v8 { namespace internal {


#define CAPTURE_INDEX 0
#define INTERNAL_INDEX 1

static Failure* malloc_failure;

static void* JSREMalloc(size_t size) {
  Object* obj = Heap::AllocateByteArray(size);

  // If allocation failed, return a NULL pointer to JSRE, and jsRegExpCompile
  // will return NULL to the caller, performs GC there.
  // Also pass failure information to the caller.
  if (obj->IsFailure()) {
    malloc_failure = Failure::cast(obj);
    return NULL;
  }

  // Note: object is unrooted, the caller of jsRegExpCompile must
  // create a handle for the return value before doing heap allocation.
  return reinterpret_cast<void*>(ByteArray::cast(obj)->GetDataStartAddress());
}


static void JSREFree(void* p) {
  USE(p);  // Do nothing, memory is garbage collected.
}


String* RegExpImpl::last_ascii_string_ = NULL;
String* RegExpImpl::two_byte_cached_string_ = NULL;


void RegExpImpl::NewSpaceCollectionPrologue() {
  // The two byte string is always in the old space.  The Ascii string may be
  // in either place.  If it is in the old space we don't need to do anything.
  if (Heap::InNewSpace(last_ascii_string_)) {
    // Invalidate the cache.
    last_ascii_string_ = NULL;
    two_byte_cached_string_ = NULL;
  }
}


void RegExpImpl::OldSpaceCollectionPrologue() {
  last_ascii_string_ = NULL;
  two_byte_cached_string_ = NULL;
}


Handle<Object> RegExpImpl::CreateRegExpLiteral(Handle<JSFunction> constructor,
                                               Handle<String> pattern,
                                               Handle<String> flags,
                                               bool* has_pending_exception) {
  // Ensure that the constructor function has been loaded.
  if (!constructor->IsLoaded()) {
    LoadLazy(constructor, has_pending_exception);
    if (*has_pending_exception) return Handle<Object>(Failure::Exception());
  }
  // Call the construct code with 2 arguments.
  Object** argv[2] = { Handle<Object>::cast(pattern).location(),
                       Handle<Object>::cast(flags).location() };
  return Execution::New(constructor, 2, argv, has_pending_exception);
}


// Converts a source string to a 16 bit flat string or a SlicedString containing
// a 16 bit flat string).
Handle<String> RegExpImpl::CachedStringToTwoByte(Handle<String> subject) {
  if (*subject == last_ascii_string_) {
    ASSERT(two_byte_cached_string_ != NULL);
    return Handle<String>(String::cast(two_byte_cached_string_));
  }
  Handle<String> two_byte_string = StringToTwoByte(subject);
  last_ascii_string_ = *subject;
  two_byte_cached_string_ = *two_byte_string;
  return two_byte_string;
}


// Converts a source string to a 16 bit flat string or a SlicedString containing
// a 16 bit flat string).
Handle<String> RegExpImpl::StringToTwoByte(Handle<String> pattern) {
  if (!pattern->IsFlat()) {
    FlattenString(pattern);
  }
  Handle<String> flat_string(pattern->IsConsString() ?
    String::cast(ConsString::cast(*pattern)->first()) :
    *pattern);
  ASSERT(!flat_string->IsConsString());
  ASSERT(flat_string->IsSeqString() || flat_string->IsSlicedString() ||
         flat_string->IsExternalString());
  if (!flat_string->IsAscii()) {
    return flat_string;
  }

  Handle<String> two_byte_string =
    Factory::NewRawTwoByteString(flat_string->length(), TENURED);
  static StringInputBuffer convert_to_two_byte_buffer;
  convert_to_two_byte_buffer.Reset(*flat_string);
  for (int i = 0; convert_to_two_byte_buffer.has_more(); i++) {
    two_byte_string->Set(i, convert_to_two_byte_buffer.GetNext());
  }
  return two_byte_string;
}


unibrow::Predicate<unibrow::RegExpSpecialChar, 128> is_reg_exp_special_char;


Handle<Object> RegExpImpl::Compile(Handle<JSRegExp> re,
                                   Handle<String> pattern,
                                   Handle<String> flags) {
  bool is_atom = true;
  for (int i = 0; is_atom && i < flags->length(); i++) {
    if (flags->Get(i) == 'i')
      is_atom = false;
  }
  for (int i = 0; is_atom && i < pattern->length(); i++) {
    if (is_reg_exp_special_char.get(pattern->Get(i)))
      is_atom = false;
  }
  Handle<Object> result;
  if (is_atom) {
    result = AtomCompile(re, pattern);
  } else {
    result = JsreCompile(re, pattern, flags);
  }

  LOG(RegExpCompileEvent(re));
  return result;
}


Handle<Object> RegExpImpl::Exec(Handle<JSRegExp> regexp,
                                Handle<String> subject,
                                Handle<Object> index) {
  switch (regexp->type_tag()) {
    case JSRegExp::JSCRE:
      return JsreExec(regexp, subject, index);
    case JSRegExp::ATOM:
      return AtomExec(regexp, subject, index);
    default:
      UNREACHABLE();
      return Handle<Object>();
  }
}


Handle<Object> RegExpImpl::ExecGlobal(Handle<JSRegExp> regexp,
                                Handle<String> subject) {
  switch (regexp->type_tag()) {
    case JSRegExp::JSCRE:
      return JsreExecGlobal(regexp, subject);
    case JSRegExp::ATOM:
      return AtomExecGlobal(regexp, subject);
    default:
      UNREACHABLE();
      return Handle<Object>();
  }
}


Handle<Object> RegExpImpl::AtomCompile(Handle<JSRegExp> re,
                                       Handle<String> pattern) {
  re->set_type_tag(JSRegExp::ATOM);
  re->set_data(*pattern);
  return re;
}


Handle<Object> RegExpImpl::AtomExec(Handle<JSRegExp> re,
                                    Handle<String> subject,
                                    Handle<Object> index) {
  Handle<String> needle(String::cast(re->data()));

  uint32_t start_index;
  if (!Array::IndexFromObject(*index, &start_index)) {
    return Handle<Smi>(Smi::FromInt(-1));
  }

  LOG(RegExpExecEvent(re, start_index, subject));
  int value = Runtime::StringMatchKmp(subject, needle, start_index);
  if (value == -1) return Factory::null_value();
  Handle<JSArray> result = Factory::NewJSArray(2);
  SetElement(result, 0, Handle<Smi>(Smi::FromInt(value)));
  SetElement(result, 1, Handle<Smi>(Smi::FromInt(value + needle->length())));
  return result;
}


Handle<Object> RegExpImpl::AtomExecGlobal(Handle<JSRegExp> re,
                                          Handle<String> subject) {
  Handle<String> needle(String::cast(re->data()));
  Handle<JSArray> result = Factory::NewJSArray(1);
  int index = 0;
  int match_count = 0;
  int subject_length = subject->length();
  int needle_length = needle->length();
  while (true) {
    LOG(RegExpExecEvent(re, index, subject));
    int value = -1;
    if (index + needle_length <= subject_length) {
      value = Runtime::StringMatchKmp(subject, needle, index);
    }
    if (value == -1) break;
    HandleScope scope;
    int end = value + needle_length;
    Handle<JSArray> pair = Factory::NewJSArray(2);
    SetElement(pair, 0, Handle<Smi>(Smi::FromInt(value)));
    SetElement(pair, 1, Handle<Smi>(Smi::FromInt(end)));
    SetElement(result, match_count, pair);
    match_count++;
    index = end;
    if (needle_length == 0)
      index++;
  }
  return result;
}


Handle<Object> RegExpImpl::JsreCompile(Handle<JSRegExp> re,
                                       Handle<String> pattern,
                                       Handle<String> flags) {
  JSRegExpIgnoreCaseOption case_option = JSRegExpDoNotIgnoreCase;
  JSRegExpMultilineOption multiline_option = JSRegExpSingleLine;
  FlattenString(flags);
  for (int i = 0; i < flags->length(); i++) {
    if (flags->Get(i) == 'i') case_option = JSRegExpIgnoreCase;
    if (flags->Get(i) == 'm') multiline_option = JSRegExpMultiline;
  }

  Handle<String> two_byte_pattern = StringToTwoByte(pattern);

  unsigned number_of_captures;
  const char* error_message = NULL;

  malloc_failure = Failure::Exception();
  JscreRegExp* code = jsRegExpCompile(two_byte_pattern->GetTwoByteData(),
                                      pattern->length(), case_option,
                                      multiline_option, &number_of_captures,
                                      &error_message, &JSREMalloc, &JSREFree);

  if (code == NULL && malloc_failure->IsRetryAfterGC()) {
    // Performs a GC, then retries.
    if (!Heap::CollectGarbage(malloc_failure->requested(),
                              malloc_failure->allocation_space())) {
      // TODO(1181417): Fix this.
      V8::FatalProcessOutOfMemory("RegExpImpl::JsreCompile");
    }
    malloc_failure = Failure::Exception();
    code = jsRegExpCompile(two_byte_pattern->GetTwoByteData(),
                           pattern->length(), case_option,
                           multiline_option, &number_of_captures,
                           &error_message, &JSREMalloc, &JSREFree);
    if (code == NULL && malloc_failure->IsRetryAfterGC()) {
      // TODO(1181417): Fix this.
      V8::FatalProcessOutOfMemory("RegExpImpl::JsreCompile");
    }
  }

  if (error_message != NULL) {
    // Throw an exception.
    SmartPointer<char> char_pattern =
        two_byte_pattern->ToCString(DISALLOW_NULLS);
    Handle<JSArray> array = Factory::NewJSArray(2);
    SetElement(array, 0, Factory::NewStringFromUtf8(CStrVector(*char_pattern)));
    SetElement(array, 1, Factory::NewStringFromUtf8(CStrVector(error_message)));
    Handle<Object> regexp_err =
        Factory::NewSyntaxError("malformed_regexp", array);
    return Handle<Object>(Top::Throw(*regexp_err));
  }

  ASSERT(code != NULL);

  // Convert the return address to a ByteArray pointer.
  Handle<ByteArray> internal(
      ByteArray::FromDataStartAddress(reinterpret_cast<Address>(code)));

  Handle<FixedArray> value = Factory::NewFixedArray(2);
  value->set(CAPTURE_INDEX, Smi::FromInt(number_of_captures));
  value->set(INTERNAL_INDEX, *internal);
  re->set_type_tag(JSRegExp::JSCRE);
  re->set_data(*value);

  return re;
}


Handle<Object> RegExpImpl::JsreExecOnce(Handle<JSRegExp> regexp,
                                        int num_captures,
                                        Handle<String> subject,
                                        int previous_index,
                                        const uc16* two_byte_subject,
                                        int* offsets_vector,
                                        int offsets_vector_length) {
  int rc;
  {
    AssertNoAllocation a;
    ByteArray* internal = JsreInternal(regexp);
    const JscreRegExp* js_regexp =
        reinterpret_cast<JscreRegExp*>(internal->GetDataStartAddress());

    LOG(RegExpExecEvent(regexp, previous_index, subject));

    rc = jsRegExpExecute(js_regexp,
                         two_byte_subject,
                         subject->length(),
                         previous_index,
                         offsets_vector,
                         offsets_vector_length);
  }

  // The KJS JavaScript engine returns null (ie, a failed match) when
  // JSRE's internal match limit is exceeded.  We duplicate that behavior here.
  if (rc == JSRegExpErrorNoMatch
      || rc == JSRegExpErrorHitLimit) {
    return Factory::null_value();
  }

  // Other JSRE errors:
  if (rc < 0) {
    // Throw an exception.
    Handle<Object> code(Smi::FromInt(rc));
    Handle<Object> args[2] = { Factory::LookupAsciiSymbol("jsre_exec"), code };
    Handle<Object> regexp_err(
        Factory::NewTypeError("jsre_error", HandleVector(args, 2)));
    return Handle<Object>(Top::Throw(*regexp_err));
  }

  Handle<JSArray> result = Factory::NewJSArray(2 * (num_captures+1));

  // The captures come in (start, end+1) pairs.
  for (int i = 0; i < 2 * (num_captures+1); i += 2) {
    SetElement(result, i, Handle<Object>(Smi::FromInt(offsets_vector[i])));
    SetElement(result, i+1, Handle<Object>(Smi::FromInt(offsets_vector[i+1])));
  }
  return result;
}


class OffsetsVector {
 public:
  inline OffsetsVector(int num_captures) {
    offsets_vector_length_ = (num_captures + 1) * 3;
    if (offsets_vector_length_ > kStaticOffsetsVectorSize) {
      vector_ = NewArray<int>(offsets_vector_length_);
    } else {
      vector_ = static_offsets_vector_;
    }
  }


  inline ~OffsetsVector() {
    if (offsets_vector_length_ > kStaticOffsetsVectorSize) {
      DeleteArray(vector_);
      vector_ = NULL;
    }
  }


  inline int* vector() {
    return vector_;
  }


  inline int length() {
    return offsets_vector_length_;
  }

 private:
  int* vector_;
  int offsets_vector_length_;
  static const int kStaticOffsetsVectorSize = 30;
  static int static_offsets_vector_[kStaticOffsetsVectorSize];
};


int OffsetsVector::static_offsets_vector_[
    OffsetsVector::kStaticOffsetsVectorSize];


Handle<Object> RegExpImpl::JsreExec(Handle<JSRegExp> regexp,
                                    Handle<String> subject,
                                    Handle<Object> index) {
  // Prepare space for the return values.
  int num_captures = JsreCapture(regexp);

  OffsetsVector offsets(num_captures);

  int previous_index = static_cast<int>(DoubleToInteger(index->Number()));

  Handle<String> subject16 = CachedStringToTwoByte(subject);

  Handle<Object> result(JsreExecOnce(regexp, num_captures, subject,
                                     previous_index,
                                     subject16->GetTwoByteData(),
                                     offsets.vector(), offsets.length()));

  return result;
}


Handle<Object> RegExpImpl::JsreExecGlobal(Handle<JSRegExp> regexp,
                                          Handle<String> subject) {
  // Prepare space for the return values.
  int num_captures = JsreCapture(regexp);

  OffsetsVector offsets(num_captures);

  int previous_index = 0;

  Handle<JSArray> result =  Factory::NewJSArray(0);
  int i = 0;
  Handle<Object> matches;

  Handle<String> subject16 = CachedStringToTwoByte(subject);

  do {
    if (previous_index > subject->length() || previous_index < 0) {
      // Per ECMA-262 15.10.6.2, if the previous index is greater than the
      // string length, there is no match.
      matches = Factory::null_value();
    } else {
      matches = JsreExecOnce(regexp, num_captures, subject, previous_index,
                             subject16->GetTwoByteData(),
                             offsets.vector(), offsets.length());

      if (matches->IsJSArray()) {
        SetElement(result, i, matches);
        i++;
        previous_index = offsets.vector()[1];
        if (offsets.vector()[0] == offsets.vector()[1]) {
          previous_index++;
        }
      }
    }
  } while (matches->IsJSArray());

  // If we exited the loop with an exception, throw it.
  if (matches->IsNull()) {  // Exited loop normally.
    return result;
  } else {  // Exited loop with the exception in matches.
    return matches;
  }
}


int RegExpImpl::JsreCapture(Handle<JSRegExp> re) {
  Object* value = re->data();
  ASSERT(value->IsFixedArray());
  return Smi::cast(FixedArray::cast(value)->get(CAPTURE_INDEX))->value();
}


ByteArray* RegExpImpl::JsreInternal(Handle<JSRegExp> re) {
  Object* value = re->data();
  ASSERT(value->IsFixedArray());
  return ByteArray::cast(FixedArray::cast(value)->get(INTERNAL_INDEX));
}

}}  // namespace v8::internal
