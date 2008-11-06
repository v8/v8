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
    RegExpParseResult parse_result;
    if (!ParseRegExp(&buffer, &parse_result)) {
      // Throw an exception if we fail to parse the pattern.
      return CreateRegExpException(re,
                                   pattern,
                                   parse_result.error,
                                   "malformed_regexp");
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


class ExecutionState;


class DotPrinter {
 public:
  DotPrinter() : stream_(&alloc_) { }
  void PrintNode(const char* label, RegExpNode* node);
  void Visit(RegExpNode* node);
  StringStream* stream() { return &stream_; }
 private:
  HeapStringAllocator alloc_;
  StringStream stream_;
  std::set<RegExpNode*> seen_;
};


class RegExpCompiler {
 public:
  explicit RegExpCompiler(int capture_count)
    : next_register_(2 * capture_count) { }

  RegExpNode* Compile(RegExpTree* tree,
                      RegExpNode* on_success,
                      RegExpNode* on_failure) {
    return tree->ToNode(this, on_success, on_failure);
  }

  int AllocateRegister() { return next_register_++; }

 private:
  int next_register_;
};


class RegExpNode: public ZoneObject {
 public:
  virtual ~RegExpNode() { }
  virtual void EmitDot(DotPrinter* out);
};


class SeqRegExpNode: public RegExpNode {
 public:
  explicit SeqRegExpNode(RegExpNode* on_success)
    : on_success_(on_success) { }
  RegExpNode* on_success() { return on_success_; }
 private:
  RegExpNode* on_success_;
};


class EndNode: public RegExpNode {
 public:
  enum Action { ACCEPT, BACKTRACK };
  virtual void EmitDot(DotPrinter* out);
  static EndNode* GetAccept() { return &kAccept; }
  static EndNode* GetBacktrack() { return &kBacktrack; }
 private:
  explicit EndNode(Action action) : action_(action) { }
  Action action_;
  static EndNode kAccept;
  static EndNode kBacktrack;
};


EndNode EndNode::kAccept(ACCEPT);
EndNode EndNode::kBacktrack(BACKTRACK);


class AtomNode: public SeqRegExpNode {
 public:
  AtomNode(Vector<const uc16> data,
           RegExpNode* on_success,
           RegExpNode* on_failure)
    : SeqRegExpNode(on_success),
      on_failure_(on_failure),
      data_(data) { }
  virtual void EmitDot(DotPrinter* out);
  Vector<const uc16> data() { return data_; }
  RegExpNode* on_failure() { return on_failure_; }
 private:
  RegExpNode* on_failure_;
  Vector<const uc16> data_;
};


class BackreferenceNode: public SeqRegExpNode {
 public:
  BackreferenceNode(int start_reg,
                    int end_reg,
                    RegExpNode* on_success,
                    RegExpNode* on_failure)
    : SeqRegExpNode(on_success),
      on_failure_(on_failure),
      start_reg_(start_reg),
      end_reg_(end_reg) { }
  virtual void EmitDot(DotPrinter* out);
  RegExpNode* on_failure() { return on_failure_; }
  int start_register() { return start_reg_; }
  int end_register() { return end_reg_; }
 private:
  RegExpNode* on_failure_;
  int start_reg_;
  int end_reg_;
};


class CharacterClassNode: public SeqRegExpNode {
 public:
  CharacterClassNode(ZoneList<CharacterRange>* ranges,
                     RegExpNode* on_success,
                     RegExpNode* on_failure)
    : SeqRegExpNode(on_success),
      on_failure_(on_failure),
      ranges_(ranges) { }
  virtual void EmitDot(DotPrinter* out);
  ZoneList<CharacterRange>* ranges() { return ranges_; }
  RegExpNode* on_failure() { return on_failure_; }
 private:
  RegExpNode* on_failure_;
  ZoneList<CharacterRange>* ranges_;
};


class Guard: public ZoneObject {
 public:
  enum Relation { LT, GEQ };
  Guard(int reg, Relation op, int value)
    : reg_(reg),
      op_(op),
      value_(value) { }
  int reg() { return reg_; }
  Relation op() { return op_; }
  int value() { return value_; }
 private:
  int reg_;
  Relation op_;
  int value_;
};


class GuardedAlternative {
 public:
  explicit GuardedAlternative(RegExpNode* node) : node_(node), guards_(NULL) { }
  void AddGuard(Guard* guard);
  RegExpNode* node() { return node_; }
  ZoneList<Guard*>* guards() { return guards_; }
 private:
  RegExpNode* node_;
  ZoneList<Guard*>* guards_;
};


void GuardedAlternative::AddGuard(Guard* guard) {
  if (guards_ == NULL)
    guards_ = new ZoneList<Guard*>(1);
  guards_->Add(guard);
}


class ChoiceNode: public RegExpNode {
 public:
  explicit ChoiceNode(int expected_size, RegExpNode* on_failure)
    : on_failure_(on_failure),
      choices_(new ZoneList<GuardedAlternative>(expected_size)) { }
  virtual void EmitDot(DotPrinter* out);
  void AddChild(GuardedAlternative node) { choices()->Add(node); }
  ZoneList<GuardedAlternative>* choices() { return choices_; }
  DispatchTable* table() { return &table_; }
  RegExpNode* on_failure() { return on_failure_; }
 private:
  RegExpNode* on_failure_;
  ZoneList<GuardedAlternative>* choices_;
  DispatchTable table_;
};


class ActionNode: public SeqRegExpNode {
 public:
  enum Type {
    STORE_REGISTER,
    INCREMENT_REGISTER,
    STORE_POSITION,
    BEGIN_SUBMATCH,
    ESCAPE_SUBMATCH,
    END_SUBMATCH
  };
  static ActionNode* StoreRegister(int reg, int val, RegExpNode* on_success) {
    ActionNode* result = new ActionNode(STORE_REGISTER, on_success);
    result->data_.u_store_register.reg_ = reg;
    result->data_.u_store_register.value_ = val;
    return result;
  }
  static ActionNode* IncrementRegister(int reg, RegExpNode* on_success) {
    ActionNode* result = new ActionNode(INCREMENT_REGISTER, on_success);
    result->data_.u_increment_register.reg_ = reg;
    return result;
  }
  static ActionNode* StorePosition(int reg, RegExpNode* on_success) {
    ActionNode* result = new ActionNode(STORE_POSITION, on_success);
    result->data_.u_store_position.reg_ = reg;
    return result;
  }
  static ActionNode* BeginSubmatch(RegExpNode* on_success) {
    return new ActionNode(BEGIN_SUBMATCH, on_success);
  }
  static ActionNode* EscapeSubmatch(RegExpNode* on_success) {
    return new ActionNode(ESCAPE_SUBMATCH, on_success);
  }
  static ActionNode* EndSubmatch(RegExpNode* on_success) {
    return new ActionNode(END_SUBMATCH, on_success);
  }
  virtual void EmitDot(DotPrinter* out);
  Type type() { return type_; }
 private:
  ActionNode(Type type, RegExpNode* on_success)
    : SeqRegExpNode(on_success),
      type_(type) { }
  Type type_;
  union {
    struct {
      int reg_;
      int value_;
    } u_store_register;
    struct {
      int reg_;
    } u_increment_register;
    struct {
      int reg_;
    } u_store_position;
  } data_;
};


// -------------------------------------------------------------------
// Dot/dotty output

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
  stream()->Add("\"]; \n");
  Visit(node);
  stream()->Add("}\n");
  printf("%s", *(stream()->ToCString()));
}


void DotPrinter::Visit(RegExpNode* node) {
  if (seen_.find(node) != seen_.end())
    return;
  seen_.insert(node);
  node->EmitDot(this);
}


void RegExpNode::EmitDot(DotPrinter* out) {
  UNIMPLEMENTED();
}


static void PrintOnFailure(DotPrinter* out,
                           RegExpNode* from,
                           RegExpNode* on_failure) {
  if (on_failure == EndNode::GetBacktrack()) return;
  out->stream()->Add("  n%p -> n%p [style=dotted];\n", from, on_failure);
  out->Visit(on_failure);
}


void ChoiceNode::EmitDot(DotPrinter* out) {
  out->stream()->Add("  n%p [label=\"?\", shape=circle];\n", this);
  PrintOnFailure(out, this, this->on_failure());
  for (int i = 0; i < choices()->length(); i++) {
    GuardedAlternative alt = choices()->at(i);
    out->stream()->Add("  n%p -> n%p [label=\"%i",
                       this,
                       alt.node(),
                       i);
    if (alt.guards() != NULL) {
      out->stream()->Add(" [");
      for (int j = 0; j < alt.guards()->length(); j++) {
        if (j > 0) out->stream()->Add(" ");
        Guard* guard = alt.guards()->at(j);
        switch (guard->op()) {
          case Guard::GEQ:
            out->stream()->Add("$%i &#8805; %i", guard->reg(), guard->value());
            break;
          case Guard::LT:
            out->stream()->Add("$%i < %i", guard->reg(), guard->value());
            break;
        }
      }
      out->stream()->Add("]");
    }
    out->stream()->Add("\"];\n");
    out->Visit(choices()->at(i).node());
  }
}


void AtomNode::EmitDot(DotPrinter* out) {
  out->stream()->Add("  n%p [label=\"'%w'\", shape=doubleoctagon];\n",
                     this,
                     data());
  out->stream()->Add("  n%p -> n%p;\n", this, this->on_success());
  out->Visit(this->on_success());
  PrintOnFailure(out, this, this->on_failure());
}


void BackreferenceNode::EmitDot(DotPrinter* out) {
  out->stream()->Add("  n%p [label=\"$%i..$%i\", shape=doubleoctagon];\n",
                     this,
                     start_register(),
                     end_register());
  out->stream()->Add("  n%p -> n%p;\n", this, this->on_success());
  out->Visit(this->on_success());
  PrintOnFailure(out, this, this->on_failure());
}


void EndNode::EmitDot(DotPrinter* out) {
  out->stream()->Add("  n%p [style=bold, shape=point];\n", this);
}


void CharacterClassNode::EmitDot(DotPrinter* out) {
  out->stream()->Add("  n%p [label=\"[...]\"];\n", this);
  out->stream()->Add("  n%p -> n%p;\n", this, this->on_success());
  out->Visit(this->on_success());
  PrintOnFailure(out, this, this->on_failure());
}


void ActionNode::EmitDot(DotPrinter* out) {
  out->stream()->Add("  n%p [", this);
  switch (type()) {
    case STORE_REGISTER:
      out->stream()->Add("label=\"$%i:=%i\", shape=box",
                         data_.u_store_register.reg_,
                         data_.u_store_register.value_);
      break;
    case INCREMENT_REGISTER:
      out->stream()->Add("label=\"$%i++\", shape=box",
                         data_.u_increment_register.reg_);
      break;
    case STORE_POSITION:
      out->stream()->Add("label=\"$%i:=$pos\", shape=box",
                         data_.u_store_position.reg_);
      break;
    case BEGIN_SUBMATCH:
      out->stream()->Add("label=\"begin\", shape=septagon");
      break;
    case ESCAPE_SUBMATCH:
      out->stream()->Add("label=\"escape\", shape=septagon");
      break;
    case END_SUBMATCH:
      out->stream()->Add("label=\"end\", shape=septagon");
      break;
  }
  out->stream()->Add("];\n");
  out->stream()->Add("  n%p -> n%p;\n", this, this->on_success());
  out->Visit(this->on_success());
}


// -------------------------------------------------------------------
// Tree to graph conversion


RegExpNode* RegExpAtom::ToNode(RegExpCompiler* compiler,
                               RegExpNode* on_success,
                               RegExpNode* on_failure) {
  return new AtomNode(data(), on_success, on_failure);
}


RegExpNode* RegExpCharacterClass::ToNode(RegExpCompiler* compiler,
                                         RegExpNode* on_success,
                                         RegExpNode* on_failure) {
  return new CharacterClassNode(ranges(), on_success, on_failure);
}


RegExpNode* RegExpDisjunction::ToNode(RegExpCompiler* compiler,
                                      RegExpNode* on_success,
                                      RegExpNode* on_failure) {
  ZoneList<RegExpTree*>* children = nodes();
  int length = children->length();
  ChoiceNode* result = new ChoiceNode(length, on_failure);
  for (int i = 0; i < length; i++) {
    GuardedAlternative child(compiler->Compile(children->at(i),
                                               on_success,
                                               on_failure));
    result->AddChild(child);
  }
  return result;
}


RegExpNode* RegExpQuantifier::ToNode(RegExpCompiler* compiler,
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
  bool has_min = min() > 0;
  bool has_max = max() < RegExpQuantifier::kInfinity;
  bool needs_counter = has_min || has_max;
  int reg = needs_counter ? compiler->AllocateRegister() : -1;
  ChoiceNode* center = new ChoiceNode(2, on_failure);
  RegExpNode* loop_return = needs_counter
      ? static_cast<RegExpNode*>(ActionNode::IncrementRegister(reg, center))
      : static_cast<RegExpNode*>(center);
  RegExpNode* body_node = compiler->Compile(body(), loop_return, on_failure);
  GuardedAlternative body_alt(body_node);
  if (has_max) {
    Guard* body_guard = new Guard(reg, Guard::LT, max());
    body_alt.AddGuard(body_guard);
  }
  GuardedAlternative rest_alt(on_success);
  if (has_min) {
    Guard* rest_guard = new Guard(reg, Guard::GEQ, min());
    rest_alt.AddGuard(rest_guard);
  }
  if (is_greedy()) {
    center->AddChild(body_alt);
    center->AddChild(rest_alt);
  } else {
    center->AddChild(rest_alt);
    center->AddChild(body_alt);
  }
  if (needs_counter) {
    return ActionNode::StoreRegister(reg, 0, center);
  } else {
    return center;
  }
}


RegExpNode* RegExpAssertion::ToNode(RegExpCompiler* compiler,
                                    RegExpNode* on_success,
                                    RegExpNode* on_failure) {
  // TODO(self): implement assertions.
  return on_success;
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
  if (is_positive()) {
    RegExpNode* proceed = ActionNode::EndSubmatch(on_success);
    RegExpNode* escape = ActionNode::EscapeSubmatch(on_failure);
    RegExpNode* body_node = compiler->Compile(body(), proceed, escape);
    return ActionNode::BeginSubmatch(body_node);
  } else {
    RegExpNode* failed = ActionNode::EscapeSubmatch(on_success);
    RegExpNode* succeeded = ActionNode::EndSubmatch(on_failure);
    RegExpNode* body_node = compiler->Compile(body(), succeeded, failed);
    return ActionNode::BeginSubmatch(body_node);
  }
}


RegExpNode* RegExpCapture::ToNode(RegExpCompiler* compiler,
                                  RegExpNode* on_success,
                                  RegExpNode* on_failure) {
  int start_reg = RegExpCapture::StartRegister(index());
  int end_reg = RegExpCapture::EndRegister(index());
  RegExpNode* store_end = ActionNode::StorePosition(end_reg, on_success);
  RegExpNode* body_node = compiler->Compile(body(), store_end, on_failure);
  return ActionNode::StorePosition(start_reg, body_node);
}


RegExpNode* RegExpAlternative::ToNode(RegExpCompiler* compiler,
                                      RegExpNode* on_success,
                                      RegExpNode* on_failure) {
  ZoneList<RegExpTree*>* children = nodes();
  RegExpNode* current = on_success;
  for (int i = children->length() - 1; i >= 0; i--) {
    current = compiler->Compile(children->at(i), current, on_failure);
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


void RegExpEngine::DotPrint(const char* label, RegExpNode* node) {
  DotPrinter printer;
  printer.PrintNode(label, node);
}


RegExpNode* RegExpEngine::Compile(RegExpParseResult* input) {
  RegExpCompiler compiler(input->capture_count);
  RegExpNode* node = compiler.Compile(input->tree,
                                      EndNode::GetAccept(),
                                      EndNode::GetBacktrack());
  return node;
}


}}  // namespace v8::internal
