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

#define _HAS_EXCEPTIONS 0
#include <set>

#include "v8.h"

#include "ast.h"
#include "execution.h"
#include "factory.h"
#include "jsregexp-inl.h"
#include "platform.h"
#include "runtime.h"
#include "top.h"
#include "compilation-cache.h"
#include "string-stream.h"
#include "parser.h"
#include "assembler-re2k.h"
#include "regexp-macro-assembler.h"
#include "regexp-macro-assembler-re2k.h"
#include "interpreter-re2k.h"

// Including pcre.h undefines DEBUG to avoid getting debug output from
// the JSCRE implementation. Make sure to redefine it in debug mode
// after having included the header file.
#ifdef DEBUG
#include "third_party/jscre/pcre.h"
#define DEBUG
#else
#include "third_party/jscre/pcre.h"
#endif


namespace v8 { namespace internal {


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
    if (*has_pending_exception) return Handle<Object>();
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
  StringShape shape(*pattern);
  if (!pattern->IsFlat(shape)) {
    FlattenString(pattern);
    shape = StringShape(*pattern);
  }
  Handle<String> flat_string(shape.IsCons() ?
    String::cast(ConsString::cast(*pattern)->first()) :
    *pattern);
  ASSERT(flat_string->IsString());
  StringShape flat_shape(*flat_string);
  ASSERT(!flat_shape.IsCons());
  ASSERT(flat_shape.IsSequential() ||
         flat_shape.IsSliced() ||
         flat_shape.IsExternal());
  if (!flat_shape.IsAsciiRepresentation()) {
    return flat_string;
  }

  int len = flat_string->length(flat_shape);
  Handle<String> two_byte_string =
    Factory::NewRawTwoByteString(len, TENURED);
  uc16* dest = SeqTwoByteString::cast(*two_byte_string)->GetChars();
  String::WriteToFlat(*flat_string, flat_shape, dest, 0, len);
  return two_byte_string;
}


static JSRegExp::Flags RegExpFlagsFromString(Handle<String> str) {
  int flags = JSRegExp::NONE;
  StringShape shape(*str);
  for (int i = 0; i < str->length(shape); i++) {
    switch (str->Get(shape, i)) {
      case 'i':
        flags |= JSRegExp::IGNORE_CASE;
        break;
      case 'g':
        flags |= JSRegExp::GLOBAL;
        break;
      case 'm':
        flags |= JSRegExp::MULTILINE;
        break;
    }
  }
  return JSRegExp::Flags(flags);
}


static inline void ThrowRegExpException(Handle<JSRegExp> re,
                                        Handle<String> pattern,
                                        Handle<String> error_text,
                                        const char* message) {
  Handle<JSArray> array = Factory::NewJSArray(2);
  SetElement(array, 0, pattern);
  SetElement(array, 1, error_text);
  Handle<Object> regexp_err = Factory::NewSyntaxError(message, array);
  Top::Throw(*regexp_err);
}


Handle<Object> RegExpImpl::Compile(Handle<JSRegExp> re,
                                   Handle<String> pattern,
                                   Handle<String> flag_str) {
  JSRegExp::Flags flags = RegExpFlagsFromString(flag_str);
  Handle<FixedArray> cached = CompilationCache::LookupRegExp(pattern, flags);
  bool in_cache = !cached.is_null();
  Handle<Object> result;
  if (in_cache) {
    re->set_data(*cached);
    result = re;
  } else {
    FlattenString(pattern);
    RegExpParseResult parse_result;
    FlatStringReader reader(pattern);
    if (!ParseRegExp(&reader, &parse_result)) {
      // Throw an exception if we fail to parse the pattern.
      ThrowRegExpException(re,
                           pattern,
                           parse_result.error,
                           "malformed_regexp");
      return Handle<Object>();
    }
    RegExpAtom* atom = parse_result.tree->AsAtom();
    if (atom != NULL && !flags.is_ignore_case()) {
      if (parse_result.has_character_escapes) {
        Vector<const uc16> atom_pattern = atom->data();
        Handle<String> atom_string =
            Factory::NewStringFromTwoByte(atom_pattern);
        result = AtomCompile(re, pattern, flags, atom_string);
      } else {
        result = AtomCompile(re, pattern, flags, pattern);
      }
    } else {
      RegExpNode* node = NULL;
      Handle<FixedArray> re2k_data =
          RegExpEngine::Compile(&parse_result,
                                &node,
                                flags.is_ignore_case());
      if (re2k_data.is_null()) {
        result = JscrePrepare(re, pattern, flags);
      } else {
        result = Re2kPrepare(re, pattern, flags, re2k_data);
      }
    }
    Object* data = re->data();
    if (data->IsFixedArray()) {
      // If compilation succeeded then the data is set on the regexp
      // and we can store it in the cache.
      Handle<FixedArray> data(FixedArray::cast(re->data()));
      CompilationCache::PutRegExp(pattern, flags, data);
    }
  }

  LOG(RegExpCompileEvent(re, in_cache));
  return result;
}


Handle<Object> RegExpImpl::Exec(Handle<JSRegExp> regexp,
                                Handle<String> subject,
                                Handle<Object> index) {
  switch (regexp->TypeTag()) {
    case JSRegExp::JSCRE:
      return JscreExec(regexp, subject, index);
    case JSRegExp::ATOM:
      return AtomExec(regexp, subject, index);
    case JSRegExp::RE2K:
      return Re2kExec(regexp, subject, index);
    default:
      UNREACHABLE();
      return Handle<Object>();
  }
}


Handle<Object> RegExpImpl::ExecGlobal(Handle<JSRegExp> regexp,
                                Handle<String> subject) {
  switch (regexp->TypeTag()) {
    case JSRegExp::JSCRE:
      return JscreExecGlobal(regexp, subject);
    case JSRegExp::ATOM:
      return AtomExecGlobal(regexp, subject);
    case JSRegExp::RE2K:
      return Re2kExecGlobal(regexp, subject);
    default:
      UNREACHABLE();
      return Handle<Object>();
  }
}


Handle<Object> RegExpImpl::AtomCompile(Handle<JSRegExp> re,
                                       Handle<String> pattern,
                                       JSRegExp::Flags flags,
                                       Handle<String> match_pattern) {
  Factory::SetRegExpData(re, JSRegExp::ATOM, pattern, flags, match_pattern);
  return re;
}


Handle<Object> RegExpImpl::AtomExec(Handle<JSRegExp> re,
                                    Handle<String> subject,
                                    Handle<Object> index) {
  Handle<String> needle(String::cast(re->DataAt(JSRegExp::kAtomPatternIndex)));

  uint32_t start_index;
  if (!Array::IndexFromObject(*index, &start_index)) {
    return Handle<Smi>(Smi::FromInt(-1));
  }

  LOG(RegExpExecEvent(re, start_index, subject));
  int value = Runtime::StringMatch(subject, needle, start_index);
  if (value == -1) return Factory::null_value();

  Handle<FixedArray> array = Factory::NewFixedArray(2);
  array->set(0, Smi::FromInt(value));
  array->set(1, Smi::FromInt(value + needle->length()));
  return Factory::NewJSArrayWithElements(array);
}


Handle<Object> RegExpImpl::AtomExecGlobal(Handle<JSRegExp> re,
                                          Handle<String> subject) {
  Handle<String> needle(String::cast(re->DataAt(JSRegExp::kAtomPatternIndex)));
  Handle<JSArray> result = Factory::NewJSArray(1);
  int index = 0;
  int match_count = 0;
  int subject_length = subject->length();
  int needle_length = needle->length();
  while (true) {
    LOG(RegExpExecEvent(re, index, subject));
    int value = -1;
    if (index + needle_length <= subject_length) {
      value = Runtime::StringMatch(subject, needle, index);
    }
    if (value == -1) break;
    HandleScope scope;
    int end = value + needle_length;

    Handle<FixedArray> array = Factory::NewFixedArray(2);
    array->set(0, Smi::FromInt(value));
    array->set(1, Smi::FromInt(end));
    Handle<JSArray> pair = Factory::NewJSArrayWithElements(array);
    SetElement(result, match_count, pair);
    match_count++;
    index = end;
    if (needle_length == 0) index++;
  }
  return result;
}


Handle<Object>RegExpImpl::JscrePrepare(Handle<JSRegExp> re,
                                       Handle<String> pattern,
                                       JSRegExp::Flags flags) {
  Handle<Object> value(Heap::undefined_value());
  Factory::SetRegExpData(re, JSRegExp::JSCRE, pattern, flags, value);
  return re;
}


Handle<Object>RegExpImpl::Re2kPrepare(Handle<JSRegExp> re,
                                      Handle<String> pattern,
                                      JSRegExp::Flags flags,
                                      Handle<FixedArray> re2k_data) {
  Factory::SetRegExpData(re, JSRegExp::RE2K, pattern, flags, re2k_data);
  return re;
}


static inline Object* DoCompile(String* pattern,
                                JSRegExp::Flags flags,
                                unsigned* number_of_captures,
                                const char** error_message,
                                JscreRegExp** code) {
  JSRegExpIgnoreCaseOption case_option = flags.is_ignore_case()
    ? JSRegExpIgnoreCase
    : JSRegExpDoNotIgnoreCase;
  JSRegExpMultilineOption multiline_option = flags.is_multiline()
    ? JSRegExpMultiline
    : JSRegExpSingleLine;
  *error_message = NULL;
  malloc_failure = Failure::Exception();
  *code = jsRegExpCompile(pattern->GetTwoByteData(),
                          pattern->length(),
                          case_option,
                          multiline_option,
                          number_of_captures,
                          error_message,
                          &JSREMalloc,
                          &JSREFree);
  if (*code == NULL && (malloc_failure->IsRetryAfterGC() ||
                       malloc_failure->IsOutOfMemoryFailure())) {
    return malloc_failure;
  } else {
    // It doesn't matter which object we return here, we just need to return
    // a non-failure to indicate to the GC-retry code that there was no
    // allocation failure.
    return pattern;
  }
}


void CompileWithRetryAfterGC(Handle<String> pattern,
                             JSRegExp::Flags flags,
                             unsigned* number_of_captures,
                             const char** error_message,
                             JscreRegExp** code) {
  CALL_HEAP_FUNCTION_VOID(DoCompile(*pattern,
                                    flags,
                                    number_of_captures,
                                    error_message,
                                    code));
}


Handle<Object> RegExpImpl::JscreCompile(Handle<JSRegExp> re) {
  ASSERT_EQ(re->TypeTag(), JSRegExp::JSCRE);
  ASSERT(re->DataAt(JSRegExp::kJscreDataIndex)->IsUndefined());

  Handle<String> pattern(re->Pattern());
  JSRegExp::Flags flags = re->GetFlags();

  Handle<String> two_byte_pattern = StringToTwoByte(pattern);

  unsigned number_of_captures;
  const char* error_message = NULL;

  JscreRegExp* code = NULL;
  FlattenString(pattern);

  CompileWithRetryAfterGC(two_byte_pattern,
                          flags,
                          &number_of_captures,
                          &error_message,
                          &code);

  if (code == NULL) {
    // Throw an exception.
    Handle<JSArray> array = Factory::NewJSArray(2);
    SetElement(array, 0, pattern);
    SetElement(array, 1, Factory::NewStringFromUtf8(CStrVector(
        (error_message == NULL) ? "Unknown regexp error" : error_message)));
    Handle<Object> regexp_err =
        Factory::NewSyntaxError("malformed_regexp", array);
    Top::Throw(*regexp_err);
    return Handle<Object>();
  }

  // Convert the return address to a ByteArray pointer.
  Handle<ByteArray> internal(
      ByteArray::FromDataStartAddress(reinterpret_cast<Address>(code)));

  Handle<FixedArray> value = Factory::NewFixedArray(kJscreDataLength);
  value->set(kJscreNumberOfCapturesIndex, Smi::FromInt(number_of_captures));
  value->set(kJscreInternalIndex, *internal);
  Factory::SetRegExpData(re, JSRegExp::JSCRE, pattern, flags, value);

  return re;
}


Handle<Object> RegExpImpl::Re2kExecOnce(Handle<JSRegExp> regexp,
                                        int num_captures,
                                        Handle<String> two_byte_subject,
                                        int previous_index,
                                        int* offsets_vector,
                                        int offsets_vector_length) {
#ifdef DEBUG
  if (FLAG_trace_regexp_bytecodes) {
    String* pattern = regexp->Pattern();
    PrintF("\n\nRegexp match:   /%s/\n\n", *(pattern->ToCString()));
    PrintF("\n\nSubject string: '%s'\n\n", *(two_byte_subject->ToCString()));
  }
#endif
  ASSERT(StringShape(*two_byte_subject).IsTwoByteRepresentation());
  ASSERT(two_byte_subject->IsFlat(StringShape(*two_byte_subject)));
  bool rc;
  {
    for (int i = (num_captures + 1) * 2 - 1; i >= 0; i--) {
      offsets_vector[i] = -1;
    }

    LOG(RegExpExecEvent(regexp, previous_index, two_byte_subject));

    Handle<ByteArray> byte_codes = Re2kCode(regexp);

    rc = Re2kInterpreter::Match(byte_codes,
                                two_byte_subject,
                                offsets_vector,
                                previous_index);
  }

  if (!rc) {
    return Factory::null_value();
  }

  Handle<FixedArray> array = Factory::NewFixedArray(2 * (num_captures+1));
  // The captures come in (start, end+1) pairs.
  for (int i = 0; i < 2 * (num_captures+1); i += 2) {
    array->set(i, Smi::FromInt(offsets_vector[i]));
    array->set(i+1, Smi::FromInt(offsets_vector[i+1]));
  }
  return Factory::NewJSArrayWithElements(array);
}


Handle<Object> RegExpImpl::JscreExecOnce(Handle<JSRegExp> regexp,
                                         int num_captures,
                                         Handle<String> subject,
                                         int previous_index,
                                         const uc16* two_byte_subject,
                                         int* offsets_vector,
                                         int offsets_vector_length) {
  int rc;
  {
    AssertNoAllocation a;
    ByteArray* internal = JscreInternal(regexp);
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

  Handle<FixedArray> array = Factory::NewFixedArray(2 * (num_captures+1));
  // The captures come in (start, end+1) pairs.
  for (int i = 0; i < 2 * (num_captures+1); i += 2) {
    array->set(i, Smi::FromInt(offsets_vector[i]));
    array->set(i+1, Smi::FromInt(offsets_vector[i+1]));
  }
  return Factory::NewJSArrayWithElements(array);
}


class OffsetsVector {
 public:
  inline OffsetsVector(int num_registers) :
    offsets_vector_length_(num_registers) {
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
  static const int kStaticOffsetsVectorSize = 50;
  static int static_offsets_vector_[kStaticOffsetsVectorSize];
};


int OffsetsVector::static_offsets_vector_[
    OffsetsVector::kStaticOffsetsVectorSize];


Handle<Object> RegExpImpl::Re2kExec(Handle<JSRegExp> regexp,
                                    Handle<String> subject,
                                    Handle<Object> index) {
  ASSERT_EQ(regexp->TypeTag(), JSRegExp::RE2K);
  ASSERT(!regexp->DataAt(JSRegExp::kRe2kDataIndex)->IsUndefined());

  // Prepare space for the return values.
  int number_of_registers = Re2kNumberOfRegisters(regexp);
  OffsetsVector offsets(number_of_registers);

  int num_captures = Re2kNumberOfCaptures(regexp);

  int previous_index = static_cast<int>(DoubleToInteger(index->Number()));

  Handle<String> subject16 = CachedStringToTwoByte(subject);

  Handle<Object> result(
      Re2kExecOnce(regexp,
                   num_captures,
                   subject16,
                   previous_index,
                   offsets.vector(),
                   offsets.length()));
  return result;
}


Handle<Object> RegExpImpl::JscreExec(Handle<JSRegExp> regexp,
                                     Handle<String> subject,
                                     Handle<Object> index) {
  ASSERT_EQ(regexp->TypeTag(), JSRegExp::JSCRE);
  if (regexp->DataAt(JSRegExp::kJscreDataIndex)->IsUndefined()) {
    Handle<Object> compile_result = JscreCompile(regexp);
    if (compile_result->IsException()) return compile_result;
  }
  ASSERT(regexp->DataAt(JSRegExp::kJscreDataIndex)->IsFixedArray());

  int num_captures = JscreNumberOfCaptures(regexp);

  OffsetsVector offsets((num_captures + 1) * 3);

  int previous_index = static_cast<int>(DoubleToInteger(index->Number()));

  Handle<String> subject16 = CachedStringToTwoByte(subject);

  Handle<Object> result(JscreExecOnce(regexp,
                                      num_captures,
                                      subject,
                                      previous_index,
                                      subject16->GetTwoByteData(),
                                      offsets.vector(),
                                      offsets.length()));

  return result;
}


Handle<Object> RegExpImpl::Re2kExecGlobal(Handle<JSRegExp> regexp,
                                          Handle<String> subject) {
  ASSERT_EQ(regexp->TypeTag(), JSRegExp::RE2K);
  ASSERT(!regexp->DataAt(JSRegExp::kRe2kDataIndex)->IsUndefined());

  // Prepare space for the return values.
  int number_of_registers = Re2kNumberOfRegisters(regexp);
  OffsetsVector offsets(number_of_registers);

  int previous_index = 0;

  Handle<JSArray> result = Factory::NewJSArray(0);
  int i = 0;
  Handle<Object> matches;

  Handle<String> subject16 = CachedStringToTwoByte(subject);

  do {
    if (previous_index > subject->length() || previous_index < 0) {
      // Per ECMA-262 15.10.6.2, if the previous index is greater than the
      // string length, there is no match.
      matches = Factory::null_value();
    } else {
      matches = Re2kExecOnce(regexp,
                             Re2kNumberOfCaptures(regexp),
                             subject16,
                             previous_index,
                             offsets.vector(),
                             offsets.length());

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


Handle<Object> RegExpImpl::JscreExecGlobal(Handle<JSRegExp> regexp,
                                           Handle<String> subject) {
  ASSERT_EQ(regexp->TypeTag(), JSRegExp::JSCRE);
  if (regexp->DataAt(JSRegExp::kJscreDataIndex)->IsUndefined()) {
    Handle<Object> compile_result = JscreCompile(regexp);
    if (compile_result->IsException()) return compile_result;
  }
  ASSERT(regexp->DataAt(JSRegExp::kJscreDataIndex)->IsFixedArray());

  // Prepare space for the return values.
  int num_captures = JscreNumberOfCaptures(regexp);

  OffsetsVector offsets((num_captures + 1) * 3);

  int previous_index = 0;

  Handle<JSArray> result = Factory::NewJSArray(0);
  int i = 0;
  Handle<Object> matches;

  Handle<String> subject16 = CachedStringToTwoByte(subject);

  do {
    if (previous_index > subject->length() || previous_index < 0) {
      // Per ECMA-262 15.10.6.2, if the previous index is greater than the
      // string length, there is no match.
      matches = Factory::null_value();
    } else {
      matches = JscreExecOnce(regexp,
                              num_captures,
                              subject,
                              previous_index,
                              subject16->GetTwoByteData(),
                              offsets.vector(),
                              offsets.length());

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


int RegExpImpl::JscreNumberOfCaptures(Handle<JSRegExp> re) {
  FixedArray* value = FixedArray::cast(re->DataAt(JSRegExp::kJscreDataIndex));
  return Smi::cast(value->get(kJscreNumberOfCapturesIndex))->
      value();
}


ByteArray* RegExpImpl::JscreInternal(Handle<JSRegExp> re) {
  FixedArray* value = FixedArray::cast(re->DataAt(JSRegExp::kJscreDataIndex));
  return ByteArray::cast(value->get(kJscreInternalIndex));
}


int RegExpImpl::Re2kNumberOfCaptures(Handle<JSRegExp> re) {
  FixedArray* value = FixedArray::cast(re->DataAt(JSRegExp::kRe2kDataIndex));
  return Smi::cast(value->get(kRe2kNumberOfCapturesIndex))->value();
}


int RegExpImpl::Re2kNumberOfRegisters(Handle<JSRegExp> re) {
  FixedArray* value = FixedArray::cast(re->DataAt(JSRegExp::kRe2kDataIndex));
  return Smi::cast(value->get(kRe2kNumberOfRegistersIndex))->value();
}


Handle<ByteArray> RegExpImpl::Re2kCode(Handle<JSRegExp> re) {
  FixedArray* value = FixedArray::cast(re->DataAt(JSRegExp::kRe2kDataIndex));
  return Handle<ByteArray>(ByteArray::cast(value->get(kRe2kCodeIndex)));
}


// -------------------------------------------------------------------
// New regular expression engine


void RegExpTree::AppendToText(RegExpText* text) {
  UNREACHABLE();
}


void RegExpAtom::AppendToText(RegExpText* text) {
  text->AddElement(TextElement::Atom(this));
}


void RegExpCharacterClass::AppendToText(RegExpText* text) {
  text->AddElement(TextElement::CharClass(this));
}


void RegExpText::AppendToText(RegExpText* text) {
  for (int i = 0; i < elements()->length(); i++)
    text->AddElement(elements()->at(i));
}


TextElement TextElement::Atom(RegExpAtom* atom) {
  TextElement result = TextElement(ATOM);
  result.data.u_atom = atom;
  return result;
}


TextElement TextElement::CharClass(
      RegExpCharacterClass* char_class) {
  TextElement result = TextElement(CHAR_CLASS);
  result.data.u_char_class = char_class;
  return result;
}


class RegExpCompiler {
 public:
  explicit RegExpCompiler(int capture_count);

  int AllocateRegister() { return next_register_++; }

  Handle<FixedArray> Assemble(RegExpMacroAssembler* assembler,
                              RegExpNode* start,
                              int capture_count,
                              bool case_independent);

  inline void AddWork(RegExpNode* node) { work_list_->Add(node); }

  static const int kImplementationOffset = 0;
  static const int kNumberOfRegistersOffset = 0;
  static const int kCodeOffset = 1;

  RegExpMacroAssembler* macro_assembler() { return macro_assembler_; }
  EndNode* accept() { return accept_; }
  EndNode* backtrack() { return backtrack_; }

  static const int kMaxRecursion = 100;
  inline int recursion_depth() { return recursion_depth_; }
  inline void IncrementRecursionDepth() { recursion_depth_++; }
  inline void DecrementRecursionDepth() { recursion_depth_--; }

 private:
  EndNode* accept_;
  EndNode* backtrack_;
  int next_register_;
  List<RegExpNode*>* work_list_;
  int recursion_depth_;
  RegExpMacroAssembler* macro_assembler_;
};


// Attempts to compile the regexp using a Regexp2000 code generator.  Returns
// a fixed array or a null handle depending on whether it succeeded.
RegExpCompiler::RegExpCompiler(int capture_count)
  : next_register_(2 * (capture_count + 1)),
    work_list_(NULL),
    recursion_depth_(0) {
  accept_ = new EndNode(EndNode::ACCEPT);
  backtrack_ = new EndNode(EndNode::BACKTRACK);
}


Handle<FixedArray> RegExpCompiler::Assemble(
    RegExpMacroAssembler* macro_assembler,
    RegExpNode* start,
    int capture_count,
    bool case_independent) {
  if (case_independent) return Handle<FixedArray>::null();
  macro_assembler_ = macro_assembler;
  List <RegExpNode*> work_list(0);
  work_list_ = &work_list;
  Label fail;
  macro_assembler->PushBacktrack(&fail);
  if (!start->GoTo(this)) {
    fail.Unuse();
    return Handle<FixedArray>::null();
  }
  while (!work_list.is_empty()) {
    if (!work_list.RemoveLast()->GoTo(this)) {
      fail.Unuse();
      return Handle<FixedArray>::null();
    }
  }
  macro_assembler->Bind(&fail);
  macro_assembler->Fail();
  Handle<FixedArray> array =
      Factory::NewFixedArray(RegExpImpl::kRe2kDataLength);
  array->set(RegExpImpl::kRe2kImplementationIndex,
             Smi::FromInt(macro_assembler->Implementation()));
  array->set(RegExpImpl::kRe2kNumberOfRegistersIndex,
             Smi::FromInt(next_register_));
  array->set(RegExpImpl::kRe2kNumberOfCapturesIndex,
             Smi::FromInt(capture_count));
  Handle<Object> code = macro_assembler->GetCode();
  array->set(RegExpImpl::kRe2kCodeIndex, *code);
  work_list_ = NULL;
  return array;
}


bool RegExpNode::GoTo(RegExpCompiler* compiler) {
  // TODO(erikcorry): Implement support.
  if (info_.follows_word_interest ||
      info_.follows_newline_interest ||
      info_.follows_start_interest) {
    return false;
  }
  if (label_.is_bound()) {
    compiler->macro_assembler()->GoTo(&label_);
    return true;
  } else {
    if (compiler->recursion_depth() > RegExpCompiler::kMaxRecursion) {
      compiler->macro_assembler()->GoTo(&label_);
      compiler->AddWork(this);
      return true;
    } else {
      compiler->IncrementRecursionDepth();
      bool how_it_went = Emit(compiler);
      compiler->DecrementRecursionDepth();
      return how_it_went;
    }
  }
}


bool EndNode::GoTo(RegExpCompiler* compiler) {
  if (info()->follows_word_interest ||
      info()->follows_newline_interest ||
      info()->follows_start_interest) {
    return false;
  }
  if (!label()->is_bound()) {
    Bind(compiler->macro_assembler());
  }
  switch (action_) {
    case ACCEPT:
      compiler->macro_assembler()->Succeed();
    break;
    case BACKTRACK:
      compiler->macro_assembler()->Backtrack();
    break;
  }
  return true;
}


Label* RegExpNode::label() {
  return &label_;
}


bool EndNode::Emit(RegExpCompiler* compiler) {
  RegExpMacroAssembler* macro = compiler->macro_assembler();
  switch (action_) {
    case ACCEPT:
      Bind(macro);
      macro->Succeed();
      return true;
    case BACKTRACK:
      Bind(macro);
      macro->Backtrack();
      return true;
  }
  return false;
}


void GuardedAlternative::AddGuard(Guard* guard) {
  if (guards_ == NULL)
    guards_ = new ZoneList<Guard*>(1);
  guards_->Add(guard);
}


ActionNode* ActionNode::StoreRegister(int reg,
                                      int val,
                                      RegExpNode* on_success) {
  ActionNode* result = new ActionNode(STORE_REGISTER, on_success);
  result->data_.u_store_register.reg = reg;
  result->data_.u_store_register.value = val;
  return result;
}


ActionNode* ActionNode::IncrementRegister(int reg, RegExpNode* on_success) {
  ActionNode* result = new ActionNode(INCREMENT_REGISTER, on_success);
  result->data_.u_increment_register.reg = reg;
  return result;
}


ActionNode* ActionNode::StorePosition(int reg, RegExpNode* on_success) {
  ActionNode* result = new ActionNode(STORE_POSITION, on_success);
  result->data_.u_position_register.reg = reg;
  return result;
}


ActionNode* ActionNode::SavePosition(int reg, RegExpNode* on_success) {
  ActionNode* result = new ActionNode(SAVE_POSITION, on_success);
  result->data_.u_position_register.reg = reg;
  return result;
}


ActionNode* ActionNode::RestorePosition(int reg, RegExpNode* on_success) {
  ActionNode* result = new ActionNode(RESTORE_POSITION, on_success);
  result->data_.u_position_register.reg = reg;
  return result;
}


ActionNode* ActionNode::BeginSubmatch(int reg, RegExpNode* on_success) {
  ActionNode* result = new ActionNode(BEGIN_SUBMATCH, on_success);
  result->data_.u_submatch_stack_pointer_register.reg = reg;
  return result;
}


ActionNode* ActionNode::EscapeSubmatch(int reg, RegExpNode* on_success) {
  ActionNode* result = new ActionNode(ESCAPE_SUBMATCH, on_success);
  result->data_.u_submatch_stack_pointer_register.reg = reg;
  return result;
}


#define DEFINE_ACCEPT(Type)                                          \
  void Type##Node::Accept(NodeVisitor* visitor) {                    \
    visitor->Visit##Type(this);                                      \
  }
FOR_EACH_NODE_TYPE(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT


// -------------------------------------------------------------------
// Emit code.


void ChoiceNode::GenerateGuard(RegExpMacroAssembler* macro_assembler,
                               Guard* guard,
                               Label* on_failure) {
  switch (guard->op()) {
    case Guard::LT:
      macro_assembler->IfRegisterGE(guard->reg(), guard->value(), on_failure);
      break;
    case Guard::GEQ:
      macro_assembler->IfRegisterLT(guard->reg(), guard->value(), on_failure);
      break;
  }
}


bool TextNode::Emit(RegExpCompiler* compiler) {
  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  Bind(macro_assembler);
  int element_count = elms_->length();
  int cp_offset = 0;
  for (int i = 0; i < element_count; i++) {
    TextElement elm = (*elms_)[i];
    switch (elm.type) {
      case TextElement::ATOM: {
        Vector<const uc16> quarks = elm.data.u_atom->data();
        macro_assembler->CheckCharacters(quarks,
                                         cp_offset,
                                         on_failure_->label());
        cp_offset += quarks.length();
        break;
      }
      case TextElement::CHAR_CLASS: {
        RegExpCharacterClass* cc = elm.data.u_char_class;
        macro_assembler->LoadCurrentCharacter(cp_offset, on_failure_->label());
        cp_offset++;

        ZoneList<CharacterRange>* ranges = cc->ranges();

        Label success;

        Label *char_is_in_class =
            cc->is_negated() ? on_failure_->label() : &success;

        int range_count = ranges->length();

        if (range_count == 0) {
          if (!cc->is_negated()) {
            on_failure()->GoTo(compiler);
          }
          break;
        }

        for (int i = 0; i < range_count - 1; i++) {
          CharacterRange& range = (*ranges)[i];
          Label next_range;
          uc16 from = range.from();
          uc16 to = range.to();
          if (to == from) {
            macro_assembler->CheckCharacter(to, char_is_in_class);
          } else {
            if (from != 0) {
              macro_assembler->CheckCharacterLT(from, &next_range);
            }
            if (to != 0xffff) {
              macro_assembler->CheckCharacterLT(to + 1, char_is_in_class);
            } else {
              macro_assembler->GoTo(char_is_in_class);
            }
          }
          macro_assembler->Bind(&next_range);
        }

        if (range_count != 0) {
          CharacterRange& range = (*ranges)[range_count - 1];
          uc16 from = range.from();
          uc16 to = range.to();

          if (to == from) {
            if (cc->is_negated()) {
              macro_assembler->CheckCharacter(to, on_failure_->label());
            } else {
              macro_assembler->CheckNotCharacter(to, on_failure_->label());
            }
          } else {
            if (from != 0) {
              if (!cc->is_negated()) {
                macro_assembler->CheckCharacterLT(from, on_failure_->label());
              } else {
                macro_assembler->CheckCharacterLT(from, &success);
              }
            }
            if (to != 0xffff) {
              if (!cc->is_negated()) {
                macro_assembler->CheckCharacterGT(to, on_failure_->label());
              } else {
                macro_assembler->CheckCharacterLT(to + 1, on_failure_->label());
              }
            } else {
              if (cc->is_negated()) {
                macro_assembler->GoTo(on_failure_->label());
              }
            }
          }
        } else if (cc->is_negated()) {
          macro_assembler->GoTo(on_failure_->label());
        }

        macro_assembler->Bind(&success);

        break;
      }
      default:
        UNREACHABLE();
        return false;
    }
  }
  compiler->AddWork(on_failure_);
  macro_assembler->AdvanceCurrentPosition(cp_offset);
  return on_success()->GoTo(compiler);
}


bool ChoiceNode::Emit(RegExpCompiler* compiler) {
  int choice_count = alternatives_->length();
  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  Bind(macro_assembler);
  // For now we just call all choices one after the other.  The idea ultimately
  // is to use the Dispatch table to try only the relevant ones.
  int i;
  for (i = 0; i < choice_count - 1; i++) {
    GuardedAlternative alternative = (*alternatives_)[i];
    Label after;
    Label after_no_pop_cp;
    ZoneList<Guard*>* guards = alternative.guards();
    if (guards != NULL) {
      int guard_count = guards->length();
      for (int j = 0; j < guard_count; j++) {
        GenerateGuard(macro_assembler, (*guards)[j], &after_no_pop_cp);
      }
    }
    macro_assembler->PushCurrentPosition();
    macro_assembler->PushBacktrack(&after);
    if (!alternative.node()->GoTo(compiler)) {
      after.Unuse();
      after_no_pop_cp.Unuse();
      return false;
    }
    macro_assembler->Bind(&after);
    macro_assembler->PopCurrentPosition();
    macro_assembler->Bind(&after_no_pop_cp);
  }
  GuardedAlternative alternative = (*alternatives_)[i];
  ZoneList<Guard*>* guards = alternative.guards();
  if (guards != NULL) {
    int guard_count = guards->length();
    for (int j = 0; j < guard_count; j++) {
      GenerateGuard(macro_assembler, (*guards)[j], on_failure_->label());
    }
  }
  if (!on_failure_->IsBacktrack()) {
    macro_assembler->PushBacktrack(on_failure_->label());
    compiler->AddWork(on_failure_);
  }
  if (!alternative.node()->GoTo(compiler)) {
    return false;
  }
  return true;
}


bool ActionNode::Emit(RegExpCompiler* compiler) {
  RegExpMacroAssembler* macro = compiler->macro_assembler();
  Bind(macro);
  switch (type_) {
    case STORE_REGISTER:
      macro->SetRegister(data_.u_store_register.reg,
                         data_.u_store_register.value);
      break;
    case INCREMENT_REGISTER: {
      Label undo;
      macro->PushBacktrack(&undo);
      macro->AdvanceRegister(data_.u_increment_register.reg, 1);
      bool ok = on_success()->GoTo(compiler);
      if (!ok) {
        undo.Unuse();
        return false;
      }
      macro->Bind(&undo);
      macro->AdvanceRegister(data_.u_increment_register.reg, -1);
      macro->Backtrack();
      break;
    }
    case STORE_POSITION: {
      Label undo;
      macro->PushRegister(data_.u_position_register.reg);
      macro->PushBacktrack(&undo);
      macro->WriteCurrentPositionToRegister(data_.u_position_register.reg);
      bool ok = on_success()->GoTo(compiler);
      if (!ok) {
        undo.Unuse();
        return false;
      }
      macro->Bind(&undo);
      macro->PopRegister(data_.u_position_register.reg);
      macro->Backtrack();
      break;
    }
    case SAVE_POSITION:
      macro->WriteCurrentPositionToRegister(
          data_.u_position_register.reg);
      break;
    case RESTORE_POSITION:
      macro->ReadCurrentPositionFromRegister(
          data_.u_position_register.reg);
      break;
    case BEGIN_SUBMATCH:
      macro->WriteStackPointerToRegister(
          data_.u_submatch_stack_pointer_register.reg);
      break;
    case ESCAPE_SUBMATCH:
      macro->ReadStackPointerFromRegister(
          data_.u_submatch_stack_pointer_register.reg);
      break;
    default:
      UNREACHABLE();
      return false;
  }
  return on_success()->GoTo(compiler);
}


// -------------------------------------------------------------------
// Dot/dotty output


#ifdef DEBUG


class DotPrinter: public NodeVisitor {
 public:
  DotPrinter() : stream_(&alloc_) { }
  void PrintNode(const char* label, RegExpNode* node);
  void Visit(RegExpNode* node);
  void PrintOnFailure(RegExpNode* from, RegExpNode* on_failure);
  StringStream* stream() { return &stream_; }
#define DECLARE_VISIT(Type)                                          \
  virtual void Visit##Type(Type##Node* that);
FOR_EACH_NODE_TYPE(DECLARE_VISIT)
#undef DECLARE_VISIT
 private:
  HeapStringAllocator alloc_;
  StringStream stream_;
  std::set<RegExpNode*> seen_;
};


void DotPrinter::PrintNode(const char* label, RegExpNode* node) {
  stream()->Add("digraph G {\n  graph [label=\"");
  for (int i = 0; label[i]; i++) {
    switch (label[i]) {
      case '\\':
        stream()->Add("\\\\");
        break;
      case '"':
        stream()->Add("\"");
        break;
      default:
        stream()->Put(label[i]);
        break;
    }
  }
  stream()->Add("\"];\n");
  Visit(node);
  stream()->Add("}\n");
  printf("%s", *(stream()->ToCString()));
}


void DotPrinter::Visit(RegExpNode* node) {
  if (seen_.find(node) != seen_.end())
    return;
  seen_.insert(node);
  node->Accept(this);
}


void DotPrinter::PrintOnFailure(RegExpNode* from, RegExpNode* on_failure) {
  if (on_failure->IsBacktrack()) return;
  stream()->Add("  n%p -> n%p [style=dotted];\n", from, on_failure);
  Visit(on_failure);
}


class TableEntryBodyPrinter {
 public:
  TableEntryBodyPrinter(StringStream* stream, ChoiceNode* choice)
    : stream_(stream), choice_(choice) { }
  void Call(uc16 from, DispatchTable::Entry entry) {
    OutSet* out_set = entry.out_set();
    for (unsigned i = 0; i < OutSet::kFirstLimit; i++) {
      if (out_set->Get(i)) {
        stream()->Add("    n%p:s%io%i -> n%p;\n",
                      choice(),
                      from,
                      i,
                      choice()->alternatives()->at(i).node());
      }
    }
  }
 private:
  StringStream* stream() { return stream_; }
  ChoiceNode* choice() { return choice_; }
  StringStream* stream_;
  ChoiceNode* choice_;
};


class TableEntryHeaderPrinter {
 public:
  explicit TableEntryHeaderPrinter(StringStream* stream)
    : first_(true), stream_(stream) { }
  void Call(uc16 from, DispatchTable::Entry entry) {
    if (first_) {
      first_ = false;
    } else {
      stream()->Add("|");
    }
    stream()->Add("{\\%k-\\%k|{", from, entry.to());
    OutSet* out_set = entry.out_set();
    int priority = 0;
    for (unsigned i = 0; i < OutSet::kFirstLimit; i++) {
      if (out_set->Get(i)) {
        if (priority > 0) stream()->Add("|");
        stream()->Add("<s%io%i> %i", from, i, priority);
        priority++;
      }
    }
    stream()->Add("}}");
  }
 private:
  bool first_;
  StringStream* stream() { return stream_; }
  StringStream* stream_;
};


void DotPrinter::VisitChoice(ChoiceNode* that) {
  stream()->Add("  n%p [shape=Mrecord, label=\"", that);
  TableEntryHeaderPrinter header_printer(stream());
  that->table()->ForEach(&header_printer);
  stream()->Add("\"]\n", that);
  TableEntryBodyPrinter body_printer(stream(), that);
  that->table()->ForEach(&body_printer);
  PrintOnFailure(that, that->on_failure());
  for (int i = 0; i < that->alternatives()->length(); i++) {
    GuardedAlternative alt = that->alternatives()->at(i);
    alt.node()->Accept(this);
  }
}


void DotPrinter::VisitText(TextNode* that) {
  stream()->Add("  n%p [label=\"", that);
  for (int i = 0; i < that->elements()->length(); i++) {
    if (i > 0) stream()->Add(" ");
    TextElement elm = that->elements()->at(i);
    switch (elm.type) {
      case TextElement::ATOM: {
        stream()->Add("'%w'", elm.data.u_atom->data());
        break;
      }
      case TextElement::CHAR_CLASS: {
        RegExpCharacterClass* node = elm.data.u_char_class;
        stream()->Add("[");
        if (node->is_negated())
          stream()->Add("^");
        for (int j = 0; j < node->ranges()->length(); j++) {
          CharacterRange range = node->ranges()->at(j);
          stream()->Add("%k-%k", range.from(), range.to());
        }
        stream()->Add("]");
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  stream()->Add("\", shape=box, peripheries=2];\n");
  stream()->Add("  n%p -> n%p;\n", that, that->on_success());
  Visit(that->on_success());
  PrintOnFailure(that, that->on_failure());
}


void DotPrinter::VisitBackreference(BackreferenceNode* that) {
  stream()->Add("  n%p [label=\"$%i..$%i\", shape=doubleoctagon];\n",
                that,
                that->start_register(),
                that->end_register());
  stream()->Add("  n%p -> n%p;\n", that, that->on_success());
  Visit(that->on_success());
  PrintOnFailure(that, that->on_failure());
}


void DotPrinter::VisitEnd(EndNode* that) {
  stream()->Add("  n%p [style=bold, shape=point];\n", that);
}


void DotPrinter::VisitAction(ActionNode* that) {
  stream()->Add("  n%p [", that);
  switch (that->type_) {
    case ActionNode::STORE_REGISTER:
      stream()->Add("label=\"$%i:=%i\", shape=octagon",
                    that->data_.u_store_register.reg,
                    that->data_.u_store_register.value);
      break;
    case ActionNode::INCREMENT_REGISTER:
      stream()->Add("label=\"$%i++\", shape=octagon",
                    that->data_.u_increment_register.reg);
      break;
    case ActionNode::STORE_POSITION:
      stream()->Add("label=\"$%i:=$pos\", shape=octagon",
                    that->data_.u_position_register.reg);
      break;
    case ActionNode::SAVE_POSITION:
      stream()->Add("label=\"$%i:=$pos\", shape=octagon",
                    that->data_.u_position_register.reg);
      break;
    case ActionNode::RESTORE_POSITION:
      stream()->Add("label=\"$pos:=$%i\", shape=octagon",
                    that->data_.u_position_register.reg);
      break;
    case ActionNode::BEGIN_SUBMATCH:
      stream()->Add("label=\"begin\", shape=septagon");
      break;
    case ActionNode::ESCAPE_SUBMATCH:
      stream()->Add("label=\"escape\", shape=septagon");
      break;
  }
  stream()->Add("];\n");
  stream()->Add("  n%p -> n%p;\n", that, that->on_success());
  Visit(that->on_success());
}


class DispatchTableDumper {
 public:
  explicit DispatchTableDumper(StringStream* stream) : stream_(stream) { }
  void Call(uc16 key, DispatchTable::Entry entry);
  StringStream* stream() { return stream_; }
 private:
  StringStream* stream_;
};


void DispatchTableDumper::Call(uc16 key, DispatchTable::Entry entry) {
  stream()->Add("[%k-%k]: {", key, entry.to());
  OutSet* set = entry.out_set();
  bool first = true;
  for (unsigned i = 0; i < OutSet::kFirstLimit; i++) {
    if (set->Get(i)) {
      if (first) {
        first = false;
      } else {
        stream()->Add(", ");
      }
      stream()->Add("%i", i);
    }
  }
  stream()->Add("}\n");
}


void DispatchTable::Dump() {
  HeapStringAllocator alloc;
  StringStream stream(&alloc);
  DispatchTableDumper dumper(&stream);
  tree()->ForEach(&dumper);
  OS::PrintError("%s", *stream.ToCString());
}


void RegExpEngine::DotPrint(const char* label, RegExpNode* node) {
  DotPrinter printer;
  printer.PrintNode(label, node);
}


#endif  // DEBUG


// -------------------------------------------------------------------
// Tree to graph conversion


RegExpNode* RegExpAtom::ToNode(RegExpCompiler* compiler,
                               RegExpNode* on_success,
                               RegExpNode* on_failure) {
  ZoneList<TextElement>* elms = new ZoneList<TextElement>(1);
  elms->Add(TextElement::Atom(this));
  return new TextNode(elms, on_success, on_failure);
}


RegExpNode* RegExpText::ToNode(RegExpCompiler* compiler,
                               RegExpNode* on_success,
                               RegExpNode* on_failure) {
  return new TextNode(elements(), on_success, on_failure);
}


RegExpNode* RegExpCharacterClass::ToNode(RegExpCompiler* compiler,
                                         RegExpNode* on_success,
                                         RegExpNode* on_failure) {
  ZoneList<TextElement>* elms = new ZoneList<TextElement>(1);
  elms->Add(TextElement::CharClass(this));
  return new TextNode(elms, on_success, on_failure);
}


RegExpNode* RegExpDisjunction::ToNode(RegExpCompiler* compiler,
                                      RegExpNode* on_success,
                                      RegExpNode* on_failure) {
  ZoneList<RegExpTree*>* alternatives = this->alternatives();
  int length = alternatives->length();
  ChoiceNode* result = new ChoiceNode(length, on_failure);
  for (int i = 0; i < length; i++) {
    GuardedAlternative alternative(alternatives->at(i)->ToNode(compiler,
                                                               on_success,
                                                               on_failure));
    result->AddAlternative(alternative);
  }
  return result;
}


RegExpNode* RegExpQuantifier::ToNode(RegExpCompiler* compiler,
                                     RegExpNode* on_success,
                                     RegExpNode* on_failure) {
  return ToNode(min(),
                max(),
                is_greedy(),
                body(),
                compiler,
                on_success,
                on_failure);
}


RegExpNode* RegExpQuantifier::ToNode(int min,
                                     int max,
                                     bool is_greedy,
                                     RegExpTree* body,
                                     RegExpCompiler* compiler,
                                     RegExpNode* on_success,
                                     RegExpNode* on_failure) {
  // x{f, t} becomes this:
  //
  //             (r++)<-.
  //               |     `
  //               |     (x)
  //               v     ^
  //      (r=0)-->(?)---/ [if r < t]
  //               |
  //   [if r >= f] \----> ...
  //
  //
  // TODO(someone): clear captures on repetition and handle empty
  //   matches.
  bool has_min = min > 0;
  bool has_max = max < RegExpQuantifier::kInfinity;
  bool needs_counter = has_min || has_max;
  int reg_ctr = needs_counter ? compiler->AllocateRegister() : -1;
  ChoiceNode* center = new ChoiceNode(2, on_failure);
  RegExpNode* loop_return = needs_counter
      ? static_cast<RegExpNode*>(ActionNode::IncrementRegister(reg_ctr, center))
      : static_cast<RegExpNode*>(center);
  RegExpNode* body_node = body->ToNode(compiler, loop_return, on_failure);
  GuardedAlternative body_alt(body_node);
  if (has_max) {
    Guard* body_guard = new Guard(reg_ctr, Guard::LT, max);
    body_alt.AddGuard(body_guard);
  }
  GuardedAlternative rest_alt(on_success);
  if (has_min) {
    Guard* rest_guard = new Guard(reg_ctr, Guard::GEQ, min);
    rest_alt.AddGuard(rest_guard);
  }
  if (is_greedy) {
    center->AddAlternative(body_alt);
    center->AddAlternative(rest_alt);
  } else {
    center->AddAlternative(rest_alt);
    center->AddAlternative(body_alt);
  }
  if (needs_counter) {
    return ActionNode::StoreRegister(reg_ctr, 0, center);
  } else {
    return center;
  }
}


RegExpNode* RegExpAssertion::ToNode(RegExpCompiler* compiler,
                                    RegExpNode* on_success,
                                    RegExpNode* on_failure) {
  NodeInfo info;
  switch (type()) {
    case START_OF_LINE:
      info.follows_newline_interest = true;
      break;
    case START_OF_INPUT:
      info.follows_start_interest = true;
      break;
    case BOUNDARY: case NON_BOUNDARY:
      info.follows_word_interest = true;
      break;
    case END_OF_LINE: case END_OF_INPUT:
      // This is wrong but has the effect of making the compiler abort.
      info.follows_start_interest = true;
  }
  return on_success->PropagateInterest(&info);
}


RegExpNode* RegExpBackreference::ToNode(RegExpCompiler* compiler,
                                        RegExpNode* on_success,
                                        RegExpNode* on_failure) {
  return new BackreferenceNode(RegExpCapture::StartRegister(index()),
                               RegExpCapture::EndRegister(index()),
                               on_success,
                               on_failure);
}


RegExpNode* RegExpEmpty::ToNode(RegExpCompiler* compiler,
                                RegExpNode* on_success,
                                RegExpNode* on_failure) {
  return on_success;
}


RegExpNode* RegExpLookahead::ToNode(RegExpCompiler* compiler,
                                    RegExpNode* on_success,
                                    RegExpNode* on_failure) {
  int stack_pointer_register = compiler->AllocateRegister();
  int position_register = compiler->AllocateRegister();
  if (is_positive()) {
    // begin submatch scope
    // $reg = $pos
    // if [body]
    // then
    //   $pos = $reg
    //   escape submatch scope (drop all backtracks created in scope)
    //   succeed
    // else
    //   end submatch scope (nothing to clean up, just exit the scope)
    //   fail
    return ActionNode::BeginSubmatch(
        stack_pointer_register,
        ActionNode::SavePosition(
            position_register,
            body()->ToNode(
                compiler,
                ActionNode::RestorePosition(
                    position_register,
                    ActionNode::EscapeSubmatch(stack_pointer_register,
                                               on_success)),
                on_failure)));
  } else {
    // begin submatch scope
    // try
    // first if (body)
    //       then
    //         escape submatch scope
    //         fail
    //       else
    //         backtrack
    // second
    //       end submatch scope
    //       restore current position
    //       succeed
    ChoiceNode* try_node =
        new ChoiceNode(1, ActionNode::RestorePosition(position_register,
                                                      on_success));
    RegExpNode* body_node = body()->ToNode(
        compiler,
        ActionNode::EscapeSubmatch(stack_pointer_register, on_failure),
        compiler->backtrack());
    GuardedAlternative body_alt(body_node);
    try_node->AddAlternative(body_alt);
    return ActionNode::BeginSubmatch(stack_pointer_register,
                                     ActionNode::SavePosition(
                                         position_register,
                                         try_node));
  }
}


RegExpNode* RegExpCapture::ToNode(RegExpCompiler* compiler,
                                  RegExpNode* on_success,
                                  RegExpNode* on_failure) {
  return ToNode(body(), index(), compiler, on_success, on_failure);
}


RegExpNode* RegExpCapture::ToNode(RegExpTree* body,
                                  int index,
                                  RegExpCompiler* compiler,
                                  RegExpNode* on_success,
                                  RegExpNode* on_failure) {
  int start_reg = RegExpCapture::StartRegister(index);
  int end_reg = RegExpCapture::EndRegister(index);
  RegExpNode* store_end = ActionNode::StorePosition(end_reg, on_success);
  RegExpNode* body_node = body->ToNode(compiler, store_end, on_failure);
  return ActionNode::StorePosition(start_reg, body_node);
}


RegExpNode* RegExpAlternative::ToNode(RegExpCompiler* compiler,
                                      RegExpNode* on_success,
                                      RegExpNode* on_failure) {
  ZoneList<RegExpTree*>* children = nodes();
  RegExpNode* current = on_success;
  for (int i = children->length() - 1; i >= 0; i--) {
    current = children->at(i)->ToNode(compiler, current, on_failure);
  }
  return current;
}


static const int kSpaceRangeCount = 20;
static const uc16 kSpaceRanges[kSpaceRangeCount] = {
  0x0009, 0x000D, 0x0020, 0x0020, 0x00A0, 0x00A0, 0x1680,
  0x1680, 0x180E, 0x180E, 0x2000, 0x200A, 0x2028, 0x2029,
  0x202F, 0x202F, 0x205F, 0x205F, 0x3000, 0x3000
};


static const int kWordRangeCount = 8;
static const uc16 kWordRanges[kWordRangeCount] = {
  '0', '9', 'A', 'Z', '_', '_', 'a', 'z'
};


static const int kDigitRangeCount = 2;
static const uc16 kDigitRanges[kDigitRangeCount] = {
  '0', '9'
};


static const int kLineTerminatorRangeCount = 6;
static const uc16 kLineTerminatorRanges[kLineTerminatorRangeCount] = {
  0x000A, 0x000A, 0x000D, 0x000D, 0x2028, 0x2029
};


static void AddClass(const uc16* elmv,
                     int elmc,
                     ZoneList<CharacterRange>* ranges) {
  for (int i = 0; i < elmc; i += 2) {
    ASSERT(elmv[i] <= elmv[i + 1]);
    ranges->Add(CharacterRange(elmv[i], elmv[i + 1]));
  }
}


static void AddClassNegated(const uc16 *elmv,
                            int elmc,
                            ZoneList<CharacterRange>* ranges) {
  ASSERT(elmv[0] != 0x0000);
  ASSERT(elmv[elmc-1] != 0xFFFF);
  uc16 last = 0x0000;
  for (int i = 0; i < elmc; i += 2) {
    ASSERT(last <= elmv[i] - 1);
    ASSERT(elmv[i] <= elmv[i + 1]);
    ranges->Add(CharacterRange(last, elmv[i] - 1));
    last = elmv[i + 1] + 1;
  }
  ranges->Add(CharacterRange(last, 0xFFFF));
}


void CharacterRange::AddClassEscape(uc16 type,
                                    ZoneList<CharacterRange>* ranges) {
  switch (type) {
    case 's':
      AddClass(kSpaceRanges, kSpaceRangeCount, ranges);
      break;
    case 'S':
      AddClassNegated(kSpaceRanges, kSpaceRangeCount, ranges);
      break;
    case 'w':
      AddClass(kWordRanges, kWordRangeCount, ranges);
      break;
    case 'W':
      AddClassNegated(kWordRanges, kWordRangeCount, ranges);
      break;
    case 'd':
      AddClass(kDigitRanges, kDigitRangeCount, ranges);
      break;
    case 'D':
      AddClassNegated(kDigitRanges, kDigitRangeCount, ranges);
      break;
    case '.':
      AddClassNegated(kLineTerminatorRanges,
                      kLineTerminatorRangeCount,
                      ranges);
      break;
    // This is not a character range as defined by the spec but a
    // convenient shorthand for a character class that matches any
    // character.
    case '*':
      ranges->Add(CharacterRange::Everything());
      break;
    default:
      UNREACHABLE();
  }
}


// -------------------------------------------------------------------
// Interest propagation


RegExpNode* RegExpNode::GetSibling(NodeInfo* info) {
  for (int i = 0; i < siblings_.length(); i++) {
    RegExpNode* sibling = siblings_.Get(i);
    if (sibling->info()->SameInterests(info))
      return sibling;
  }
  return NULL;
}


template <class C>
static RegExpNode* PropagateToEndpoint(C* node, NodeInfo* info) {
  RegExpNode* sibling = node->GetSibling(info);
  if (sibling != NULL) return sibling;
  node->EnsureSiblings();
  sibling = new C(*node);
  sibling->info()->AdoptInterests(info);
  node->AddSibling(sibling);
  return sibling;
}


RegExpNode* ActionNode::PropagateInterest(NodeInfo* info) {
  RegExpNode* sibling = GetSibling(info);
  if (sibling != NULL) return sibling;
  EnsureSiblings();
  ActionNode* action = new ActionNode(*this);
  action->info()->AdoptInterests(info);
  AddSibling(action);
  action->set_on_success(action->on_success()->PropagateInterest(info));
  return action;
}


RegExpNode* ChoiceNode::PropagateInterest(NodeInfo* info) {
  RegExpNode* sibling = GetSibling(info);
  if (sibling != NULL) return sibling;
  EnsureSiblings();
  ChoiceNode* choice = new ChoiceNode(*this);
  choice->info()->AdoptInterests(info);
  AddSibling(choice);
  ZoneList<GuardedAlternative>* old_alternatives = alternatives();
  int count = old_alternatives->length();
  choice->alternatives_ = new ZoneList<GuardedAlternative>(count);
  for (int i = 0; i < count; i++) {
    GuardedAlternative alternative = old_alternatives->at(i);
    alternative.set_node(alternative.node()->PropagateInterest(info));
    choice->alternatives()->Add(alternative);
  }
  return choice;
}


RegExpNode* EndNode::PropagateInterest(NodeInfo* info) {
  return PropagateToEndpoint(this, info);
}


RegExpNode* BackreferenceNode::PropagateInterest(NodeInfo* info) {
  return PropagateToEndpoint(this, info);
}


RegExpNode* TextNode::PropagateInterest(NodeInfo* info) {
  return PropagateToEndpoint(this, info);
}


// -------------------------------------------------------------------
// Splay tree


OutSet* OutSet::Extend(unsigned value) {
  if (Get(value))
    return this;
  if (successors() != NULL) {
    for (int i = 0; i < successors()->length(); i++) {
      OutSet* successor = successors()->at(i);
      if (successor->Get(value))
        return successor;
    }
  } else {
    successors_ = new ZoneList<OutSet*>(2);
  }
  OutSet* result = new OutSet(first_, remaining_);
  result->Set(value);
  successors()->Add(result);
  return result;
}


void OutSet::Set(unsigned value) {
  if (value < kFirstLimit) {
    first_ |= (1 << value);
  } else {
    if (remaining_ == NULL)
      remaining_ = new ZoneList<unsigned>(1);
    if (remaining_->is_empty() || !remaining_->Contains(value))
      remaining_->Add(value);
  }
}


bool OutSet::Get(unsigned value) {
  if (value < kFirstLimit) {
    return (first_ & (1 << value)) != 0;
  } else if (remaining_ == NULL) {
    return false;
  } else {
    return remaining_->Contains(value);
  }
}


const uc16 DispatchTable::Config::kNoKey = unibrow::Utf8::kBadChar;
const DispatchTable::Entry DispatchTable::Config::kNoValue;


void DispatchTable::AddRange(CharacterRange full_range, int value) {
  CharacterRange current = full_range;
  if (tree()->is_empty()) {
    // If this is the first range we just insert into the table.
    ZoneSplayTree<Config>::Locator loc;
    ASSERT_RESULT(tree()->Insert(current.from(), &loc));
    loc.set_value(Entry(current.from(), current.to(), empty()->Extend(value)));
    return;
  }
  // First see if there is a range to the left of this one that
  // overlaps.
  ZoneSplayTree<Config>::Locator loc;
  if (tree()->FindGreatestLessThan(current.from(), &loc)) {
    Entry* entry = &loc.value();
    // If we've found a range that overlaps with this one, and it
    // starts strictly to the left of this one, we have to fix it
    // because the following code only handles ranges that start on
    // or after the start point of the range we're adding.
    if (entry->from() < current.from() && entry->to() >= current.from()) {
      // Snap the overlapping range in half around the start point of
      // the range we're adding.
      CharacterRange left(entry->from(), current.from() - 1);
      CharacterRange right(current.from(), entry->to());
      // The left part of the overlapping range doesn't overlap.
      // Truncate the whole entry to be just the left part.
      entry->set_to(left.to());
      // The right part is the one that overlaps.  We add this part
      // to the map and let the next step deal with merging it with
      // the range we're adding.
      ZoneSplayTree<Config>::Locator loc;
      ASSERT_RESULT(tree()->Insert(right.from(), &loc));
      loc.set_value(Entry(right.from(),
                          right.to(),
                          entry->out_set()));
    }
  }
  while (current.is_valid()) {
    if (tree()->FindLeastGreaterThan(current.from(), &loc) &&
        (loc.value().from() <= current.to()) &&
        (loc.value().to() >= current.from())) {
      Entry* entry = &loc.value();
      // We have overlap.  If there is space between the start point of
      // the range we're adding and where the overlapping range starts
      // then we have to add a range covering just that space.
      if (current.from() < entry->from()) {
        ZoneSplayTree<Config>::Locator ins;
        ASSERT_RESULT(tree()->Insert(current.from(), &ins));
        ins.set_value(Entry(current.from(),
                            entry->from() - 1,
                            empty()->Extend(value)));
        current.set_from(entry->from());
      }
      ASSERT_EQ(current.from(), entry->from());
      // If the overlapping range extends beyond the one we want to add
      // we have to snap the right part off and add it separately.
      if (entry->to() > current.to()) {
        ZoneSplayTree<Config>::Locator ins;
        ASSERT_RESULT(tree()->Insert(current.to() + 1, &ins));
        ins.set_value(Entry(current.to() + 1,
                            entry->to(),
                            entry->out_set()));
        entry->set_to(current.to());
      }
      ASSERT(entry->to() <= current.to());
      // The overlapping range is now completely contained by the range
      // we're adding so we can just update it and move the start point
      // of the range we're adding just past it.
      entry->AddValue(value);
      // Bail out if the last interval ended at 0xFFFF since otherwise
      // adding 1 will wrap around to 0.
      if (entry->to() == 0xFFFF)
        break;
      ASSERT(entry->to() + 1 > current.from());
      current.set_from(entry->to() + 1);
    } else {
      // There is no overlap so we can just add the range
      ZoneSplayTree<Config>::Locator ins;
      ASSERT_RESULT(tree()->Insert(current.from(), &ins));
      ins.set_value(Entry(current.from(),
                          current.to(),
                          empty()->Extend(value)));
      break;
    }
  }
}


OutSet* DispatchTable::Get(uc16 value) {
  ZoneSplayTree<Config>::Locator loc;
  if (!tree()->FindGreatestLessThan(value, &loc))
    return empty();
  Entry* entry = &loc.value();
  if (value <= entry->to())
    return entry->out_set();
  else
    return empty();
}


// -------------------------------------------------------------------
// Analysis


void Analysis::EnsureAnalyzed(RegExpNode* that) {
  if (that->info()->been_analyzed || that->info()->being_analyzed)
    return;
  that->info()->being_analyzed = true;
  that->Accept(this);
  that->info()->being_analyzed = false;
  that->info()->been_analyzed = true;
}


void Analysis::VisitEnd(EndNode* that) {
  // nothing to do
}


void Analysis::VisitText(TextNode* that) {
  EnsureAnalyzed(that->on_success());
  EnsureAnalyzed(that->on_failure());
}


void Analysis::VisitAction(ActionNode* that) {
  RegExpNode* next = that->on_success();
  EnsureAnalyzed(next);
  that->info()->determine_newline = next->info()->prev_determine_newline();
  that->info()->determine_word = next->info()->prev_determine_word();
  that->info()->determine_start = next->info()->prev_determine_start();
}


void Analysis::VisitChoice(ChoiceNode* that) {
  NodeInfo* info = that->info();
  for (int i = 0; i < that->alternatives()->length(); i++) {
    RegExpNode* node = that->alternatives()->at(i).node();
    EnsureAnalyzed(node);
    info->determine_newline |= node->info()->prev_determine_newline();
    info->determine_word |= node->info()->prev_determine_word();
    info->determine_start |= node->info()->prev_determine_start();
  }
  if (!that->table_calculated()) {
    DispatchTableConstructor cons(that->table());
    cons.BuildTable(that);
  }
  EnsureAnalyzed(that->on_failure());
}


void Analysis::VisitBackreference(BackreferenceNode* that) {
  EnsureAnalyzed(that->on_success());
  EnsureAnalyzed(that->on_failure());
}


// -------------------------------------------------------------------
// Dispatch table construction


void DispatchTableConstructor::VisitEnd(EndNode* that) {
  AddRange(CharacterRange::Everything());
}


void DispatchTableConstructor::BuildTable(ChoiceNode* node) {
  ASSERT(!node->table_calculated());
  node->set_being_calculated(true);
  ZoneList<GuardedAlternative>* alternatives = node->alternatives();
  for (int i = 0; i < alternatives->length(); i++) {
    set_choice_index(i);
    alternatives->at(i).node()->Accept(this);
  }
  node->set_being_calculated(false);
  node->set_table_calculated(true);
}


class AddDispatchRange {
 public:
  explicit AddDispatchRange(DispatchTableConstructor* constructor)
    : constructor_(constructor) { }
  void Call(uc32 from, DispatchTable::Entry entry);
 private:
  DispatchTableConstructor* constructor_;
};


void AddDispatchRange::Call(uc32 from, DispatchTable::Entry entry) {
  CharacterRange range(from, entry.to());
  constructor_->AddRange(range);
}


void DispatchTableConstructor::VisitChoice(ChoiceNode* node) {
  if (node->being_calculated())
    return;
  if (!node->table_calculated()) {
    DispatchTableConstructor constructor(node->table());
    constructor.BuildTable(node);
  }
  ASSERT(node->table_calculated());
  AddDispatchRange adder(this);
  node->table()->ForEach(&adder);
}


void DispatchTableConstructor::VisitBackreference(BackreferenceNode* that) {
  // TODO(plesner): What should this do?
}



static int CompareRangeByFrom(const CharacterRange* a,
                              const CharacterRange* b) {
  return Spaceship<uc16>(a->from(), b->from());
}


void DispatchTableConstructor::AddInverse(ZoneList<CharacterRange>* ranges) {
  ranges->Sort(CompareRangeByFrom);
  uc16 last = 0;
  for (int i = 0; i < ranges->length(); i++) {
    CharacterRange range = ranges->at(i);
    if (last < range.from())
      AddRange(CharacterRange(last, range.from() - 1));
    if (range.to() >= last) {
      if (range.to() == 0xFFFF) {
        return;
      } else {
        last = range.to() + 1;
      }
    }
  }
  AddRange(CharacterRange(last, 0xFFFF));
}


void DispatchTableConstructor::VisitText(TextNode* that) {
  TextElement elm = that->elements()->at(0);
  switch (elm.type) {
    case TextElement::ATOM: {
      uc16 c = elm.data.u_atom->data()[0];
      AddRange(CharacterRange(c, c));
      break;
    }
    case TextElement::CHAR_CLASS: {
      RegExpCharacterClass* tree = elm.data.u_char_class;
      ZoneList<CharacterRange>* ranges = tree->ranges();
      if (tree->is_negated()) {
        AddInverse(ranges);
      } else {
        for (int i = 0; i < ranges->length(); i++)
          AddRange(ranges->at(i));
      }
      break;
    }
    default: {
      UNIMPLEMENTED();
    }
  }
}


void DispatchTableConstructor::VisitAction(ActionNode* that) {
  that->on_success()->Accept(this);
}


Handle<FixedArray> RegExpEngine::Compile(RegExpParseResult* input,
                                         RegExpNode** node_return,
                                         bool ignore_case) {
  RegExpCompiler compiler(input->capture_count);
  // Wrap the body of the regexp in capture #0.
  RegExpNode* captured_body = RegExpCapture::ToNode(input->tree,
                                                    0,
                                                    &compiler,
                                                    compiler.accept(),
                                                    compiler.backtrack());
  // Add a .*? at the beginning, outside the body capture.
  // Note: We could choose to not add this if the regexp is anchored at
  //   the start of the input but I'm not sure how best to do that and
  //   since we don't even handle ^ yet I'm saving that optimization for
  //   later.
  RegExpNode* node = RegExpQuantifier::ToNode(0,
                                              RegExpQuantifier::kInfinity,
                                              false,
                                              new RegExpCharacterClass('*'),
                                              &compiler,
                                              captured_body,
                                              compiler.backtrack());
  if (node_return != NULL) *node_return = node;
  Analysis analysis;
  analysis.EnsureAnalyzed(node);
  byte codes[1024];
  Re2kAssembler assembler(Vector<byte>(codes, 1024));
  RegExpMacroAssemblerRe2k macro_assembler(&assembler);
  return compiler.Assemble(&macro_assembler,
                           node,
                           input->capture_count,
                           ignore_case);
}

RegExpMacroAssembler::RegExpMacroAssembler() {
}

RegExpMacroAssembler::~RegExpMacroAssembler() {
}


}}  // namespace v8::internal
