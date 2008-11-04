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
  StringShape shape(*pattern);
  if (!pattern->IsFlat(shape)) {
    FlattenString(pattern);
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


static inline Handle<Object> CreateRegExpException(Handle<JSRegExp> re,
                                                   Handle<String> pattern,
                                                   Handle<String> error_text,
                                                   const char* message) {
  Handle<JSArray> array = Factory::NewJSArray(2);
  SetElement(array, 0, pattern);
  SetElement(array, 1, error_text);
  Handle<Object> regexp_err = Factory::NewSyntaxError(message, array);
  return Handle<Object>(Top::Throw(*regexp_err));
}


Handle<Object> RegExpImpl::Compile(Handle<JSRegExp> re,
                                   Handle<String> pattern,
                                   Handle<String> flag_str) {
  JSRegExp::Flags flags = RegExpFlagsFromString(flag_str);
  Handle<FixedArray> cached = CompilationCache::LookupRegExp(pattern, flags);
  bool in_cache = !cached.is_null();
  Handle<Object> result;
  StringShape shape(*pattern);
  if (in_cache) {
    re->set_data(*cached);
    result = re;
  } else {
    SafeStringInputBuffer buffer(pattern.location());
    Handle<String> error_text;
    RegExpTree* ast = ParseRegExp(&buffer, &error_text);
    if (!error_text.is_null()) {
      // Throw an exception if we fail to parse the pattern.
      return CreateRegExpException(re, pattern, error_text, "malformed_regexp");
    }
    RegExpAtom* atom = ast->AsAtom();
    if (atom != NULL && !flags.is_ignore_case()) {
      Vector<const uc16> atom_pattern = atom->data();
      // Test if pattern equals atom_pattern and reuse pattern if it does.
      Handle<String> atom_string = Factory::NewStringFromTwoByte(atom_pattern);
      result = AtomCompile(re, atom_string, flags);
    } else {
      result = JsrePrepare(re, pattern, flags);
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
  switch (regexp->TypeTag()) {
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
                                       Handle<String> pattern,
                                       JSRegExp::Flags flags) {
  Factory::SetRegExpData(re, JSRegExp::ATOM, pattern, flags, pattern);
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
  array->set(0,
             Smi::FromInt(value),
             SKIP_WRITE_BARRIER);
  array->set(1,
             Smi::FromInt(value + needle->length()),
             SKIP_WRITE_BARRIER);
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
    array->set(0,
               Smi::FromInt(value),
               SKIP_WRITE_BARRIER);
    array->set(1,
               Smi::FromInt(end),
               SKIP_WRITE_BARRIER);
    Handle<JSArray> pair = Factory::NewJSArrayWithElements(array);
    SetElement(result, match_count, pair);
    match_count++;
    index = end;
    if (needle_length == 0) index++;
  }
  return result;
}


Handle<Object>RegExpImpl::JsrePrepare(Handle<JSRegExp> re,
                                      Handle<String> pattern,
                                      JSRegExp::Flags flags) {
  Handle<Object> value(Heap::undefined_value());
  Factory::SetRegExpData(re, JSRegExp::JSCRE, pattern, flags, value);
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


Handle<Object> RegExpImpl::JsreCompile(Handle<JSRegExp> re) {
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
    return Handle<Object>(Top::Throw(*regexp_err));
  }

  // Convert the return address to a ByteArray pointer.
  Handle<ByteArray> internal(
      ByteArray::FromDataStartAddress(reinterpret_cast<Address>(code)));

  Handle<FixedArray> value = Factory::NewFixedArray(2);
  value->set(CAPTURE_INDEX, Smi::FromInt(number_of_captures));
  value->set(INTERNAL_INDEX, *internal);
  Factory::SetRegExpData(re, JSRegExp::JSCRE, pattern, flags, value);

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

  Handle<FixedArray> array = Factory::NewFixedArray(2 * (num_captures+1));
  // The captures come in (start, end+1) pairs.
  for (int i = 0; i < 2 * (num_captures+1); i += 2) {
    array->set(i,
               Smi::FromInt(offsets_vector[i]),
               SKIP_WRITE_BARRIER);
    array->set(i+1,
               Smi::FromInt(offsets_vector[i+1]),
               SKIP_WRITE_BARRIER);
  }
  return Factory::NewJSArrayWithElements(array);
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
  ASSERT_EQ(regexp->TypeTag(), JSRegExp::JSCRE);
  if (regexp->DataAt(JSRegExp::kJscreDataIndex)->IsUndefined()) {
    Handle<Object> compile_result = JsreCompile(regexp);
    if (compile_result->IsException()) return compile_result;
  }
  ASSERT(regexp->DataAt(JSRegExp::kJscreDataIndex)->IsFixedArray());

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
  ASSERT_EQ(regexp->TypeTag(), JSRegExp::JSCRE);
  if (regexp->DataAt(JSRegExp::kJscreDataIndex)->IsUndefined()) {
    Handle<Object> compile_result = JsreCompile(regexp);
    if (compile_result->IsException()) return compile_result;
  }
  ASSERT(regexp->DataAt(JSRegExp::kJscreDataIndex)->IsFixedArray());

  // Prepare space for the return values.
  int num_captures = JsreCapture(regexp);

  OffsetsVector offsets(num_captures);

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
  FixedArray* value = FixedArray::cast(re->DataAt(JSRegExp::kJscreDataIndex));
  return Smi::cast(value->get(CAPTURE_INDEX))->value();
}


ByteArray* RegExpImpl::JsreInternal(Handle<JSRegExp> re) {
  FixedArray* value = FixedArray::cast(re->DataAt(JSRegExp::kJscreDataIndex));
  return ByteArray::cast(value->get(INTERNAL_INDEX));
}


// -------------------------------------------------------------------
// New regular expression engine


template <typename Char> class ExecutionState;


template <typename Char>
class DotPrinter {
 public:
  DotPrinter() : stream_(&alloc_) { }
  void PrintNode(RegExpNode<Char>* node);
  void Visit(RegExpNode<Char>* node);
  StringStream* stream() { return &stream_; }
 private:
  HeapStringAllocator alloc_;
  StringStream stream_;
  std::set<RegExpNode<Char>*> seen_;
};


template <typename Char>
class RegExpCompiler: public RegExpVisitor {
 public:
  RegExpCompiler() { }
  RegExpNode<Char>* Compile(RegExpTree* tree, RegExpNode<Char>* rest) {
    return static_cast<RegExpNode<Char>*>(tree->Accept(this, rest));
  }
#define MAKE_CASE(Name) virtual void* Visit##Name(RegExp##Name*, void*);
  FOR_EACH_REG_EXP_NODE_TYPE(MAKE_CASE)
#undef MAKE_CASE
};


template <typename Char>
class RegExpNode: public ZoneObject {
 public:
  virtual ~RegExpNode() { }
  virtual void EmitDot(DotPrinter<Char>* out);
  virtual bool Step(ExecutionState<Char>* state) = 0;
};


template <typename Char>
class SeqRegExpNode: public RegExpNode<Char> {
 public:
  explicit SeqRegExpNode(RegExpNode<Char>* next) : next_(next) { }
  RegExpNode<Char>* next() { return next_; }
 private:
  RegExpNode<Char>* next_;
};


template <typename Char>
class EndNode: public RegExpNode<Char> {
 public:
  virtual void EmitDot(DotPrinter<Char>* out);
  virtual bool Step(ExecutionState<Char>* state);
};


template <typename Char>
class AtomNode: public SeqRegExpNode<Char> {
 public:
  AtomNode(Vector<const uc16> data, RegExpNode<Char>* next)
    : SeqRegExpNode<Char>(next),
      data_(data) { }
  virtual void EmitDot(DotPrinter<Char>* out);
  virtual bool Step(ExecutionState<Char>* state);
  Vector<const uc16> data() { return data_; }
 private:
  Vector<const uc16> data_;
};


template <typename Char>
class CharacterClassNode: public SeqRegExpNode<Char> {
 public:
  CharacterClassNode(ZoneList<CharacterRange>* ranges, RegExpNode<Char>* next)
    : SeqRegExpNode<Char>(next),
      ranges_(ranges) { }
  virtual void EmitDot(DotPrinter<Char>* out);
  virtual bool Step(ExecutionState<Char>* state);
  ZoneList<CharacterRange>* ranges() { return ranges_; }
 private:
  ZoneList<CharacterRange>* ranges_;
};


template <typename Char>
class ChoiceNode: public RegExpNode<Char> {
 public:
  explicit ChoiceNode(ZoneList<RegExpNode<Char>*>* choices)
    : choices_(choices) { }
  virtual void EmitDot(DotPrinter<Char>* out);
  virtual bool Step(ExecutionState<Char>* state);
  ZoneList<RegExpNode<Char>*>* choices() { return choices_; }
  DispatchTable* table() { return table_; }
 private:
  ZoneList<RegExpNode<Char>*>* choices_;
  DispatchTable table_;
};


// -------------------------------------------------------------------
// Dot/dotty output


template <typename Char>
void DotPrinter<Char>::PrintNode(RegExpNode<Char>* node) {
  stream()->Add("digraph G {\n");
  Visit(node);
  stream()->Add("}\n");
  printf("%s", *(stream()->ToCString()));
}


template <typename Char>
void DotPrinter<Char>::Visit(RegExpNode<Char>* node) {
  if (seen_.find(node) != seen_.end())
    return;
  seen_.insert(node);
  node->EmitDot(this);
}


template <typename Char>
void RegExpNode<Char>::EmitDot(DotPrinter<Char>* out) {
  UNIMPLEMENTED();
}


template <typename Char>
void ChoiceNode<Char>::EmitDot(DotPrinter<Char>* out) {
  out->stream()->Add("n%p [label=\"?\"];\n", this);
  for (int i = 0; i < choices()->length(); i++) {
    out->stream()->Add("n%p -> n%p [label=\"%i\"];\n",
                       this,
                       choices()->at(i),
                       i);
    out->Visit(choices()->at(i));
  }
}


template <typename Char>
void AtomNode<Char>::EmitDot(DotPrinter<Char>* out) {
  out->stream()->Add("n%p [label=\"'%w'\"];\n", this, data());
  out->stream()->Add("n%p -> n%p;\n", this, this->next());
  out->Visit(this->next());
}


template <typename Char>
void EndNode<Char>::EmitDot(DotPrinter<Char>* out) {
  out->stream()->Add("n%p [style=bold, label=\"done\"];\n", this);
}


template <typename Char>
void CharacterClassNode<Char>::EmitDot(DotPrinter<Char>* out) {
  out->stream()->Add("n%p [label=\"[...]\"];\n", this);
  out->stream()->Add("n%p -> n%p;\n", this, this->next());
  out->Visit(this->next());
}


// -------------------------------------------------------------------
// Tree to graph conversion


template <typename Char>
void* RegExpCompiler<Char>::VisitAtom(RegExpAtom* that, void* rest) {
  return new AtomNode<Char>(that->data(),
                            static_cast<RegExpNode<Char>*>(rest));
}


template <typename Char>
void* RegExpCompiler<Char>::VisitCharacterClass(RegExpCharacterClass* that,
                                                void* rest) {
  return new CharacterClassNode<Char>(that->ranges(),
                                      static_cast<RegExpNode<Char>*>(rest));
}


template <typename Char>
void* RegExpCompiler<Char>::VisitDisjunction(RegExpDisjunction* that,
                                             void* rest_ptr) {
  RegExpNode<Char>* rest = static_cast<RegExpNode<Char>*>(rest_ptr);
  ZoneList<RegExpTree*>* children = that->nodes();
  int length = children->length();
  ZoneList<RegExpNode<Char>*>* choices
      = new ZoneList<RegExpNode<Char>*>(length);
  for (int i = 0; i < length; i++)
    choices->Add(Compile(children->at(i), rest));
  return new ChoiceNode<Char>(choices);
}


template <typename Char>
void* RegExpCompiler<Char>::VisitQuantifier(RegExpQuantifier* that,
                                            void* rest_ptr) {
  RegExpNode<Char>* rest = static_cast<RegExpNode<Char>*>(rest_ptr);
  if (that->max() >= RegExpQuantifier::kInfinity) {
    // Don't try to count the number of iterations if the max it too
    // large.
    if (that->min() != 0) {
      UNIMPLEMENTED();
    }
    ZoneList<RegExpNode<Char>*>* loop_choices
        = new ZoneList<RegExpNode<Char>*>(2);
    RegExpNode<Char>* loop_node = new ChoiceNode<Char>(loop_choices);
    RegExpNode<Char>* body_node = Compile(that->body(), loop_node);
    if (that->is_greedy()) {
      loop_choices->Add(body_node);
      loop_choices->Add(rest);
    } else {
      loop_choices->Add(rest);
      loop_choices->Add(body_node);
    }
    return loop_node;
  } else {
    UNIMPLEMENTED();
    return NULL;
  }
}


template <typename Char>
void* RegExpCompiler<Char>::VisitAssertion(RegExpAssertion* that,
                                           void* rest) {
  UNIMPLEMENTED();
  return NULL;
}


template <typename Char>
void* RegExpCompiler<Char>::VisitCapture(RegExpCapture* that, void* rest) {
  UNIMPLEMENTED();
  return NULL;
}


template <typename Char>
void* RegExpCompiler<Char>::VisitLookahead(RegExpLookahead* that,
                                           void* rest) {
  UNIMPLEMENTED();
  return NULL;
}


template <typename Char>
void* RegExpCompiler<Char>::VisitBackreference(RegExpBackreference* that,
                                               void* rest) {
  UNIMPLEMENTED();
  return NULL;
}


template <typename Char>
void* RegExpCompiler<Char>::VisitEmpty(RegExpEmpty* that, void* rest) {
  return rest;
}


template <typename Char>
void* RegExpCompiler<Char>::VisitAlternative(RegExpAlternative* that,
                                             void* rest) {
  ZoneList<RegExpTree*>* children = that->nodes();
  RegExpNode<Char>* current = static_cast<RegExpNode<Char>*>(rest);
  for (int i = children->length() - 1; i >= 0; i--) {
    current = Compile(children->at(i), current);
  }
  return current;
}


static const int kSpaceRangeCount = 20;
static const uc16 kSpaceRanges[kSpaceRangeCount] = {
  0x0009, 0x0009, 0x000B, 0x000C, 0x0020, 0x0020, 0x00A0, 0x00A0,
  0x1680, 0x1680, 0x180E, 0x180E, 0x2000, 0x200A, 0x202F, 0x202F,
  0x205F, 0x205F, 0x3000, 0x3000
};


static const int kWordRangeCount = 8;
static const uc16 kWordRanges[kWordRangeCount] = {
  '0', '9', 'A', 'Z', '_', '_', 'a', 'z'
};


static const int kDigitRangeCount = 2;
static const uc16 kDigitRanges[kDigitRangeCount] = {
  '0', '9'
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


void CharacterRange::AddClassEscape(uc16 type, ZoneList<CharacterRange>* ranges) {
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
      ranges->Add(CharacterRange(0x0000, 0xFFFF));
      break;
    default:
      UNREACHABLE();
  }
}


// -------------------------------------------------------------------
// Splay tree


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


OutSet OutSet::Clone() {
  if (remaining_ == NULL) {
    return OutSet(first_, NULL);
  } else {
    int length = remaining_->length();
    ZoneList<unsigned>* clone = new ZoneList<unsigned>(length);
    for (int i = 0; i < length; i++)
      clone->Add(remaining_->at(i));
    return OutSet(first_, clone);
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
    loc.set_value(Entry(current.from(), current.to(), value));
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
                          entry->out_set().Clone()));
    }
  }
  while (current.is_valid()) {
    if (tree()->FindLeastGreaterThan(current.from(), &loc) &&
        (loc.value().from() <= current.to())) {
      Entry* entry = &loc.value();
      // We have overlap.  If there is space between the start point of
      // the range we're adding and where the overlapping range starts
      // then we have to add a range covering just that space.
      if (current.from() < entry->from()) {
        ZoneSplayTree<Config>::Locator ins;
        ASSERT_RESULT(tree()->Insert(current.from(), &ins));
        ins.set_value(Entry(current.from(), entry->from() - 1, value));
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
                            entry->out_set().Clone()));
        entry->set_to(current.to());
      }
      ASSERT(entry->to() <= current.to());
      // The overlapping range is now completely contained by the range
      // we're adding so we can just update it and move the start point
      // of the range we're adding just past it.
      entry->AddValue(value);
      current.set_from(entry->to() + 1);
    } else {
      // There is no overlap so we can just add the range
      ZoneSplayTree<Config>::Locator ins;
      ASSERT_RESULT(tree()->Insert(current.from(), &ins));
      ins.set_value(Entry(current.from(), current.to(), value));
      break;
    }
  }
}


OutSet DispatchTable::Get(uc16 value) {
  ZoneSplayTree<Config>::Locator loc;
  if (!tree()->FindGreatestLessThan(value, &loc))
    return OutSet::empty();
  Entry* entry = &loc.value();
  if (value <= entry->to())
    return entry->out_set();
  else
    return OutSet::empty();
}


// -------------------------------------------------------------------
// Execution


template <typename Char>
class ExecutionState {
 public:
  ExecutionState(RegExpNode<Char>* start, Vector<Char> input)
    : current_(start),
      input_(input),
      pos_(0),
      backtrack_stack_(8),
      is_done_(false) { }

  class BacktrackState {
   public:
    BacktrackState(ChoiceNode<Char>* choice, int next, int pos)
      : choice_(choice),
        next_(next),
        pos_(pos) { }
    ChoiceNode<Char>* choice() { return choice_; }
    int next() { return next_; }
    void set_next(int value) { next_ = value; }
    int pos() { return pos_; }
   private:
    ChoiceNode<Char>* choice_;
    int next_;
    int pos_;
  };

  // Execute a single step, returning true if it succeeded
  inline bool Step() { return current()->Step(this); }

  // Stores the given choice node and the execution state on the
  // backtrack stack.
  void SaveBacktrack(ChoiceNode<Char>* choice);

  // Reverts to the next unused backtrack if there is one.  Returns
  // false exactly if there was no backtrack to restore.
  bool Backtrack();

  Char current_char() { return input()[pos()]; }

  void Advance(int delta, RegExpNode<Char>* next) {
    pos_ += delta;
    current_ = next;
  }

  bool AtEnd() { return pos_ >= input_.length(); }

  bool is_done() { return is_done_; }
  void set_done() { is_done_ = true; }

  List<BacktrackState>* backtrack_stack() { return &backtrack_stack_; }
  RegExpNode<Char>* current() { return current_; }
  void set_current(RegExpNode<Char>* value) { current_ = value; }
  Vector<Char> input() { return input_; }
  int pos() { return pos_; }
 private:
  RegExpNode<Char>* current_;
  Vector<Char> input_;
  int pos_;
  List<BacktrackState> backtrack_stack_;
  bool is_done_;
};


template <typename Char>
void ExecutionState<Char>::SaveBacktrack(ChoiceNode<Char>* choice) {
  ASSERT(choice->choices()->length() > 1);
  if (FLAG_trace_regexps) {
    PrintF("Setting up backtrack on level %i for choice %p\n",
           backtrack_stack()->length(),
           choice);
  }
  backtrack_stack()->Add(BacktrackState(choice, 1, pos_));
}


template <typename Char>
bool ExecutionState<Char>::Backtrack() {
  if (backtrack_stack()->is_empty()) return false;
  BacktrackState& top = backtrack_stack()->at(backtrack_stack()->length() - 1);
  ZoneList<RegExpNode<Char>*>* choices = top.choice()->choices();
  int next_index = top.next();
  current_ = choices->at(next_index);
  pos_ = top.pos();
  if (FLAG_trace_regexps) {
    PrintF("Backtracking to %p[%i] on level %i\n",
           top.choice(),
           next_index,
           backtrack_stack()->length() - 1);
  }
  if (next_index == choices->length() - 1) {
    if (FLAG_trace_regexps)
      PrintF("Popping backtrack on level %i\n",
             backtrack_stack()->length() - 1);
    // If this was the last alternative we're done with this backtrack
    // state and can pop it off the stack.
    backtrack_stack()->RemoveLast();
  } else {
    if (FLAG_trace_regexps)
      PrintF("Advancing backtrack on level %i\n",
             backtrack_stack()->length() - 1);
    // Otherwise we set the next choice to visit if this one fails.
    top.set_next(next_index + 1);
  }
  return true;
}


template <typename Char>
bool ChoiceNode<Char>::Step(ExecutionState<Char>* state) {
  state->SaveBacktrack(this);
  state->set_current(this->choices()->at(0));
  return true;
}


template <typename Char>
bool AtomNode<Char>::Step(ExecutionState<Char>* state) {
  Vector<const uc16> data = this->data();
  int length = data.length();
  Vector<Char> input = state->input();
  int p = state->pos();
  if (p + length > input.length())
    return false;
  for (int i = 0; i < length; i++, p++) {
    if (data[i] != input[p])
      return false;
  }
  state->Advance(length, this->next());
  return true;
}


template <typename Char>
bool CharacterClassNode<Char>::Step(ExecutionState<Char>* state) {
  if (state->AtEnd()) return false;
  ZoneList<CharacterRange>* ranges = this->ranges();
  unsigned current = state->current_char();
  for (int i = 0; i < ranges->length(); i++) {
    CharacterRange& range = ranges->at(i);
    if (range.from() <= current && current <= range.to()) {
      state->Advance(1, this->next());
      return true;
    }
  }
  return false;
}


template <typename Char>
bool EndNode<Char>::Step(ExecutionState<Char>* state) {
  state->set_done();
  return false;
}


template <typename Char>
bool RegExpEngine::Execute(RegExpNode<Char>* start, Vector<Char> input) {
  ExecutionState<Char> state(start, input);
  if (FLAG_trace_regexps) {
    PrintF("Beginning regexp execution\n");
  }
  while (state.Step() || (!state.is_done() && state.Backtrack()))
    ;
  if (FLAG_trace_regexps) {
    PrintF("Matching %s\n", state.is_done() ? "succeeded" : "failed");
  }
  return state.is_done();
}


template <typename Char>
RegExpNode<Char>* RegExpEngine::Compile(RegExpTree* regexp) {
  RegExpNode<Char>* end = new EndNode<Char>();
  RegExpCompiler<Char> compiler;
  return compiler.Compile(regexp, end);
}


template
RegExpNode<const char>* RegExpEngine::Compile<const char>(RegExpTree* regexp);

template
RegExpNode<const uc16>* RegExpEngine::Compile<const uc16>(RegExpTree* regexp);

template
bool RegExpEngine::Execute<const char>(RegExpNode<const char>* start,
                                       Vector<const char> input);

template
bool RegExpEngine::Execute<const uc16>(RegExpNode<const uc16>* start,
                                       Vector<const uc16> input);


}}  // namespace v8::internal
