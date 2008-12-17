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
#include "regexp-macro-assembler.h"
#include "regexp-macro-assembler-tracer.h"
#include "regexp-macro-assembler-irregexp.h"

#ifdef ARM
#include "regexp-macro-assembler-arm.h"
#else  // IA32
#include "macro-assembler-ia32.h"
#include "regexp-macro-assembler-ia32.h"
#endif

#include "interpreter-irregexp.h"

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


// Generic RegExp methods. Dispatches to implementation specific methods.


class OffsetsVector {
 public:
  inline OffsetsVector(int num_registers)
      : offsets_vector_length_(num_registers) {
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


Handle<Object> RegExpImpl::Compile(Handle<JSRegExp> re,
                                   Handle<String> pattern,
                                   Handle<String> flag_str) {
  JSRegExp::Flags flags = RegExpFlagsFromString(flag_str);
  Handle<FixedArray> cached = CompilationCache::LookupRegExp(pattern, flags);
  bool in_cache = !cached.is_null();
  LOG(RegExpCompileEvent(re, in_cache));

  Handle<Object> result;
  if (in_cache) {
    re->set_data(*cached);
    result = re;
  } else {
    FlattenString(pattern);
    ZoneScope zone_scope(DELETE_ON_EXIT);
    RegExpCompileData parse_result;
    FlatStringReader reader(pattern);
    if (!ParseRegExp(&reader, flags.is_multiline(), &parse_result)) {
      // Throw an exception if we fail to parse the pattern.
      ThrowRegExpException(re,
                           pattern,
                           parse_result.error,
                           "malformed_regexp");
      return Handle<Object>::null();
    }

    if (parse_result.simple && !flags.is_ignore_case()) {
      // Parse-tree is a single atom that is equal to the pattern.
      result = AtomCompile(re, pattern, flags, pattern);
    } else if (parse_result.tree->IsAtom() &&
        !flags.is_ignore_case() &&
        parse_result.capture_count == 0) {
      // TODO(lrn) Accept capture_count > 0 on atoms.
      RegExpAtom* atom = parse_result.tree->AsAtom();
      Vector<const uc16> atom_pattern = atom->data();
      Handle<String> atom_string =
          Factory::NewStringFromTwoByte(atom_pattern);
      result = AtomCompile(re, pattern, flags, atom_string);
    } else if (FLAG_irregexp) {
      result = IrregexpPrepare(re, pattern, flags);
    } else {
      result = JscrePrepare(re, pattern, flags);
    }
    Object* data = re->data();
    if (data->IsFixedArray()) {
      // If compilation succeeded then the data is set on the regexp
      // and we can store it in the cache.
      Handle<FixedArray> data(FixedArray::cast(re->data()));
      CompilationCache::PutRegExp(pattern, flags, data);
    }
  }

  return result;
}


Handle<Object> RegExpImpl::Exec(Handle<JSRegExp> regexp,
                                Handle<String> subject,
                                Handle<Object> index) {
  switch (regexp->TypeTag()) {
    case JSRegExp::ATOM:
      return AtomExec(regexp, subject, index);
    case JSRegExp::IRREGEXP: {
      Handle<Object> result = IrregexpExec(regexp, subject, index);
      if (!result.is_null() || Top::has_pending_exception()) {
        return result;
      }
      // We couldn't handle the regexp using Irregexp, so fall back
      // on JSCRE.
      // Reset the JSRegExp to use JSCRE.
      JscrePrepare(regexp,
                   Handle<String>(regexp->Pattern()),
                   regexp->GetFlags());
      // Fall-through to JSCRE.
    }
    case JSRegExp::JSCRE:
      if (FLAG_disable_jscre) {
        UNIMPLEMENTED();
      }
      return JscreExec(regexp, subject, index);
    default:
      UNREACHABLE();
      return Handle<Object>::null();
  }
}


Handle<Object> RegExpImpl::ExecGlobal(Handle<JSRegExp> regexp,
                                Handle<String> subject) {
  switch (regexp->TypeTag()) {
    case JSRegExp::ATOM:
      return AtomExecGlobal(regexp, subject);
    case JSRegExp::IRREGEXP: {
      Handle<Object> result = IrregexpExecGlobal(regexp, subject);
      if (!result.is_null() || Top::has_pending_exception()) {
        return result;
      }
      // Empty handle as result but no exception thrown means that
      // the regexp contains features not yet handled by the irregexp
      // compiler.
      // We have to fall back on JSCRE. Reset the JSRegExp to use JSCRE.
      JscrePrepare(regexp,
                   Handle<String>(regexp->Pattern()),
                   regexp->GetFlags());
      // Fall-through to JSCRE.
    }
    case JSRegExp::JSCRE:
      if (FLAG_disable_jscre) {
        UNIMPLEMENTED();
      }
      return JscreExecGlobal(regexp, subject);
    default:
      UNREACHABLE();
      return Handle<Object>::null();
  }
}


// RegExp Atom implementation: Simple string search using indexOf.


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


// JSCRE implementation.


int RegExpImpl::JscreNumberOfCaptures(Handle<JSRegExp> re) {
  FixedArray* value = FixedArray::cast(re->DataAt(JSRegExp::kJscreDataIndex));
  return Smi::cast(value->get(kJscreNumberOfCapturesIndex))->value();
}


ByteArray* RegExpImpl::JscreInternal(Handle<JSRegExp> re) {
  FixedArray* value = FixedArray::cast(re->DataAt(JSRegExp::kJscreDataIndex));
  return ByteArray::cast(value->get(kJscreInternalIndex));
}


Handle<Object>RegExpImpl::JscrePrepare(Handle<JSRegExp> re,
                                       Handle<String> pattern,
                                       JSRegExp::Flags flags) {
  Handle<Object> value(Heap::undefined_value());
  Factory::SetRegExpData(re, JSRegExp::JSCRE, pattern, flags, value);
  return re;
}


static inline Object* JscreDoCompile(String* pattern,
                                     JSRegExp::Flags flags,
                                     unsigned* number_of_captures,
                                     const char** error_message,
                                     v8::jscre::JscreRegExp** code) {
  v8::jscre::JSRegExpIgnoreCaseOption case_option = flags.is_ignore_case()
    ? v8::jscre::JSRegExpIgnoreCase
    : v8::jscre::JSRegExpDoNotIgnoreCase;
  v8::jscre::JSRegExpMultilineOption multiline_option = flags.is_multiline()
    ? v8::jscre::JSRegExpMultiline
    : v8::jscre::JSRegExpSingleLine;
  *error_message = NULL;
  malloc_failure = Failure::Exception();
  *code = v8::jscre::jsRegExpCompile(pattern->GetTwoByteData(),
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


static void JscreCompileWithRetryAfterGC(Handle<String> pattern,
                                         JSRegExp::Flags flags,
                                         unsigned* number_of_captures,
                                         const char** error_message,
                                         v8::jscre::JscreRegExp** code) {
  CALL_HEAP_FUNCTION_VOID(JscreDoCompile(*pattern,
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

  v8::jscre::JscreRegExp* code = NULL;
  FlattenString(pattern);

  JscreCompileWithRetryAfterGC(two_byte_pattern,
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


Handle<Object> RegExpImpl::JscreExec(Handle<JSRegExp> regexp,
                                     Handle<String> subject,
                                     Handle<Object> index) {
  ASSERT_EQ(regexp->TypeTag(), JSRegExp::JSCRE);
  if (regexp->DataAt(JSRegExp::kJscreDataIndex)->IsUndefined()) {
    Handle<Object> compile_result = JscreCompile(regexp);
    if (compile_result.is_null()) return compile_result;
  }
  ASSERT(regexp->DataAt(JSRegExp::kJscreDataIndex)->IsFixedArray());

  int num_captures = JscreNumberOfCaptures(regexp);

  OffsetsVector offsets((num_captures + 1) * 3);

  int previous_index = static_cast<int>(DoubleToInteger(index->Number()));

  Handle<String> subject16 = CachedStringToTwoByte(subject);

  return JscreExecOnce(regexp,
                       num_captures,
                       subject,
                       previous_index,
                       subject16->GetTwoByteData(),
                       offsets.vector(),
                       offsets.length());
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
    const v8::jscre::JscreRegExp* js_regexp =
        reinterpret_cast<v8::jscre::JscreRegExp*>(
            internal->GetDataStartAddress());

    LOG(RegExpExecEvent(regexp, previous_index, subject));

    rc = v8::jscre::jsRegExpExecute(js_regexp,
                                    two_byte_subject,
                                    subject->length(),
                                    previous_index,
                                    offsets_vector,
                                    offsets_vector_length);
  }

  // The KJS JavaScript engine returns null (ie, a failed match) when
  // JSRE's internal match limit is exceeded.  We duplicate that behavior here.
  if (rc == v8::jscre::JSRegExpErrorNoMatch
      || rc == v8::jscre::JSRegExpErrorHitLimit) {
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


Handle<Object> RegExpImpl::JscreExecGlobal(Handle<JSRegExp> regexp,
                                           Handle<String> subject) {
  ASSERT_EQ(regexp->TypeTag(), JSRegExp::JSCRE);
  if (regexp->DataAt(JSRegExp::kJscreDataIndex)->IsUndefined()) {
    Handle<Object> compile_result = JscreCompile(regexp);
    if (compile_result.is_null()) return compile_result;
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
  if (matches->IsNull()) {
    // Exited loop normally.
    return result;
  } else {
    // Exited loop with the exception in matches.
    return matches;
  }
}


// Irregexp implementation.


// Retrieves a compiled version of the regexp for either ASCII or non-ASCII
// strings. If the compiled version doesn't already exist, it is compiled
// from the source pattern.
// Irregexp is not feature complete yet. If there is something in the
// regexp that the compiler cannot currently handle, an empty
// handle is returned, but no exception is thrown.
static Handle<FixedArray> GetCompiledIrregexp(Handle<JSRegExp> re,
                                              bool is_ascii) {
  ASSERT(re->DataAt(JSRegExp::kIrregexpDataIndex)->IsFixedArray());
  Handle<FixedArray> alternatives(
      FixedArray::cast(re->DataAt(JSRegExp::kIrregexpDataIndex)));
  ASSERT_EQ(2, alternatives->length());

  int index = is_ascii ? 0 : 1;
  Object* entry = alternatives->get(index);
  if (!entry->IsNull()) {
    return Handle<FixedArray>(FixedArray::cast(entry));
  }

  // Compile the RegExp.
  ZoneScope zone_scope(DELETE_ON_EXIT);

  JSRegExp::Flags flags = re->GetFlags();

  Handle<String> pattern(re->Pattern());
  StringShape shape(*pattern);
  if (!pattern->IsFlat(shape)) {
    pattern->Flatten(shape);
  }

  RegExpCompileData compile_data;
  FlatStringReader reader(pattern);
  if (!ParseRegExp(&reader, flags.is_multiline(), &compile_data)) {
    // Throw an exception if we fail to parse the pattern.
    // THIS SHOULD NOT HAPPEN. We already parsed it successfully once.
    ThrowRegExpException(re,
                         pattern,
                         compile_data.error,
                         "malformed_regexp");
    return Handle<FixedArray>::null();
  }
  Handle<FixedArray> compiled_entry =
      RegExpEngine::Compile(&compile_data,
                            flags.is_ignore_case(),
                            flags.is_multiline(),
                            pattern,
                            is_ascii);
  if (!compiled_entry.is_null()) {
    alternatives->set(index, *compiled_entry);
  }
  return compiled_entry;
}


int RegExpImpl::IrregexpNumberOfCaptures(Handle<FixedArray> irre) {
  return Smi::cast(irre->get(kIrregexpNumberOfCapturesIndex))->value();
}


int RegExpImpl::IrregexpNumberOfRegisters(Handle<FixedArray> irre) {
  return Smi::cast(irre->get(kIrregexpNumberOfRegistersIndex))->value();
}


Handle<ByteArray> RegExpImpl::IrregexpByteCode(Handle<FixedArray> irre) {
  ASSERT(Smi::cast(irre->get(kIrregexpImplementationIndex))->value()
      == RegExpMacroAssembler::kBytecodeImplementation);
  return Handle<ByteArray>(ByteArray::cast(irre->get(kIrregexpCodeIndex)));
}


Handle<Code> RegExpImpl::IrregexpNativeCode(Handle<FixedArray> irre) {
  ASSERT(Smi::cast(irre->get(kIrregexpImplementationIndex))->value()
      != RegExpMacroAssembler::kBytecodeImplementation);
  return Handle<Code>(Code::cast(irre->get(kIrregexpCodeIndex)));
}


Handle<Object>RegExpImpl::IrregexpPrepare(Handle<JSRegExp> re,
                                          Handle<String> pattern,
                                          JSRegExp::Flags flags) {
  // Make space for ASCII and UC16 versions.
  Handle<FixedArray> alternatives = Factory::NewFixedArray(2);
  alternatives->set_null(0);
  alternatives->set_null(1);
  Factory::SetRegExpData(re, JSRegExp::IRREGEXP, pattern, flags, alternatives);
  return re;
}


Handle<Object> RegExpImpl::IrregexpExec(Handle<JSRegExp> regexp,
                                        Handle<String> subject,
                                        Handle<Object> index) {
  ASSERT_EQ(regexp->TypeTag(), JSRegExp::IRREGEXP);
  ASSERT(regexp->DataAt(JSRegExp::kIrregexpDataIndex)->IsFixedArray());

  bool is_ascii = StringShape(*subject).IsAsciiRepresentation();
  Handle<FixedArray> irregexp = GetCompiledIrregexp(regexp, is_ascii);
  if (irregexp.is_null()) {
    // We can't handle the RegExp with IRRegExp.
    return Handle<Object>::null();
  }

  // Prepare space for the return values.
  int number_of_registers = IrregexpNumberOfRegisters(irregexp);
  OffsetsVector offsets(number_of_registers);

  int num_captures = IrregexpNumberOfCaptures(irregexp);

  int previous_index = static_cast<int>(DoubleToInteger(index->Number()));

#ifdef DEBUG
  if (FLAG_trace_regexp_bytecodes) {
    String* pattern = regexp->Pattern();
    PrintF("\n\nRegexp match:   /%s/\n\n", *(pattern->ToCString()));
    PrintF("\n\nSubject string: '%s'\n\n", *(subject->ToCString()));
  }
#endif
  LOG(RegExpExecEvent(regexp, previous_index, subject));
  return IrregexpExecOnce(irregexp,
                          num_captures,
                          subject,
                          previous_index,
                          offsets.vector(),
                          offsets.length());
}


Handle<Object> RegExpImpl::IrregexpExecGlobal(Handle<JSRegExp> regexp,
                                              Handle<String> subject) {
  ASSERT_EQ(regexp->TypeTag(), JSRegExp::IRREGEXP);

  StringShape shape(*subject);
  bool is_ascii = shape.IsAsciiRepresentation();
  Handle<FixedArray> irregexp = GetCompiledIrregexp(regexp, is_ascii);
  if (irregexp.is_null()) {
    return Handle<Object>::null();
  }

  // Prepare space for the return values.
  int number_of_registers = IrregexpNumberOfRegisters(irregexp);
  OffsetsVector offsets(number_of_registers);

  int previous_index = 0;

  Handle<JSArray> result = Factory::NewJSArray(0);
  int i = 0;
  Handle<Object> matches;

  if (!subject->IsFlat(shape)) {
    subject->Flatten(shape);
  }

  do {
    if (previous_index > subject->length() || previous_index < 0) {
      // Per ECMA-262 15.10.6.2, if the previous index is greater than the
      // string length, there is no match.
      matches = Factory::null_value();
    } else {
#ifdef DEBUG
      if (FLAG_trace_regexp_bytecodes) {
        String* pattern = regexp->Pattern();
        PrintF("\n\nRegexp match:   /%s/\n\n", *(pattern->ToCString()));
        PrintF("\n\nSubject string: '%s'\n\n", *(subject->ToCString()));
      }
#endif
      LOG(RegExpExecEvent(regexp, previous_index, subject));
      matches = IrregexpExecOnce(irregexp,
                                 IrregexpNumberOfCaptures(irregexp),
                                 subject,
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
  if (matches->IsNull()) {
    // Exited loop normally.
    return result;
  } else {
    // Exited loop with the exception in matches.
    return matches;
  }
}


Handle<Object> RegExpImpl::IrregexpExecOnce(Handle<FixedArray> irregexp,
                                            int num_captures,
                                            Handle<String> subject,
                                            int previous_index,
                                            int* offsets_vector,
                                            int offsets_vector_length) {
  bool rc;

  int tag = Smi::cast(irregexp->get(kIrregexpImplementationIndex))->value();

  if (!subject->IsFlat(StringShape(*subject))) {
    FlattenString(subject);
  }

  switch (tag) {
    case RegExpMacroAssembler::kIA32Implementation: {
#ifndef ARM
      Handle<Code> code = IrregexpNativeCode(irregexp);

      StringShape shape(*subject);

      // Character offsets into string.
      int start_offset = previous_index;
      int end_offset = subject->length(shape);

      if (shape.IsCons()) {
        subject = Handle<String>(ConsString::cast(*subject)->first());
      } else if (shape.IsSliced()) {
        SlicedString* slice = SlicedString::cast(*subject);
        start_offset += slice->start();
        end_offset += slice->start();
        subject = Handle<String>(slice->buffer());
      }

      // String is now either Sequential or External
      StringShape flatshape(*subject);
      bool is_ascii = flatshape.IsAsciiRepresentation();
      int char_size_shift = is_ascii ? 0 : 1;

      RegExpMacroAssemblerIA32::Result res;

      if (flatshape.IsExternal()) {
        const byte* address;
        if (is_ascii) {
          ExternalAsciiString* ext = ExternalAsciiString::cast(*subject);
          address = reinterpret_cast<const byte*>(ext->resource()->data());
        } else {
          ExternalTwoByteString* ext = ExternalTwoByteString::cast(*subject);
          address = reinterpret_cast<const byte*>(ext->resource()->data());
        }
        res = RegExpMacroAssemblerIA32::Execute(
            *code,
            &address,
            start_offset << char_size_shift,
            end_offset << char_size_shift,
            offsets_vector,
            previous_index == 0);
      } else {  // Sequential string
        Address char_address =
            is_ascii ? SeqAsciiString::cast(*subject)->GetCharsAddress()
                     : SeqTwoByteString::cast(*subject)->GetCharsAddress();
        int byte_offset = char_address - reinterpret_cast<Address>(*subject);
        res = RegExpMacroAssemblerIA32::Execute(
            *code,
            subject.location(),
            byte_offset + (start_offset << char_size_shift),
            byte_offset + (end_offset << char_size_shift),
            offsets_vector,
            previous_index == 0);
      }

      if (res == RegExpMacroAssemblerIA32::EXCEPTION) {
        ASSERT(Top::has_pending_exception());
        return Handle<Object>::null();
      }
      rc = (res == RegExpMacroAssemblerIA32::SUCCESS);

      if (rc) {
        // Capture values are relative to start_offset only.
        for (int i = 0; i < offsets_vector_length; i++) {
          if (offsets_vector[i] >= 0) {
            offsets_vector[i] += previous_index;
          }
        }
      }
      break;
#else
      UNIMPLEMENTED();
      rc = false;
      break;
#endif
    }
    case RegExpMacroAssembler::kBytecodeImplementation: {
      for (int i = (num_captures + 1) * 2 - 1; i >= 0; i--) {
        offsets_vector[i] = -1;
      }
      Handle<ByteArray> byte_codes = IrregexpByteCode(irregexp);

      rc = IrregexpInterpreter::Match(byte_codes,
                                      subject,
                                      offsets_vector,
                                      previous_index);
      break;
    }
    case RegExpMacroAssembler::kARMImplementation:
    default:
      UNREACHABLE();
      rc = false;
      break;
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


// -------------------------------------------------------------------
// Implmentation of the Irregexp regular expression engine.
//
// The Irregexp regular expression engine is intended to be a complete
// implementation of ECMAScript regular expressions.  It generates either
// bytecodes or native code.

//   The Irregexp regexp engine is structured in three steps.
//   1) The parser generates an abstract syntax tree.  See ast.cc.
//   2) From the AST a node network is created.  The nodes are all
//      subclasses of RegExpNode.  The nodes represent states when
//      executing a regular expression.  Several optimizations are
//      performed on the node network.
//   3) From the nodes we generate either byte codes or native code
//      that can actually execute the regular expression (perform
//      the search).  The code generation step is described in more
//      detail below.

// Code generation.
//
//   The nodes are divided into four main categories.
//   * Choice nodes
//        These represent places where the regular expression can
//        match in more than one way.  For example on entry to an
//        alternation (foo|bar) or a repetition (*, +, ? or {}).
//   * Action nodes
//        These represent places where some action should be
//        performed.  Examples include recording the current position
//        in the input string to a register (in order to implement
//        captures) or other actions on register for example in order
//        to implement the counters needed for {} repetitions.
//   * Matching nodes
//        These attempt to match some element part of the input string.
//        Examples of elements include character classes, plain strings
//        or back references.
//   * End nodes
//        These are used to implement the actions required on finding
//        a successful match or failing to find a match.
//
//   The code generated (whether as byte codes or native code) maintains
//   some state as it runs.  This consists of the following elements:
//
//   * The capture registers.  Used for string captures.
//   * Other registers.  Used for counters etc.
//   * The current position.
//   * The stack of backtracking information.  Used when a matching node
//     fails to find a match and needs to try an alternative.
//
// Conceptual regular expression execution model:
//
//   There is a simple conceptual model of regular expression execution
//   which will be presented first.  The actual code generated is a more
//   efficient simulation of the simple conceptual model:
//
//   * Choice nodes are implemented as follows:
//     For each choice except the last {
//       push current position
//       push backtrack code location
//       <generate code to test for choice>
//       backtrack code location:
//       pop current position
//     }
//     <generate code to test for last choice>
//
//   * Actions nodes are generated as follows
//     <push affected registers on backtrack stack>
//     <generate code to perform action>
//     push backtrack code location
//     <generate code to test for following nodes>
//     backtrack code location:
//     <pop affected registers to restore their state>
//     <pop backtrack location from stack and go to it>
//
//   * Matching nodes are generated as follows:
//     if input string matches at current position
//       update current position
//       <generate code to test for following nodes>
//     else
//       <pop backtrack location from stack and go to it>
//
//   Thus it can be seen that the current position is saved and restored
//   by the choice nodes, whereas the registers are saved and restored by
//   by the action nodes that manipulate them.
//
//   The other interesting aspect of this model is that nodes are generated
//   at the point where they are needed by a recursive call to Emit().  If
//   the node has already been code generated then the Emit() call will
//   generate a jump to the previously generated code instead.  In order to
//   limit recursion it is possible for the Emit() function to put the node
//   on a work list for later generation and instead generate a jump.  The
//   destination of the jump is resolved later when the code is generated.
//
// Actual regular expression code generation.
//
//   Code generation is actually more complicated than the above.  In order
//   to improve the efficiency of the generated code some optimizations are
//   performed
//
//   * Choice nodes have 1-character lookahead.
//     A choice node looks at the following character and eliminates some of
//     the choices immediately based on that character.  This is not yet
//     implemented.
//   * Simple greedy loops store reduced backtracking information.
//     A quantifier like /.*foo/m will greedily match the whole input.  It will
//     then need to backtrack to a point where it can match "foo".  The naive
//     implementation of this would push each character position onto the
//     backtracking stack, then pop them off one by one.  This would use space
//     proportional to the length of the input string.  However since the "."
//     can only match in one way and always has a constant length (in this case
//     of 1) it suffices to store the current position on the top of the stack
//     once.  Matching now becomes merely incrementing the current position and
//     backtracking becomes decrementing the current position and checking the
//     result against the stored current position.  This is faster and saves
//     space.
//   * The current state is virtualized.
//     This is used to defer expensive operations until it is clear that they
//     are needed and to generate code for a node more than once, allowing
//     specialized an efficient versions of the code to be created. This is
//     explained in the section below.
//
// Execution state virtualization.
//
//   Instead of emitting code, nodes that manipulate the state can record their
//   manipulation in an object called the GenerationVariant.  The
//   GenerationVariant object can record a current position offset, an
//   optional backtrack code location on the top of the virtualized backtrack
//   stack and some register changes.  When a node is to be emitted it can flush
//   the GenerationVariant or update it.  Flushing the GenerationVariant
//   will emit code to bring the actual state into line with the virtual state.
//   Avoiding flushing the state can postpone some work (eg updates of capture
//   registers).  Postponing work can save time when executing the regular
//   expression since it may be found that the work never has to be done as a
//   failure to match can occur.  In addition it is much faster to jump to a
//   known backtrack code location than it is to pop an unknown backtrack
//   location from the stack and jump there.
//
//   The virtual state found in the GenerationVariant affects code generation.
//   For example the virtual state contains the difference between the actual
//   current position and the virtual current position, and matching code needs
//   to use this offset to attempt a match in the correct location of the input
//   string.  Therefore code generated for a non-trivial GenerationVariant is
//   specialized to that GenerationVariant.  The code generator therefore
//   has the ability to generate code for each node several times.  In order to
//   limit the size of the generated code there is an arbitrary limit on how
//   many specialized sets of code may be generated for a given node.  If the
//   limit is reached, the GenerationVariant is flushed and a generic version of
//   the code for a node is emitted.  This is subsequently used for that node.
//   The code emitted for non-generic GenerationVariants is not recorded in the
//   node and so it cannot currently be reused in the event that code generation
//   is requested for an identical GenerationVariant.


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


int TextElement::length() {
  if (type == ATOM) {
    return data.u_atom->length();
  } else {
    ASSERT(type == CHAR_CLASS);
    return 1;
  }
}


DispatchTable* ChoiceNode::GetTable(bool ignore_case) {
  if (table_ == NULL) {
    table_ = new DispatchTable();
    DispatchTableConstructor cons(table_, ignore_case);
    cons.BuildTable(this);
  }
  return table_;
}


class RegExpCompiler {
 public:
  RegExpCompiler(int capture_count, bool ignore_case, bool is_ascii);

  int AllocateRegister() { return next_register_++; }

  Handle<FixedArray> Assemble(RegExpMacroAssembler* assembler,
                              RegExpNode* start,
                              int capture_count,
                              Handle<String> pattern);

  inline void AddWork(RegExpNode* node) { work_list_->Add(node); }

  static const int kImplementationOffset = 0;
  static const int kNumberOfRegistersOffset = 0;
  static const int kCodeOffset = 1;

  RegExpMacroAssembler* macro_assembler() { return macro_assembler_; }
  EndNode* accept() { return accept_; }

  static const int kMaxRecursion = 100;
  inline int recursion_depth() { return recursion_depth_; }
  inline void IncrementRecursionDepth() { recursion_depth_++; }
  inline void DecrementRecursionDepth() { recursion_depth_--; }

  inline bool ignore_case() { return ignore_case_; }
  inline bool ascii() { return ascii_; }

 private:
  EndNode* accept_;
  int next_register_;
  List<RegExpNode*>* work_list_;
  int recursion_depth_;
  RegExpMacroAssembler* macro_assembler_;
  bool ignore_case_;
  bool ascii_;
};


class RecursionCheck {
 public:
  explicit RecursionCheck(RegExpCompiler* compiler) : compiler_(compiler) {
    compiler->IncrementRecursionDepth();
  }
  ~RecursionCheck() { compiler_->DecrementRecursionDepth(); }
 private:
  RegExpCompiler* compiler_;
};


// Attempts to compile the regexp using an Irregexp code generator.  Returns
// a fixed array or a null handle depending on whether it succeeded.
RegExpCompiler::RegExpCompiler(int capture_count, bool ignore_case, bool ascii)
    : next_register_(2 * (capture_count + 1)),
      work_list_(NULL),
      recursion_depth_(0),
      ignore_case_(ignore_case),
      ascii_(ascii) {
  accept_ = new EndNode(EndNode::ACCEPT);
}


Handle<FixedArray> RegExpCompiler::Assemble(
    RegExpMacroAssembler* macro_assembler,
    RegExpNode* start,
    int capture_count,
    Handle<String> pattern) {
#ifdef DEBUG
  if (FLAG_trace_regexp_assembler)
    macro_assembler_ = new RegExpMacroAssemblerTracer(macro_assembler);
  else
#endif
    macro_assembler_ = macro_assembler;
  List <RegExpNode*> work_list(0);
  work_list_ = &work_list;
  Label fail;
  macro_assembler->PushBacktrack(&fail);
  GenerationVariant generic_variant;
  if (!start->Emit(this, &generic_variant)) {
    fail.Unuse();
    return Handle<FixedArray>::null();
  }
  macro_assembler_->Bind(&fail);
  macro_assembler_->Fail();
  while (!work_list.is_empty()) {
    if (!work_list.RemoveLast()->Emit(this, &generic_variant)) {
      return Handle<FixedArray>::null();
    }
  }
  Handle<FixedArray> array =
      Factory::NewFixedArray(RegExpImpl::kIrregexpDataLength);
  array->set(RegExpImpl::kIrregexpImplementationIndex,
             Smi::FromInt(macro_assembler_->Implementation()));
  array->set(RegExpImpl::kIrregexpNumberOfRegistersIndex,
             Smi::FromInt(next_register_));
  array->set(RegExpImpl::kIrregexpNumberOfCapturesIndex,
             Smi::FromInt(capture_count));
  Handle<Object> code = macro_assembler_->GetCode(pattern);
  array->set(RegExpImpl::kIrregexpCodeIndex, *code);
  work_list_ = NULL;
#ifdef DEBUG
  if (FLAG_trace_regexp_assembler) {
    delete macro_assembler_;
  }
#endif
  return array;
}


bool GenerationVariant::mentions_reg(int reg) {
  for (DeferredAction* action = actions_;
       action != NULL;
       action = action->next()) {
    if (reg == action->reg()) return true;
  }
  return false;
}


int GenerationVariant::FindAffectedRegisters(OutSet* affected_registers) {
  int max_register = -1;
  for (DeferredAction* action = actions_;
       action != NULL;
       action = action->next()) {
    affected_registers->Set(action->reg());
    if (action->reg() > max_register) max_register = action->reg();
  }
  return max_register;
}


void GenerationVariant::PushAffectedRegisters(RegExpMacroAssembler* macro,
                                              int max_register,
                                              OutSet& affected_registers) {
  for (int reg = 0; reg <= max_register; reg++) {
    if (affected_registers.Get(reg)) macro->PushRegister(reg);
  }
}


void GenerationVariant::RestoreAffectedRegisters(RegExpMacroAssembler* macro,
                                                 int max_register,
                                                 OutSet& affected_registers) {
  for (int reg = max_register; reg >= 0; reg--) {
    if (affected_registers.Get(reg)) macro->PopRegister(reg);
  }
}


void GenerationVariant::PerformDeferredActions(RegExpMacroAssembler* macro,
                                               int max_register,
                                               OutSet& affected_registers) {
  for (int reg = 0; reg <= max_register; reg++) {
    if (!affected_registers.Get(reg)) {
      continue;
    }
    int value = 0;
    bool absolute = false;
    int store_position = -1;
    // This is a little tricky because we are scanning the actions in reverse
    // historical order (newest first).
    for (DeferredAction* action = actions_;
         action != NULL;
         action = action->next()) {
      if (action->reg() == reg) {
        switch (action->type()) {
          case ActionNode::SET_REGISTER: {
            GenerationVariant::DeferredSetRegister* psr =
                static_cast<GenerationVariant::DeferredSetRegister*>(action);
            value += psr->value();
            absolute = true;
            ASSERT_EQ(store_position, -1);
            break;
          }
          case ActionNode::INCREMENT_REGISTER:
            if (!absolute) {
              value++;
            }
            ASSERT_EQ(store_position, -1);
            break;
          case ActionNode::STORE_POSITION: {
            GenerationVariant::DeferredCapture* pc =
                static_cast<GenerationVariant::DeferredCapture*>(action);
            if (store_position == -1) {
              store_position = pc->cp_offset();
            }
            ASSERT(!absolute);
            ASSERT_EQ(value, 0);
            break;
          }
          default:
            UNREACHABLE();
            break;
        }
      }
    }
    if (store_position != -1) {
      macro->WriteCurrentPositionToRegister(reg, store_position);
    } else {
      if (absolute) {
        macro->SetRegister(reg, value);
      } else {
        if (value != 0) {
          macro->AdvanceRegister(reg, value);
        }
      }
    }
  }
}


// This is called as we come into a loop choice node and some other tricky
// nodes.  It normalises the state of the code generator to ensure we can
// generate generic code.
bool GenerationVariant::Flush(RegExpCompiler* compiler, RegExpNode* successor) {
  RegExpMacroAssembler* macro = compiler->macro_assembler();

  ASSERT(actions_ != NULL || cp_offset_ != 0 || backtrack() != NULL);

  if (actions_ == NULL && backtrack() == NULL) {
    // Here we just have some deferred cp advances to fix and we are back to
    // a normal situation.
    macro->AdvanceCurrentPosition(cp_offset_);
    // Create a new trivial state and generate the node with that.
    GenerationVariant new_state;
    return successor->Emit(compiler, &new_state);
  }

  // Generate deferred actions here along with code to undo them again.
  OutSet affected_registers;
  int max_register = FindAffectedRegisters(&affected_registers);
  PushAffectedRegisters(macro, max_register, affected_registers);
  PerformDeferredActions(macro, max_register, affected_registers);
  if (backtrack() != NULL) {
    // Here we have a concrete backtrack location.  These are set up by choice
    // nodes and so they indicate that we have a deferred save of the current
    // position which we may need to emit here.
    macro->PushCurrentPosition();
  }
  if (cp_offset_ != 0) {
    macro->AdvanceCurrentPosition(cp_offset_);
  }

  // Create a new trivial state and generate the node with that.
  Label undo;
  macro->PushBacktrack(&undo);
  GenerationVariant new_state;
  bool ok = successor->Emit(compiler, &new_state);

  // On backtrack we need to restore state.
  macro->Bind(&undo);
  if (!ok) return false;
  if (backtrack() != NULL) {
    macro->PopCurrentPosition();
  }
  RestoreAffectedRegisters(macro, max_register, affected_registers);
  if (backtrack() == NULL) {
    macro->Backtrack();
  } else {
    macro->GoTo(backtrack());
  }

  return true;
}


void EndNode::EmitInfoChecks(RegExpMacroAssembler* macro,
                             GenerationVariant* variant) {
  if (info()->at_end) {
    Label succeed;
    // LoadCurrentCharacter will go to the label if we are at the end of the
    // input string.
    macro->LoadCurrentCharacter(0, &succeed);
    macro->GoTo(variant->backtrack());
    macro->Bind(&succeed);
  }
}


bool NegativeSubmatchSuccess::Emit(RegExpCompiler* compiler,
                                   GenerationVariant* variant) {
  if (!variant->is_trivial()) {
    return variant->Flush(compiler, this);
  }
  RegExpMacroAssembler* macro = compiler->macro_assembler();
  if (!label()->is_bound()) {
    macro->Bind(label());
  }
  EmitInfoChecks(macro, variant);
  macro->ReadCurrentPositionFromRegister(current_position_register_);
  macro->ReadStackPointerFromRegister(stack_pointer_register_);
  // Now that we have unwound the stack we find at the top of the stack the
  // backtrack that the BeginSubmatch node got.
  macro->Backtrack();
  return true;
}


bool EndNode::Emit(RegExpCompiler* compiler, GenerationVariant* variant) {
  if (!variant->is_trivial()) {
    return variant->Flush(compiler, this);
  }
  RegExpMacroAssembler* macro = compiler->macro_assembler();
  if (!label()->is_bound()) {
    macro->Bind(label());
  }
  switch (action_) {
    case ACCEPT:
      EmitInfoChecks(macro, variant);
      macro->Succeed();
      return true;
    case BACKTRACK:
      ASSERT(!info()->at_end);
      macro->GoTo(variant->backtrack());
      return true;
    case NEGATIVE_SUBMATCH_SUCCESS:
      // This case is handled in a different virtual method.
      UNREACHABLE();
  }
  UNIMPLEMENTED();
  return false;
}


void GuardedAlternative::AddGuard(Guard* guard) {
  if (guards_ == NULL)
    guards_ = new ZoneList<Guard*>(1);
  guards_->Add(guard);
}


ActionNode* ActionNode::SetRegister(int reg,
                                    int val,
                                    RegExpNode* on_success) {
  ActionNode* result = new ActionNode(SET_REGISTER, on_success);
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


ActionNode* ActionNode::BeginSubmatch(int stack_reg,
                                      int position_reg,
                                      RegExpNode* on_success) {
  ActionNode* result = new ActionNode(BEGIN_SUBMATCH, on_success);
  result->data_.u_submatch.stack_pointer_register = stack_reg;
  result->data_.u_submatch.current_position_register = position_reg;
  return result;
}


ActionNode* ActionNode::PositiveSubmatchSuccess(int stack_reg,
                                                int position_reg,
                                                RegExpNode* on_success) {
  ActionNode* result = new ActionNode(POSITIVE_SUBMATCH_SUCCESS, on_success);
  result->data_.u_submatch.stack_pointer_register = stack_reg;
  result->data_.u_submatch.current_position_register = position_reg;
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
                               GenerationVariant* variant) {
  switch (guard->op()) {
    case Guard::LT:
      ASSERT(!variant->mentions_reg(guard->reg()));
      macro_assembler->IfRegisterGE(guard->reg(),
                                    guard->value(),
                                    variant->backtrack());
      break;
    case Guard::GEQ:
      ASSERT(!variant->mentions_reg(guard->reg()));
      macro_assembler->IfRegisterLT(guard->reg(),
                                    guard->value(),
                                    variant->backtrack());
      break;
  }
}


static unibrow::Mapping<unibrow::Ecma262UnCanonicalize> uncanonicalize;
static unibrow::Mapping<unibrow::CanonicalizationRange> canonrange;


static inline void EmitAtomNonLetters(
    RegExpMacroAssembler* macro_assembler,
    TextElement elm,
    Vector<const uc16> quarks,
    Label* on_failure,
    int cp_offset,
    bool check_offset) {
  unibrow::uchar chars[unibrow::Ecma262UnCanonicalize::kMaxWidth];
  // It is vital that this loop is backwards due to the unchecked character
  // load below.
  for (int i = quarks.length() - 1; i >= 0; i--) {
    uc16 c = quarks[i];
    int length = uncanonicalize.get(c, '\0', chars);
    if (length <= 1) {
      if (check_offset && i == quarks.length() - 1) {
        macro_assembler->LoadCurrentCharacter(cp_offset + i, on_failure);
      } else {
        // Here we don't need to check against the end of the input string
        // since this character lies before a character that matched.
        macro_assembler->LoadCurrentCharacterUnchecked(cp_offset + i);
      }
      macro_assembler->CheckNotCharacter(c, on_failure);
    }
  }
}


static bool ShortCutEmitCharacterPair(RegExpMacroAssembler* macro_assembler,
                                      uc16 c1,
                                      uc16 c2,
                                      Label* on_failure) {
  uc16 exor = c1 ^ c2;
  // Check whether exor has only one bit set.
  if (((exor - 1) & exor) == 0) {
    // If c1 and c2 differ only by one bit.
    // Ecma262UnCanonicalize always gives the highest number last.
    ASSERT(c2 > c1);
    macro_assembler->CheckNotCharacterAfterOr(c2, exor, on_failure);
    return true;
  }
  ASSERT(c2 > c1);
  uc16 diff = c2 - c1;
  if (((diff - 1) & diff) == 0 && c1 >= diff) {
    // If the characters differ by 2^n but don't differ by one bit then
    // subtract the difference from the found character, then do the or
    // trick.  We avoid the theoretical case where negative numbers are
    // involved in order to simplify code generation.
    macro_assembler->CheckNotCharacterAfterMinusOr(c2 - diff,
                                                   diff,
                                                   on_failure);
    return true;
  }
  return false;
}


static inline void EmitAtomLetters(
    RegExpMacroAssembler* macro_assembler,
    TextElement elm,
    Vector<const uc16> quarks,
    Label* on_failure,
    int cp_offset,
    bool check_offset) {
  unibrow::uchar chars[unibrow::Ecma262UnCanonicalize::kMaxWidth];
  // It is vital that this loop is backwards due to the unchecked character
  // load below.
  for (int i = quarks.length() - 1; i >= 0; i--) {
    uc16 c = quarks[i];
    int length = uncanonicalize.get(c, '\0', chars);
    if (length <= 1) continue;
    if (check_offset && i == quarks.length() - 1) {
      macro_assembler->LoadCurrentCharacter(cp_offset + i, on_failure);
    } else {
      // Here we don't need to check against the end of the input string
      // since this character lies before a character that matched.
      macro_assembler->LoadCurrentCharacterUnchecked(cp_offset + i);
    }
    Label ok;
    ASSERT(unibrow::Ecma262UnCanonicalize::kMaxWidth == 4);
    switch (length) {
      case 2: {
        if (ShortCutEmitCharacterPair(macro_assembler,
                                      chars[0],
                                      chars[1],
                                      on_failure)) {
        } else {
          macro_assembler->CheckCharacter(chars[0], &ok);
          macro_assembler->CheckNotCharacter(chars[1], on_failure);
          macro_assembler->Bind(&ok);
        }
        break;
      }
      case 4:
        macro_assembler->CheckCharacter(chars[3], &ok);
        // Fall through!
      case 3:
        macro_assembler->CheckCharacter(chars[0], &ok);
        macro_assembler->CheckCharacter(chars[1], &ok);
        macro_assembler->CheckNotCharacter(chars[2], on_failure);
        macro_assembler->Bind(&ok);
        break;
      default:
        UNREACHABLE();
        break;
    }
  }
}


static void EmitCharClass(RegExpMacroAssembler* macro_assembler,
                          RegExpCharacterClass* cc,
                          int cp_offset,
                          Label* on_failure,
                          bool check_offset,
                          bool ascii) {
  ZoneList<CharacterRange>* ranges = cc->ranges();
  int max_char;
  if (ascii) {
    max_char = String::kMaxAsciiCharCode;
  } else {
    max_char = String::kMaxUC16CharCode;
  }

  Label success;

  Label* char_is_in_class =
      cc->is_negated() ? on_failure : &success;

  int range_count = ranges->length();

  int last_valid_range = range_count - 1;
  while (last_valid_range >= 0) {
    CharacterRange& range = ranges->at(last_valid_range);
    if (range.from() <= max_char) {
      break;
    }
    last_valid_range--;
  }

  if (last_valid_range < 0) {
    if (!cc->is_negated()) {
      // TODO(plesner): We can remove this when the node level does our
      // ASCII optimizations for us.
      macro_assembler->GoTo(on_failure);
    }
    return;
  }

  if (last_valid_range == 0 &&
      !cc->is_negated() &&
      ranges->at(0).IsEverything(max_char)) {
    // This is a common case hit by non-anchored expressions.
    // TODO(erikcorry): We should have a macro assembler instruction that just
    // checks for end of string without loading the character.
    if (check_offset) {
      macro_assembler->LoadCurrentCharacter(cp_offset, on_failure);
    }
    return;
  }

  if (check_offset) {
    macro_assembler->LoadCurrentCharacter(cp_offset, on_failure);
  } else {
    // Here we don't need to check against the end of the input string
    // since this character lies before a character that matched.
    macro_assembler->LoadCurrentCharacterUnchecked(cp_offset);
  }

  for (int i = 0; i <= last_valid_range; i++) {
    CharacterRange& range = ranges->at(i);
    Label next_range;
    uc16 from = range.from();
    uc16 to = range.to();
    if (from > max_char) {
      continue;
    }
    if (to > max_char) to = max_char;
    if (to == from) {
      macro_assembler->CheckCharacter(to, char_is_in_class);
    } else {
      if (from != 0) {
        macro_assembler->CheckCharacterLT(from, &next_range);
      }
      if (to != max_char) {
        macro_assembler->CheckCharacterLT(to + 1, char_is_in_class);
      } else {
        macro_assembler->GoTo(char_is_in_class);
      }
    }
    macro_assembler->Bind(&next_range);
  }

  CharacterRange& range = ranges->at(last_valid_range);
  uc16 from = range.from();
  uc16 to = range.to();

  if (to > max_char) to = max_char;
  ASSERT(to >= from);

  if (to == from) {
    if (cc->is_negated()) {
      macro_assembler->CheckCharacter(to, on_failure);
    } else {
      macro_assembler->CheckNotCharacter(to, on_failure);
    }
  } else {
    if (from != 0) {
      if (cc->is_negated()) {
        macro_assembler->CheckCharacterLT(from, &success);
      } else {
        macro_assembler->CheckCharacterLT(from, on_failure);
      }
    }
    if (to != String::kMaxUC16CharCode) {
      if (cc->is_negated()) {
        macro_assembler->CheckCharacterLT(to + 1, on_failure);
      } else {
        macro_assembler->CheckCharacterGT(to, on_failure);
      }
    } else {
      if (cc->is_negated()) {
        macro_assembler->GoTo(on_failure);
      }
    }
  }
  macro_assembler->Bind(&success);
}


RegExpNode::LimitResult RegExpNode::LimitVersions(RegExpCompiler* compiler,
                                                  GenerationVariant* variant) {
  // TODO(erikcorry): Implement support.
  if (info_.follows_word_interest ||
      info_.follows_newline_interest ||
      info_.follows_start_interest) {
    return FAIL;
  }

  // If we are generating a greedy loop then don't stop and don't reuse code.
  if (variant->stop_node() != NULL) {
    return CONTINUE;
  }

  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  if (variant->is_trivial()) {
    if (label_.is_bound()) {
      // We are being asked to generate a generic version, but that's already
      // been done so just go to it.
      macro_assembler->GoTo(&label_);
      return DONE;
    }
    if (compiler->recursion_depth() >= RegExpCompiler::kMaxRecursion) {
      // To avoid too deep recursion we push the node to the work queue and just
      // generate a goto here.
      compiler->AddWork(this);
      macro_assembler->GoTo(&label_);
      return DONE;
    }
    // Generate generic version of the node and bind the label for later use.
    macro_assembler->Bind(&label_);
    return CONTINUE;
  }

  // We are being asked to make a non-generic version.  Keep track of how many
  // non-generic versions we generate so as not to overdo it.
  variants_generated_++;
  if (variants_generated_ < kMaxVariantsGenerated &&
      compiler->recursion_depth() <= RegExpCompiler::kMaxRecursion) {
    return CONTINUE;
  }

  // If we get here there have been too many variants generated or recursion
  // is too deep.  Time to switch to a generic version.  The code for
  // generic versions above can handle deep recursion properly.
  bool ok = variant->Flush(compiler, this);
  return ok ? DONE : FAIL;
}


// This generates the code to match a text node.  A text node can contain
// straight character sequences (possibly to be matched in a case-independent
// way) and character classes.  In order to be most efficient we test for the
// simple things first and then move on to the more complicated things.  The
// simplest thing is a non-letter or a letter if we are matching case.  The
// next-most simple thing is a case-independent letter.  The least simple is
// a character class.  Another optimization is that we test the last one first.
// If that succeeds we don't need to test for the end of the string when we
// load other characters.
bool TextNode::Emit(RegExpCompiler* compiler, GenerationVariant* variant) {
  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  Label *backtrack = variant->backtrack();
  LimitResult limit_result = LimitVersions(compiler, variant);
  if (limit_result == FAIL) return false;
  if (limit_result == DONE) return true;
  ASSERT(limit_result == CONTINUE);

  int element_count = elms_->length();
  ASSERT(element_count != 0);
  if (info()->at_end) {
    macro_assembler->GoTo(backtrack);
    return true;
  }
  // First check for non-ASCII text.
  // TODO(plesner): We should do this at node level.
  if (compiler->ascii()) {
    for (int i = element_count - 1; i >= 0; i--) {
      TextElement elm = elms_->at(i);
      if (elm.type == TextElement::ATOM) {
        Vector<const uc16> quarks = elm.data.u_atom->data();
        for (int j = quarks.length() - 1; j >= 0; j--) {
          if (quarks[j] > String::kMaxAsciiCharCode) {
            macro_assembler->GoTo(backtrack);
            return true;
          }
        }
      } else {
        ASSERT_EQ(elm.type, TextElement::CHAR_CLASS);
      }
    }
  }
  // Second, handle straight character matches.
  int checked_up_to = -1;
  for (int i = element_count - 1; i >= 0; i--) {
    TextElement elm = elms_->at(i);
    ASSERT(elm.cp_offset >= 0);
    int cp_offset = variant->cp_offset() + elm.cp_offset;
    if (elm.type == TextElement::ATOM) {
      Vector<const uc16> quarks = elm.data.u_atom->data();
      int last_cp_offset = cp_offset + quarks.length();
      if (compiler->ignore_case()) {
        EmitAtomNonLetters(macro_assembler,
                           elm,
                           quarks,
                           backtrack,
                           cp_offset,
                           checked_up_to < last_cp_offset);
      } else {
        macro_assembler->CheckCharacters(quarks,
                                         cp_offset,
                                         backtrack,
                                         checked_up_to < last_cp_offset);
      }
      if (last_cp_offset > checked_up_to) checked_up_to = last_cp_offset - 1;
    } else {
      ASSERT_EQ(elm.type, TextElement::CHAR_CLASS);
    }
  }
  // Third, handle case independent letter matches if any.
  if (compiler->ignore_case()) {
    for (int i = element_count - 1; i >= 0; i--) {
      TextElement elm = elms_->at(i);
      int cp_offset = variant->cp_offset() + elm.cp_offset;
      if (elm.type == TextElement::ATOM) {
        Vector<const uc16> quarks = elm.data.u_atom->data();
        int last_cp_offset = cp_offset + quarks.length();
        EmitAtomLetters(macro_assembler,
                        elm,
                        quarks,
                        backtrack,
                        cp_offset,
                        checked_up_to < last_cp_offset);
        if (last_cp_offset > checked_up_to) checked_up_to = last_cp_offset - 1;
      }
    }
  }
  // If the fast character matches passed then do the character classes.
  for (int i = element_count - 1; i >= 0; i--) {
    TextElement elm = elms_->at(i);
    int cp_offset = variant->cp_offset() + elm.cp_offset;
    if (elm.type == TextElement::CHAR_CLASS) {
      RegExpCharacterClass* cc = elm.data.u_char_class;
      EmitCharClass(macro_assembler,
                    cc,
                    cp_offset,
                    backtrack,
                    checked_up_to < cp_offset,
                    compiler->ascii());
      if (cp_offset > checked_up_to) checked_up_to = cp_offset;
    }
  }

  GenerationVariant new_variant(*variant);
  new_variant.set_cp_offset(checked_up_to + 1);
  RecursionCheck rc(compiler);
  return on_success()->Emit(compiler, &new_variant);
}


void TextNode::MakeCaseIndependent() {
  int element_count = elms_->length();
  for (int i = 0; i < element_count; i++) {
    TextElement elm = elms_->at(i);
    if (elm.type == TextElement::CHAR_CLASS) {
      RegExpCharacterClass* cc = elm.data.u_char_class;
      ZoneList<CharacterRange>* ranges = cc->ranges();
      int range_count = ranges->length();
      for (int i = 0; i < range_count; i++) {
        ranges->at(i).AddCaseEquivalents(ranges);
      }
    }
  }
}


int TextNode::GreedyLoopTextLength() {
  TextElement elm = elms_->at(elms_->length() - 1);
  if (elm.type == TextElement::CHAR_CLASS) {
    return elm.cp_offset + 1;
  } else {
    return elm.cp_offset + elm.data.u_atom->data().length();
  }
}


// Finds the fixed match length of a sequence of nodes that goes from
// this alternative and back to this choice node.  If there are variable
// length nodes or other complications in the way then return a sentinel
// value indicating that a greedy loop cannot be constructed.
int ChoiceNode::GreedyLoopTextLength(GuardedAlternative* alternative) {
  int length = 0;
  RegExpNode* node = alternative->node();
  // Later we will generate code for all these text nodes using recursion
  // so we have to limit the max number.
  int recursion_depth = 0;
  while (node != this) {
    if (recursion_depth++ > RegExpCompiler::kMaxRecursion) {
      return kNodeIsTooComplexForGreedyLoops;
    }
    NodeInfo* info = node->info();
    if (info->follows_word_interest ||
        info->follows_newline_interest ||
        info->follows_start_interest) {
      return kNodeIsTooComplexForGreedyLoops;
    }
    int node_length = node->GreedyLoopTextLength();
    if (node_length == kNodeIsTooComplexForGreedyLoops) {
      return kNodeIsTooComplexForGreedyLoops;
    }
    length += node_length;
    SeqRegExpNode* seq_node = static_cast<SeqRegExpNode*>(node);
    node = seq_node->on_success();
  }
  return length;
}


bool LoopChoiceNode::Emit(RegExpCompiler* compiler,
                          GenerationVariant* variant) {
  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  if (variant->stop_node() == this) {
    int text_length = GreedyLoopTextLength(&(alternatives_->at(0)));
    ASSERT(text_length != kNodeIsTooComplexForGreedyLoops);
    // Update the counter-based backtracking info on the stack.  This is an
    // optimization for greedy loops (see below).
    ASSERT(variant->cp_offset() == text_length);
    macro_assembler->AdvanceCurrentPosition(text_length);
    macro_assembler->GoTo(variant->loop_label());
    return true;
  }
  ASSERT(variant->stop_node() == NULL);
  if (!variant->is_trivial()) {
    return variant->Flush(compiler, this);
  }
  return ChoiceNode::Emit(compiler, variant);
}


bool ChoiceNode::Emit(RegExpCompiler* compiler, GenerationVariant* variant) {
  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  int choice_count = alternatives_->length();
#ifdef DEBUG
  for (int i = 0; i < choice_count - 1; i++) {
    GuardedAlternative alternative = alternatives_->at(i);
    ZoneList<Guard*>* guards = alternative.guards();
    int guard_count = (guards == NULL) ? 0 : guards->length();
    for (int j = 0; j < guard_count; j++) {
      ASSERT(!variant->mentions_reg(guards->at(j)->reg()));
    }
  }
#endif

  LimitResult limit_result = LimitVersions(compiler, variant);
  if (limit_result == DONE) return true;
  if (limit_result == FAIL) return false;
  ASSERT(limit_result == CONTINUE);

  RecursionCheck rc(compiler);

  GenerationVariant* current_variant = variant;

  int text_length = GreedyLoopTextLength(&(alternatives_->at(0)));
  bool greedy_loop = false;
  Label greedy_loop_label;
  GenerationVariant counter_backtrack_variant(&greedy_loop_label);
  if (choice_count > 1 && text_length != kNodeIsTooComplexForGreedyLoops) {
    // Here we have special handling for greedy loops containing only text nodes
    // and other simple nodes.  These are handled by pushing the current
    // position on the stack and then incrementing the current position each
    // time around the switch.  On backtrack we decrement the current position
    // and check it against the pushed value.  This avoids pushing backtrack
    // information for each iteration of the loop, which could take up a lot of
    // space.
    greedy_loop = true;
    ASSERT(variant->stop_node() == NULL);
    macro_assembler->PushCurrentPosition();
    current_variant = &counter_backtrack_variant;
    Label greedy_match_failed;
    GenerationVariant greedy_match_variant(&greedy_match_failed);
    Label loop_label;
    macro_assembler->Bind(&loop_label);
    greedy_match_variant.set_stop_node(this);
    greedy_match_variant.set_loop_label(&loop_label);
    bool ok = alternatives_->at(0).node()->Emit(compiler,
                                                &greedy_match_variant);
    macro_assembler->Bind(&greedy_match_failed);
    if (!ok) {
      greedy_loop_label.Unuse();
      return false;
    }
  }

  Label second_choice;  // For use in greedy matches.
  macro_assembler->Bind(&second_choice);

  // For now we just call all choices one after the other.  The idea ultimately
  // is to use the Dispatch table to try only the relevant ones.
  for (int i = greedy_loop ? 1 : 0; i < choice_count - 1; i++) {
    GuardedAlternative alternative = alternatives_->at(i);
    Label after;
    ZoneList<Guard*>* guards = alternative.guards();
    int guard_count = (guards == NULL) ? 0 : guards->length();
    GenerationVariant new_variant(*current_variant);
    new_variant.set_backtrack(&after);
    for (int j = 0; j < guard_count; j++) {
      GenerateGuard(macro_assembler, guards->at(j), &new_variant);
    }
    if (!alternative.node()->Emit(compiler, &new_variant)) {
      after.Unuse();
      return false;
    }
    macro_assembler->Bind(&after);
  }
  GuardedAlternative alternative = alternatives_->at(choice_count - 1);
  ZoneList<Guard*>* guards = alternative.guards();
  int guard_count = (guards == NULL) ? 0 : guards->length();
  for (int j = 0; j < guard_count; j++) {
    GenerateGuard(macro_assembler, guards->at(j), current_variant);
  }
  bool ok = alternative.node()->Emit(compiler, current_variant);
  if (!ok) return false;
  if (greedy_loop) {
    macro_assembler->Bind(&greedy_loop_label);
    // If we have unwound to the bottom then backtrack.
    macro_assembler->CheckGreedyLoop(variant->backtrack());
    // Otherwise try the second priority at an earlier position.
    macro_assembler->AdvanceCurrentPosition(-text_length);
    macro_assembler->GoTo(&second_choice);
  }
  return true;
}


bool ActionNode::Emit(RegExpCompiler* compiler, GenerationVariant* variant) {
  RegExpMacroAssembler* macro = compiler->macro_assembler();
  LimitResult limit_result = LimitVersions(compiler, variant);
  if (limit_result == DONE) return true;
  if (limit_result == FAIL) return false;
  ASSERT(limit_result == CONTINUE);

  RecursionCheck rc(compiler);

  switch (type_) {
    case STORE_POSITION: {
      GenerationVariant::DeferredCapture
          new_capture(data_.u_position_register.reg, variant);
      GenerationVariant new_variant = *variant;
      new_variant.add_action(&new_capture);
      return on_success()->Emit(compiler, &new_variant);
    }
    case INCREMENT_REGISTER: {
      GenerationVariant::DeferredIncrementRegister
          new_increment(data_.u_increment_register.reg);
      GenerationVariant new_variant = *variant;
      new_variant.add_action(&new_increment);
      return on_success()->Emit(compiler, &new_variant);
    }
    case SET_REGISTER: {
      GenerationVariant::DeferredSetRegister
          new_set(data_.u_store_register.reg, data_.u_store_register.value);
      GenerationVariant new_variant = *variant;
      new_variant.add_action(&new_set);
      return on_success()->Emit(compiler, &new_variant);
    }
    case BEGIN_SUBMATCH:
      if (!variant->is_trivial()) return variant->Flush(compiler, this);
      macro->WriteCurrentPositionToRegister(
          data_.u_submatch.current_position_register, 0);
      macro->WriteStackPointerToRegister(
          data_.u_submatch.stack_pointer_register);
      return on_success()->Emit(compiler, variant);
    case POSITIVE_SUBMATCH_SUCCESS:
      if (!variant->is_trivial()) return variant->Flush(compiler, this);
      // TODO(erikcorry): Implement support.
      if (info()->follows_word_interest ||
          info()->follows_newline_interest ||
          info()->follows_start_interest) {
        return false;
      }
      if (info()->at_end) {
        Label at_end;
        // Load current character jumps to the label if we are beyond the string
        // end.
        macro->LoadCurrentCharacter(0, &at_end);
        macro->GoTo(variant->backtrack());
        macro->Bind(&at_end);
      }
      macro->ReadCurrentPositionFromRegister(
          data_.u_submatch.current_position_register);
      macro->ReadStackPointerFromRegister(
          data_.u_submatch.stack_pointer_register);
      return on_success()->Emit(compiler, variant);
    default:
      UNREACHABLE();
      return false;
  }
}


bool BackReferenceNode::Emit(RegExpCompiler* compiler,
                             GenerationVariant* variant) {
  RegExpMacroAssembler* macro = compiler->macro_assembler();
  if (!variant->is_trivial()) {
    return variant->Flush(compiler, this);
  }

  LimitResult limit_result = LimitVersions(compiler, variant);
  if (limit_result == DONE) return true;
  if (limit_result == FAIL) return false;
  ASSERT(limit_result == CONTINUE);

  RecursionCheck rc(compiler);

  ASSERT_EQ(start_reg_ + 1, end_reg_);
  if (info()->at_end) {
    // If we are constrained to match at the end of the input then succeed
    // iff the back reference is empty.
    macro->CheckNotRegistersEqual(start_reg_, end_reg_, variant->backtrack());
  } else {
    if (compiler->ignore_case()) {
      macro->CheckNotBackReferenceIgnoreCase(start_reg_, variant->backtrack());
    } else {
      macro->CheckNotBackReference(start_reg_, variant->backtrack());
    }
  }
  return on_success()->Emit(compiler, variant);
}


// -------------------------------------------------------------------
// Dot/dotty output


#ifdef DEBUG


class DotPrinter: public NodeVisitor {
 public:
  explicit DotPrinter(bool ignore_case)
      : ignore_case_(ignore_case),
        stream_(&alloc_) { }
  void PrintNode(const char* label, RegExpNode* node);
  void Visit(RegExpNode* node);
  void PrintAttributes(RegExpNode* from);
  StringStream* stream() { return &stream_; }
  void PrintOnFailure(RegExpNode* from, RegExpNode* to);
#define DECLARE_VISIT(Type)                                          \
  virtual void Visit##Type(Type##Node* that);
FOR_EACH_NODE_TYPE(DECLARE_VISIT)
#undef DECLARE_VISIT
 private:
  bool ignore_case_;
  HeapStringAllocator alloc_;
  StringStream stream_;
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
  if (node->info()->visited) return;
  node->info()->visited = true;
  node->Accept(this);
}


void DotPrinter::PrintOnFailure(RegExpNode* from, RegExpNode* on_failure) {
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


class AttributePrinter {
 public:
  explicit AttributePrinter(DotPrinter* out)
      : out_(out), first_(true) { }
  void PrintSeparator() {
    if (first_) {
      first_ = false;
    } else {
      out_->stream()->Add("|");
    }
  }
  void PrintBit(const char* name, bool value) {
    if (!value) return;
    PrintSeparator();
    out_->stream()->Add("{%s}", name);
  }
  void PrintPositive(const char* name, int value) {
    if (value < 0) return;
    PrintSeparator();
    out_->stream()->Add("{%s|%x}", name, value);
  }
 private:
  DotPrinter* out_;
  bool first_;
};


void DotPrinter::PrintAttributes(RegExpNode* that) {
  stream()->Add("  a%p [shape=Mrecord, color=grey, fontcolor=grey, "
                "margin=0.1, fontsize=10, label=\"{",
                that);
  AttributePrinter printer(this);
  NodeInfo* info = that->info();
  printer.PrintBit("NI", info->follows_newline_interest);
  printer.PrintBit("WI", info->follows_word_interest);
  printer.PrintBit("SI", info->follows_start_interest);
  printer.PrintBit("DN", info->determine_newline);
  printer.PrintBit("DW", info->determine_word);
  printer.PrintBit("DS", info->determine_start);
  printer.PrintBit("DDN", info->does_determine_newline);
  printer.PrintBit("DDW", info->does_determine_word);
  printer.PrintBit("DDS", info->does_determine_start);
  printer.PrintPositive("IW", info->is_word);
  printer.PrintPositive("IN", info->is_newline);
  printer.PrintPositive("FN", info->follows_newline);
  printer.PrintPositive("FW", info->follows_word);
  printer.PrintPositive("FS", info->follows_start);
  Label* label = that->label();
  if (label->is_bound())
    printer.PrintPositive("@", label->pos());
  stream()->Add("}\"];\n");
  stream()->Add("  a%p -> n%p [style=dashed, color=grey, "
                "arrowhead=none];\n", that, that);
}


static const bool kPrintDispatchTable = false;
void DotPrinter::VisitChoice(ChoiceNode* that) {
  if (kPrintDispatchTable) {
    stream()->Add("  n%p [shape=Mrecord, label=\"", that);
    TableEntryHeaderPrinter header_printer(stream());
    that->GetTable(ignore_case_)->ForEach(&header_printer);
    stream()->Add("\"]\n", that);
    PrintAttributes(that);
    TableEntryBodyPrinter body_printer(stream(), that);
    that->GetTable(ignore_case_)->ForEach(&body_printer);
  } else {
    stream()->Add("  n%p [shape=Mrecord, label=\"?\"];\n", that);
    for (int i = 0; i < that->alternatives()->length(); i++) {
      GuardedAlternative alt = that->alternatives()->at(i);
      stream()->Add("  n%p -> n%p;\n", that, alt.node());
    }
  }
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
  PrintAttributes(that);
  stream()->Add("  n%p -> n%p;\n", that, that->on_success());
  Visit(that->on_success());
}


void DotPrinter::VisitBackReference(BackReferenceNode* that) {
  stream()->Add("  n%p [label=\"$%i..$%i\", shape=doubleoctagon];\n",
                that,
                that->start_register(),
                that->end_register());
  PrintAttributes(that);
  stream()->Add("  n%p -> n%p;\n", that, that->on_success());
  Visit(that->on_success());
}


void DotPrinter::VisitEnd(EndNode* that) {
  stream()->Add("  n%p [style=bold, shape=point];\n", that);
  PrintAttributes(that);
}


void DotPrinter::VisitAction(ActionNode* that) {
  stream()->Add("  n%p [", that);
  switch (that->type_) {
    case ActionNode::SET_REGISTER:
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
    case ActionNode::BEGIN_SUBMATCH:
      stream()->Add("label=\"$%i:=$pos,begin\", shape=septagon",
                    that->data_.u_submatch.current_position_register);
      break;
    case ActionNode::POSITIVE_SUBMATCH_SUCCESS:
      stream()->Add("label=\"escape\", shape=septagon");
      break;
  }
  stream()->Add("];\n");
  PrintAttributes(that);
  RegExpNode* successor = that->on_success();
  stream()->Add("  n%p -> n%p;\n", that, successor);
  Visit(successor);
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


void RegExpEngine::DotPrint(const char* label,
                            RegExpNode* node,
                            bool ignore_case) {
  DotPrinter printer(ignore_case);
  printer.PrintNode(label, node);
}


#endif  // DEBUG


// -------------------------------------------------------------------
// Tree to graph conversion


RegExpNode* RegExpAtom::ToNode(RegExpCompiler* compiler,
                               RegExpNode* on_success) {
  ZoneList<TextElement>* elms = new ZoneList<TextElement>(1);
  elms->Add(TextElement::Atom(this));
  return new TextNode(elms, on_success);
}


RegExpNode* RegExpText::ToNode(RegExpCompiler* compiler,
                               RegExpNode* on_success) {
  return new TextNode(elements(), on_success);
}


RegExpNode* RegExpCharacterClass::ToNode(RegExpCompiler* compiler,
                                         RegExpNode* on_success) {
  return new TextNode(this, on_success);
}


RegExpNode* RegExpDisjunction::ToNode(RegExpCompiler* compiler,
                                      RegExpNode* on_success) {
  ZoneList<RegExpTree*>* alternatives = this->alternatives();
  int length = alternatives->length();
  ChoiceNode* result = new ChoiceNode(length);
  for (int i = 0; i < length; i++) {
    GuardedAlternative alternative(alternatives->at(i)->ToNode(compiler,
                                                               on_success));
    result->AddAlternative(alternative);
  }
  return result;
}


RegExpNode* RegExpQuantifier::ToNode(RegExpCompiler* compiler,
                                     RegExpNode* on_success) {
  return ToNode(min(),
                max(),
                is_greedy(),
                body(),
                compiler,
                on_success);
}


RegExpNode* RegExpQuantifier::ToNode(int min,
                                     int max,
                                     bool is_greedy,
                                     RegExpTree* body,
                                     RegExpCompiler* compiler,
                                     RegExpNode* on_success) {
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
  bool has_max = max < RegExpTree::kInfinity;
  bool needs_counter = has_min || has_max;
  int reg_ctr = needs_counter ? compiler->AllocateRegister() : -1;
  ChoiceNode* center = new LoopChoiceNode(2);
  RegExpNode* loop_return = needs_counter
      ? static_cast<RegExpNode*>(ActionNode::IncrementRegister(reg_ctr, center))
      : static_cast<RegExpNode*>(center);
  RegExpNode* body_node = body->ToNode(compiler, loop_return);
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
    return ActionNode::SetRegister(reg_ctr, 0, center);
  } else {
    return center;
  }
}


RegExpNode* RegExpAssertion::ToNode(RegExpCompiler* compiler,
                                    RegExpNode* on_success) {
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
    case END_OF_INPUT:
      info.at_end = true;
      break;
    case END_OF_LINE:
      // This is wrong but has the effect of making the compiler abort.
      info.at_end = true;
  }
  return on_success->PropagateForward(&info);
}


RegExpNode* RegExpBackReference::ToNode(RegExpCompiler* compiler,
                                        RegExpNode* on_success) {
  return new BackReferenceNode(RegExpCapture::StartRegister(index()),
                               RegExpCapture::EndRegister(index()),
                               on_success);
}


RegExpNode* RegExpEmpty::ToNode(RegExpCompiler* compiler,
                                RegExpNode* on_success) {
  return on_success;
}


RegExpNode* RegExpLookahead::ToNode(RegExpCompiler* compiler,
                                    RegExpNode* on_success) {
  int stack_pointer_register = compiler->AllocateRegister();
  int position_register = compiler->AllocateRegister();
  RegExpNode* success;
  if (is_positive()) {
    return ActionNode::BeginSubmatch(
        stack_pointer_register,
        position_register,
        body()->ToNode(
            compiler,
            ActionNode::PositiveSubmatchSuccess(stack_pointer_register,
                                                position_register,
                                                on_success)));
  } else {
    // We use a ChoiceNode for a negative lookahead because it has most of
    // the characteristics we need.  It has the body of the lookahead as its
    // first alternative and the expression after the lookahead of the second
    // alternative.  If the first alternative succeeds then the
    // NegativeSubmatchSuccess will unwind the stack including everything the
    // choice node set up and backtrack.  If the first alternative fails then
    // the second alternative is tried, which is exactly the desired result
    // for a negative lookahead.  In the case where the dispatch table
    // determines that the first alternative cannot match we will save time
    // by not trying it.  Things are not quite so well-optimized if the
    // dispatch table determines that the second alternative cannot match.
    // In this case we could optimize by immediately backtracking.
    ChoiceNode* choice_node = new ChoiceNode(2);
    GuardedAlternative body_alt(
        body()->ToNode(
            compiler,
            success = new NegativeSubmatchSuccess(stack_pointer_register,
                                                  position_register)));
    choice_node->AddAlternative(body_alt);
    choice_node->AddAlternative(GuardedAlternative(on_success));
    return ActionNode::BeginSubmatch(stack_pointer_register,
                                     position_register,
                                     choice_node);
  }
}


RegExpNode* RegExpCapture::ToNode(RegExpCompiler* compiler,
                                  RegExpNode* on_success) {
  return ToNode(body(), index(), compiler, on_success);
}


RegExpNode* RegExpCapture::ToNode(RegExpTree* body,
                                  int index,
                                  RegExpCompiler* compiler,
                                  RegExpNode* on_success) {
  int start_reg = RegExpCapture::StartRegister(index);
  int end_reg = RegExpCapture::EndRegister(index);
  RegExpNode* store_end = ActionNode::StorePosition(end_reg, on_success);
  RegExpNode* body_node = body->ToNode(compiler, store_end);
  return ActionNode::StorePosition(start_reg, body_node);
}


RegExpNode* RegExpAlternative::ToNode(RegExpCompiler* compiler,
                                      RegExpNode* on_success) {
  ZoneList<RegExpTree*>* children = nodes();
  RegExpNode* current = on_success;
  for (int i = children->length() - 1; i >= 0; i--) {
    current = children->at(i)->ToNode(compiler, current);
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
  ASSERT(elmv[elmc-1] != String::kMaxUC16CharCode);
  uc16 last = 0x0000;
  for (int i = 0; i < elmc; i += 2) {
    ASSERT(last <= elmv[i] - 1);
    ASSERT(elmv[i] <= elmv[i + 1]);
    ranges->Add(CharacterRange(last, elmv[i] - 1));
    last = elmv[i + 1] + 1;
  }
  ranges->Add(CharacterRange(last, String::kMaxUC16CharCode));
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


Vector<const uc16> CharacterRange::GetWordBounds() {
  return Vector<const uc16>(kWordRanges, kWordRangeCount);
}


class CharacterRangeSplitter {
 public:
  CharacterRangeSplitter(ZoneList<CharacterRange>** included,
                          ZoneList<CharacterRange>** excluded)
      : included_(included),
        excluded_(excluded) { }
  void Call(uc16 from, DispatchTable::Entry entry);

  static const int kInBase = 0;
  static const int kInOverlay = 1;

 private:
  ZoneList<CharacterRange>** included_;
  ZoneList<CharacterRange>** excluded_;
};


void CharacterRangeSplitter::Call(uc16 from, DispatchTable::Entry entry) {
  if (!entry.out_set()->Get(kInBase)) return;
  ZoneList<CharacterRange>** target = entry.out_set()->Get(kInOverlay)
    ? included_
    : excluded_;
  if (*target == NULL) *target = new ZoneList<CharacterRange>(2);
  (*target)->Add(CharacterRange(entry.from(), entry.to()));
}


void CharacterRange::Split(ZoneList<CharacterRange>* base,
                           Vector<const uc16> overlay,
                           ZoneList<CharacterRange>** included,
                           ZoneList<CharacterRange>** excluded) {
  ASSERT_EQ(NULL, *included);
  ASSERT_EQ(NULL, *excluded);
  DispatchTable table;
  for (int i = 0; i < base->length(); i++)
    table.AddRange(base->at(i), CharacterRangeSplitter::kInBase);
  for (int i = 0; i < overlay.length(); i += 2) {
    table.AddRange(CharacterRange(overlay[i], overlay[i+1]),
                   CharacterRangeSplitter::kInOverlay);
  }
  CharacterRangeSplitter callback(included, excluded);
  table.ForEach(&callback);
}


void CharacterRange::AddCaseEquivalents(ZoneList<CharacterRange>* ranges) {
  unibrow::uchar chars[unibrow::Ecma262UnCanonicalize::kMaxWidth];
  if (IsSingleton()) {
    // If this is a singleton we just expand the one character.
    int length = uncanonicalize.get(from(), '\0', chars);
    for (int i = 0; i < length; i++) {
      uc32 chr = chars[i];
      if (chr != from()) {
        ranges->Add(CharacterRange::Singleton(chars[i]));
      }
    }
  } else if (from() <= kRangeCanonicalizeMax &&
             to() <= kRangeCanonicalizeMax) {
    // If this is a range we expand the characters block by block,
    // expanding contiguous subranges (blocks) one at a time.
    // The approach is as follows.  For a given start character we
    // look up the block that contains it, for instance 'a' if the
    // start character is 'c'.  A block is characterized by the property
    // that all characters uncanonicalize in the same way as the first
    // element, except that each entry in the result is incremented
    // by the distance from the first element.  So a-z is a block
    // because 'a' uncanonicalizes to ['a', 'A'] and the k'th letter
    // uncanonicalizes to ['a' + k, 'A' + k].
    // Once we've found the start point we look up its uncanonicalization
    // and produce a range for each element.  For instance for [c-f]
    // we look up ['a', 'A'] and produce [c-f] and [C-F].  We then only
    // add a range if it is not already contained in the input, so [c-f]
    // will be skipped but [C-F] will be added.  If this range is not
    // completely contained in a block we do this for all the blocks
    // covered by the range.
    unibrow::uchar range[unibrow::Ecma262UnCanonicalize::kMaxWidth];
    // First, look up the block that contains the 'from' character.
    int length = canonrange.get(from(), '\0', range);
    if (length == 0) {
      range[0] = from();
    } else {
      ASSERT_EQ(1, length);
    }
    int pos = from();
    // The start of the current block.  Note that except for the first
    // iteration 'start' is always equal to 'pos'.
    int start;
    // If it is not the start point of a block the entry contains the
    // offset of the character from the start point.
    if ((range[0] & kStartMarker) == 0) {
      start = pos - range[0];
    } else {
      start = pos;
    }
    // Then we add the ranges on at a time, incrementing the current
    // position to be after the last block each time.  The position
    // always points to the start of a block.
    while (pos < to()) {
      length = canonrange.get(start, '\0', range);
      if (length == 0) {
        range[0] = start;
      } else {
        ASSERT_EQ(1, length);
      }
      ASSERT((range[0] & kStartMarker) != 0);
      // The start point of a block contains the distance to the end
      // of the range.
      int block_end = start + (range[0] & kPayloadMask) - 1;
      int end = (block_end > to()) ? to() : block_end;
      length = uncanonicalize.get(start, '\0', range);
      for (int i = 0; i < length; i++) {
        uc32 c = range[i];
        uc16 range_from = c + (pos - start);
        uc16 range_to = c + (end - start);
        if (!(from() <= range_from && range_to <= to())) {
          ranges->Add(CharacterRange(range_from, range_to));
        }
      }
      start = pos = block_end + 1;
    }
  } else {
    // TODO(plesner) when we've fixed the 2^11 bug in unibrow.
  }
}


// -------------------------------------------------------------------
// Interest propagation


RegExpNode* RegExpNode::TryGetSibling(NodeInfo* info) {
  for (int i = 0; i < siblings_.length(); i++) {
    RegExpNode* sibling = siblings_.Get(i);
    if (sibling->info()->Matches(info))
      return sibling;
  }
  return NULL;
}


RegExpNode* RegExpNode::EnsureSibling(NodeInfo* info, bool* cloned) {
  ASSERT_EQ(false, *cloned);
  ASSERT(!info->HasAssertions());
  siblings_.Ensure(this);
  RegExpNode* result = TryGetSibling(info);
  if (result != NULL) return result;
  result = this->Clone();
  NodeInfo* new_info = result->info();
  new_info->ResetCompilationState();
  new_info->AddFromPreceding(info);
  AddSibling(result);
  *cloned = true;
  return result;
}


template <class C>
static RegExpNode* PropagateToEndpoint(C* node, NodeInfo* info) {
  NodeInfo full_info(*node->info());
  full_info.AddFromPreceding(info);
  bool cloned = false;
  return RegExpNode::EnsureSibling(node, &full_info, &cloned);
}


RegExpNode* ActionNode::PropagateForward(NodeInfo* info) {
  NodeInfo full_info(*this->info());
  full_info.AddFromPreceding(info);
  bool cloned = false;
  ActionNode* action = EnsureSibling(this, &full_info, &cloned);
  action->set_on_success(action->on_success()->PropagateForward(info));
  return action;
}


RegExpNode* ChoiceNode::PropagateForward(NodeInfo* info) {
  NodeInfo full_info(*this->info());
  full_info.AddFromPreceding(info);
  bool cloned = false;
  ChoiceNode* choice = EnsureSibling(this, &full_info, &cloned);
  if (cloned) {
    ZoneList<GuardedAlternative>* old_alternatives = alternatives();
    int count = old_alternatives->length();
    choice->alternatives_ = new ZoneList<GuardedAlternative>(count);
    for (int i = 0; i < count; i++) {
      GuardedAlternative alternative = old_alternatives->at(i);
      alternative.set_node(alternative.node()->PropagateForward(info));
      choice->alternatives()->Add(alternative);
    }
  }
  return choice;
}


RegExpNode* EndNode::PropagateForward(NodeInfo* info) {
  return PropagateToEndpoint(this, info);
}


RegExpNode* BackReferenceNode::PropagateForward(NodeInfo* info) {
  NodeInfo full_info(*this->info());
  full_info.AddFromPreceding(info);
  bool cloned = false;
  BackReferenceNode* back_ref = EnsureSibling(this, &full_info, &cloned);
  if (cloned) {
    // TODO(erikcorry): A back reference has to have two successors (by default
    // the same node).  The first is used if the back reference matches a non-
    // empty back reference, the second if it matches an empty one.  This
    // doesn't matter for at_end, which is the only one implemented right now,
    // but it will matter for other pieces of info.
    back_ref->set_on_success(back_ref->on_success()->PropagateForward(info));
  }
  return back_ref;
}


RegExpNode* TextNode::PropagateForward(NodeInfo* info) {
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
      if (entry->to() == String::kMaxUC16CharCode)
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


void AssertionPropagation::EnsureAnalyzed(RegExpNode* that) {
  if (that->info()->been_analyzed || that->info()->being_analyzed)
    return;
  that->info()->being_analyzed = true;
  that->Accept(this);
  that->info()->being_analyzed = false;
  that->info()->been_analyzed = true;
}


void AssertionPropagation::VisitEnd(EndNode* that) {
  // nothing to do
}


void TextNode::CalculateOffsets() {
  int element_count = elements()->length();
  // Set up the offsets of the elements relative to the start.  This is a fixed
  // quantity since a TextNode can only contain fixed-width things.
  int cp_offset = 0;
  for (int i = 0; i < element_count; i++) {
    TextElement& elm = elements()->at(i);
    elm.cp_offset = cp_offset;
    if (elm.type == TextElement::ATOM) {
      cp_offset += elm.data.u_atom->data().length();
    } else {
      cp_offset++;
      Vector<const uc16> quarks = elm.data.u_atom->data();
    }
  }
}


void AssertionPropagation::VisitText(TextNode* that) {
  if (ignore_case_) {
    that->MakeCaseIndependent();
  }
  EnsureAnalyzed(that->on_success());
  NodeInfo* info = that->info();
  NodeInfo* next_info = that->on_success()->info();
  // If the following node is interested in what it follows then this
  // node must determine it.
  info->determine_newline = next_info->follows_newline_interest;
  info->determine_word = next_info->follows_word_interest;
  info->determine_start = next_info->follows_start_interest;
  that->CalculateOffsets();
}


void AssertionPropagation::VisitAction(ActionNode* that) {
  RegExpNode* target = that->on_success();
  EnsureAnalyzed(target);
  // If the next node is interested in what it follows then this node
  // has to be interested too so it can pass the information on.
  that->info()->AddFromFollowing(target->info());
}


void AssertionPropagation::VisitChoice(ChoiceNode* that) {
  NodeInfo* info = that->info();
  for (int i = 0; i < that->alternatives()->length(); i++) {
    RegExpNode* node = that->alternatives()->at(i).node();
    EnsureAnalyzed(node);
    // Anything the following nodes need to know has to be known by
    // this node also, so it can pass it on.
    info->AddFromFollowing(node->info());
  }
}


void AssertionPropagation::VisitBackReference(BackReferenceNode* that) {
  EnsureAnalyzed(that->on_success());
}


// -------------------------------------------------------------------
// Assumption expansion


RegExpNode* RegExpNode::EnsureExpanded(NodeInfo* info) {
  siblings_.Ensure(this);
  NodeInfo new_info = *this->info();
  if (new_info.follows_word_interest)
    new_info.follows_word = info->follows_word;
  if (new_info.follows_newline_interest)
    new_info.follows_newline = info->follows_newline;
  // If the following node should determine something we need to get
  // a sibling that determines it.
  new_info.does_determine_newline = new_info.determine_newline;
  new_info.does_determine_word = new_info.determine_word;
  new_info.does_determine_start = new_info.determine_start;
  RegExpNode* sibling = TryGetSibling(&new_info);
  if (sibling == NULL) {
    sibling = ExpandLocal(&new_info);
    siblings_.Add(sibling);
    sibling->info()->being_expanded = true;
    sibling->ExpandChildren();
    sibling->info()->being_expanded = false;
    sibling->info()->been_expanded = true;
  } else {
    NodeInfo* sib_info = sibling->info();
    if (!sib_info->been_expanded && !sib_info->being_expanded) {
      sibling->info()->being_expanded = true;
      sibling->ExpandChildren();
      sibling->info()->being_expanded = false;
      sibling->info()->been_expanded = true;
    }
  }
  return sibling;
}


RegExpNode* ChoiceNode::ExpandLocal(NodeInfo* info) {
  ChoiceNode* clone = this->Clone();
  clone->info()->ResetCompilationState();
  clone->info()->AddAssumptions(info);
  return clone;
}


void ChoiceNode::ExpandChildren() {
  ZoneList<GuardedAlternative>* alts = alternatives();
  ZoneList<GuardedAlternative>* new_alts
      = new ZoneList<GuardedAlternative>(alts->length());
  for (int i = 0; i < alts->length(); i++) {
    GuardedAlternative next = alts->at(i);
    next.set_node(next.node()->EnsureExpanded(info()));
    new_alts->Add(next);
  }
  alternatives_ = new_alts;
}


RegExpNode* TextNode::ExpandLocal(NodeInfo* info) {
  TextElement last = elements()->last();
  if (last.type == TextElement::CHAR_CLASS) {
    RegExpCharacterClass* char_class = last.data.u_char_class;
    if (info->does_determine_word) {
      ZoneList<CharacterRange>* word = NULL;
      ZoneList<CharacterRange>* non_word = NULL;
      CharacterRange::Split(char_class->ranges(),
                            CharacterRange::GetWordBounds(),
                            &word,
                            &non_word);
      if (non_word == NULL) {
        // This node contains no non-word characters so it must be
        // all word.
        this->info()->is_word = NodeInfo::TRUE;
      } else if (word == NULL) {
        // Vice versa.
        this->info()->is_word = NodeInfo::FALSE;
      } else {
        // If this character class contains both word and non-word
        // characters we need to split it into two.
        ChoiceNode* result = new ChoiceNode(2);
        // Welcome to the family, son!
        result->set_siblings(this->siblings());
        *result->info() = *this->info();
        result->info()->ResetCompilationState();
        result->info()->AddAssumptions(info);
        RegExpNode* word_node
            = new TextNode(new RegExpCharacterClass(word, false),
                           on_success());
        word_node->info()->determine_word = true;
        word_node->info()->does_determine_word = true;
        word_node->info()->is_word = NodeInfo::TRUE;
        result->alternatives()->Add(GuardedAlternative(word_node));
        RegExpNode* non_word_node
            = new TextNode(new RegExpCharacterClass(non_word, false),
                           on_success());
        non_word_node->info()->determine_word = true;
        non_word_node->info()->does_determine_word = true;
        non_word_node->info()->is_word = NodeInfo::FALSE;
        result->alternatives()->Add(GuardedAlternative(non_word_node));
        return result;
      }
    }
  }
  TextNode* clone = this->Clone();
  clone->info()->ResetCompilationState();
  clone->info()->AddAssumptions(info);
  return clone;
}


void TextNode::ExpandAtomChildren(RegExpAtom* that) {
  NodeInfo new_info = *info();
  uc16 last = that->data()[that->data().length() - 1];
  if (info()->determine_word) {
    new_info.follows_word = IsRegExpWord(last)
      ? NodeInfo::TRUE : NodeInfo::FALSE;
  } else {
    new_info.follows_word = NodeInfo::UNKNOWN;
  }
  if (info()->determine_newline) {
    new_info.follows_newline = IsRegExpNewline(last)
      ? NodeInfo::TRUE : NodeInfo::FALSE;
  } else {
    new_info.follows_newline = NodeInfo::UNKNOWN;
  }
  if (info()->determine_start) {
    new_info.follows_start = NodeInfo::FALSE;
  } else {
    new_info.follows_start = NodeInfo::UNKNOWN;
  }
  set_on_success(on_success()->EnsureExpanded(&new_info));
}


void TextNode::ExpandCharClassChildren(RegExpCharacterClass* that) {
  if (info()->does_determine_word) {
    // ASSERT(info()->is_word != NodeInfo::UNKNOWN);
    NodeInfo next_info = *on_success()->info();
    next_info.follows_word = info()->is_word;
    set_on_success(on_success()->EnsureExpanded(&next_info));
  } else {
    set_on_success(on_success()->EnsureExpanded(info()));
  }
}


void TextNode::ExpandChildren() {
  TextElement last = elements()->last();
  switch (last.type) {
    case TextElement::ATOM:
      ExpandAtomChildren(last.data.u_atom);
      break;
    case TextElement::CHAR_CLASS:
      ExpandCharClassChildren(last.data.u_char_class);
      break;
    default:
      UNREACHABLE();
  }
}


RegExpNode* ActionNode::ExpandLocal(NodeInfo* info) {
  ActionNode* clone = this->Clone();
  clone->info()->ResetCompilationState();
  clone->info()->AddAssumptions(info);
  return clone;
}


void ActionNode::ExpandChildren() {
  set_on_success(on_success()->EnsureExpanded(info()));
}


RegExpNode* BackReferenceNode::ExpandLocal(NodeInfo* info) {
  BackReferenceNode* clone = this->Clone();
  clone->info()->ResetCompilationState();
  clone->info()->AddAssumptions(info);
  return clone;
}


void BackReferenceNode::ExpandChildren() {
  set_on_success(on_success()->EnsureExpanded(info()));
}


RegExpNode* EndNode::ExpandLocal(NodeInfo* info) {
  EndNode* clone = this->Clone();
  clone->info()->ResetCompilationState();
  clone->info()->AddAssumptions(info);
  return clone;
}


void EndNode::ExpandChildren() {
  // nothing to do
}


// -------------------------------------------------------------------
// Dispatch table construction


void DispatchTableConstructor::VisitEnd(EndNode* that) {
  AddRange(CharacterRange::Everything());
}


void DispatchTableConstructor::BuildTable(ChoiceNode* node) {
  node->set_being_calculated(true);
  ZoneList<GuardedAlternative>* alternatives = node->alternatives();
  for (int i = 0; i < alternatives->length(); i++) {
    set_choice_index(i);
    alternatives->at(i).node()->Accept(this);
  }
  node->set_being_calculated(false);
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
  DispatchTable* table = node->GetTable(ignore_case_);
  AddDispatchRange adder(this);
  table->ForEach(&adder);
}


void DispatchTableConstructor::VisitBackReference(BackReferenceNode* that) {
  // TODO(160): Find the node that we refer back to and propagate its start
  // set back to here.  For now we just accept anything.
  AddRange(CharacterRange::Everything());
}



static int CompareRangeByFrom(const CharacterRange* a,
                              const CharacterRange* b) {
  return Compare<uc16>(a->from(), b->from());
}


void DispatchTableConstructor::AddInverse(ZoneList<CharacterRange>* ranges) {
  ranges->Sort(CompareRangeByFrom);
  uc16 last = 0;
  for (int i = 0; i < ranges->length(); i++) {
    CharacterRange range = ranges->at(i);
    if (last < range.from())
      AddRange(CharacterRange(last, range.from() - 1));
    if (range.to() >= last) {
      if (range.to() == String::kMaxUC16CharCode) {
        return;
      } else {
        last = range.to() + 1;
      }
    }
  }
  AddRange(CharacterRange(last, String::kMaxUC16CharCode));
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
  RegExpNode* target = that->on_success();
  target->Accept(this);
}


#ifdef DEBUG


class VisitNodeScope {
 public:
  explicit VisitNodeScope(RegExpNode* node) : node_(node) {
    ASSERT(!node->info()->visited);
    node->info()->visited = true;
  }
  ~VisitNodeScope() {
    node_->info()->visited = false;
  }
 private:
  RegExpNode* node_;
};


class NodeValidator : public NodeVisitor {
 public:
  virtual void ValidateInfo(NodeInfo* info) = 0;
#define DECLARE_VISIT(Type)                                          \
  virtual void Visit##Type(Type##Node* that);
FOR_EACH_NODE_TYPE(DECLARE_VISIT)
#undef DECLARE_VISIT
};


class PostAnalysisNodeValidator : public NodeValidator {
 public:
  virtual void ValidateInfo(NodeInfo* info);
};


class PostExpansionNodeValidator : public NodeValidator {
 public:
  virtual void ValidateInfo(NodeInfo* info);
};


void PostAnalysisNodeValidator::ValidateInfo(NodeInfo* info) {
  ASSERT(info->been_analyzed);
}


void PostExpansionNodeValidator::ValidateInfo(NodeInfo* info) {
  ASSERT_EQ(info->determine_newline, info->does_determine_newline);
  ASSERT_EQ(info->determine_start, info->does_determine_start);
  ASSERT_EQ(info->determine_word, info->does_determine_word);
  ASSERT_EQ(info->follows_word_interest,
            (info->follows_word != NodeInfo::UNKNOWN));
  if (false) {
    // These are still unimplemented.
    ASSERT_EQ(info->follows_start_interest,
              (info->follows_start != NodeInfo::UNKNOWN));
    ASSERT_EQ(info->follows_newline_interest,
              (info->follows_newline != NodeInfo::UNKNOWN));
  }
}


void NodeValidator::VisitAction(ActionNode* that) {
  if (that->info()->visited) return;
  VisitNodeScope scope(that);
  ValidateInfo(that->info());
  that->on_success()->Accept(this);
}


void NodeValidator::VisitBackReference(BackReferenceNode* that) {
  if (that->info()->visited) return;
  VisitNodeScope scope(that);
  ValidateInfo(that->info());
  that->on_success()->Accept(this);
}


void NodeValidator::VisitChoice(ChoiceNode* that) {
  if (that->info()->visited) return;
  VisitNodeScope scope(that);
  ValidateInfo(that->info());
  ZoneList<GuardedAlternative>* alts = that->alternatives();
  for (int i = 0; i < alts->length(); i++)
    alts->at(i).node()->Accept(this);
}


void NodeValidator::VisitEnd(EndNode* that) {
  if (that->info()->visited) return;
  VisitNodeScope scope(that);
  ValidateInfo(that->info());
}


void NodeValidator::VisitText(TextNode* that) {
  if (that->info()->visited) return;
  VisitNodeScope scope(that);
  ValidateInfo(that->info());
  that->on_success()->Accept(this);
}


#endif


Handle<FixedArray> RegExpEngine::Compile(RegExpCompileData* data,
                                         bool ignore_case,
                                         bool is_multiline,
                                         Handle<String> pattern,
                                         bool is_ascii) {
  RegExpCompiler compiler(data->capture_count, ignore_case, is_ascii);
  // Wrap the body of the regexp in capture #0.
  RegExpNode* captured_body = RegExpCapture::ToNode(data->tree,
                                                    0,
                                                    &compiler,
                                                    compiler.accept());
  // Add a .*? at the beginning, outside the body capture.
  // Note: We could choose to not add this if the regexp is anchored at
  //   the start of the input but I'm not sure how best to do that and
  //   since we don't even handle ^ yet I'm saving that optimization for
  //   later.
  RegExpNode* node = RegExpQuantifier::ToNode(0,
                                              RegExpTree::kInfinity,
                                              false,
                                              new RegExpCharacterClass('*'),
                                              &compiler,
                                              captured_body);
  AssertionPropagation analysis(ignore_case);
  analysis.EnsureAnalyzed(node);

  NodeInfo info = *node->info();
  data->has_lookbehind = info.HasLookbehind();
  if (data->has_lookbehind) {
    // If this node needs information about the preceding text we let
    // it start with a character class that consumes a single character
    // and proceeds to wherever is appropriate.  This means that if
    // has_lookbehind is set the code generator must start one character
    // before the start position.
    node = new TextNode(new RegExpCharacterClass('*'), node);
    analysis.EnsureAnalyzed(node);
  }

#ifdef DEBUG
  PostAnalysisNodeValidator post_analysis_validator;
  node->Accept(&post_analysis_validator);
#endif

  node = node->EnsureExpanded(&info);

#ifdef DEBUG
  PostExpansionNodeValidator post_expansion_validator;
  node->Accept(&post_expansion_validator);
#endif

  data->node = node;

  if (is_multiline && !FLAG_attempt_multiline_irregexp) {
    return Handle<FixedArray>::null();
  }

  if (data->has_lookbehind) {
    return Handle<FixedArray>::null();
  }

  if (FLAG_irregexp_native) {
#ifdef ARM
    // Unimplemented, fall-through to bytecode implementation.
#else  // IA32
    RegExpMacroAssemblerIA32::Mode mode;
    if (is_ascii) {
      mode = RegExpMacroAssemblerIA32::ASCII;
    } else {
      mode = RegExpMacroAssemblerIA32::UC16;
    }
    RegExpMacroAssemblerIA32 macro_assembler(mode,
                                             (data->capture_count + 1) * 2);
    return compiler.Assemble(&macro_assembler,
                             node,
                             data->capture_count,
                             pattern);
#endif
  }
  EmbeddedVector<byte, 1024> codes;
  RegExpMacroAssemblerIrregexp macro_assembler(codes);
  return compiler.Assemble(&macro_assembler,
                           node,
                           data->capture_count,
                           pattern);
}


}}  // namespace v8::internal
