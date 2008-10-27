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
#include "jsregexp.h"
#include "third_party/jscre/pcre.h"
#include "platform.h"
#include "runtime.h"
#include "top.h"
#include "compilation-cache.h"
#include "string-stream.h"
#include <set>

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
  if (!flat_string->IsAsciiRepresentation()) {
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


static JSRegExp::Flags RegExpFlagsFromString(Handle<String> str) {
  int flags = JSRegExp::NONE;
  for (int i = 0; i < str->length(); i++) {
    switch (str->Get(i)) {
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


unibrow::Predicate<unibrow::RegExpSpecialChar, 128> is_reg_exp_special_char;


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
    bool is_atom = !flags.is_ignore_case();
    for (int i = 0; is_atom && i < pattern->length(); i++) {
      if (is_reg_exp_special_char.get(pattern->Get(i)))
        is_atom = false;
    }
    if (is_atom) {
      result = AtomCompile(re, pattern, flags);
    } else {
      result = JsreCompile(re, pattern, flags);
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


Handle<Object> RegExpImpl::JsreCompile(Handle<JSRegExp> re,
                                       Handle<String> pattern,
                                       JSRegExp::Flags flags) {
  JSRegExpIgnoreCaseOption case_option = flags.is_ignore_case()
    ? JSRegExpIgnoreCase
    : JSRegExpDoNotIgnoreCase;
  JSRegExpMultilineOption multiline_option = flags.is_multiline()
    ? JSRegExpMultiline
    : JSRegExpSingleLine;

  Handle<String> two_byte_pattern = StringToTwoByte(pattern);

  unsigned number_of_captures;
  const char* error_message = NULL;

  JscreRegExp* code = NULL;
  FlattenString(pattern);

  bool first_time = true;

  while (true) {
    malloc_failure = Failure::Exception();
    code = jsRegExpCompile(two_byte_pattern->GetTwoByteData(),
                           pattern->length(), case_option,
                           multiline_option, &number_of_captures,
                           &error_message, &JSREMalloc, &JSREFree);
    if (code == NULL) {
      if (first_time && malloc_failure->IsRetryAfterGC()) {
        first_time = false;
        if (!Heap::CollectGarbage(malloc_failure->requested(),
                                  malloc_failure->allocation_space())) {
          // TODO(1181417): Fix this.
          V8::FatalProcessOutOfMemory("RegExpImpl::JsreCompile");
        }
        continue;
      }
      if (malloc_failure->IsRetryAfterGC() ||
          malloc_failure->IsOutOfMemoryFailure()) {
        // TODO(1181417): Fix this.
        V8::FatalProcessOutOfMemory("RegExpImpl::JsreCompile");
      } else {
        // Throw an exception.
        Handle<JSArray> array = Factory::NewJSArray(2);
        SetElement(array, 0, pattern);
        SetElement(array, 1, Factory::NewStringFromUtf8(CStrVector(
            (error_message == NULL) ? "Unknown regexp error" : error_message)));
        Handle<Object> regexp_err =
            Factory::NewSyntaxError("malformed_regexp", array);
        return Handle<Object>(Top::Throw(*regexp_err));
      }
    }

    ASSERT(code != NULL);
    // Convert the return address to a ByteArray pointer.
    Handle<ByteArray> internal(
        ByteArray::FromDataStartAddress(reinterpret_cast<Address>(code)));

    Handle<FixedArray> value = Factory::NewFixedArray(2);
    value->set(CAPTURE_INDEX, Smi::FromInt(number_of_captures));
    value->set(INTERNAL_INDEX, *internal);
    Factory::SetRegExpData(re, JSRegExp::JSCRE, pattern, flags, value);

    return re;
  }
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
  SeqRegExpNode(RegExpNode<Char>* next) : next_(next) { }
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
  ChoiceNode(ZoneList<RegExpNode<Char>*>* choices) : choices_(choices) { }
  virtual void EmitDot(DotPrinter<Char>* out);
  virtual bool Step(ExecutionState<Char>* state);
  ZoneList<RegExpNode<Char>*>* choices() { return choices_; }
 private:
  ZoneList<RegExpNode<Char>*>* choices_;
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
    out->stream()->Add("n%p -> n%p [label=\"%i\"];\n", this, choices()->at(i), i);
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
    if (range.is_character_class()) {
      switch (range.from()) {
        case '.':
          state->Advance(1, this->next());
          return true;
        default:
          UNIMPLEMENTED();
      }
    } else {
      if (range.from() <= current && current <= range.to()) {
        state->Advance(1, this->next());
        return true;
      }
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
