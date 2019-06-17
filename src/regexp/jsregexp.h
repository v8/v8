// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_JSREGEXP_H_
#define V8_REGEXP_JSREGEXP_H_

#include "src/objects/js-regexp.h"

namespace v8 {
namespace internal {

class RegExpNode;
class RegExpTree;

inline bool IgnoreCase(JSRegExp::Flags flags) {
  return (flags & JSRegExp::kIgnoreCase) != 0;
}

inline bool IsUnicode(JSRegExp::Flags flags) {
  return (flags & JSRegExp::kUnicode) != 0;
}

inline bool IsSticky(JSRegExp::Flags flags) {
  return (flags & JSRegExp::kSticky) != 0;
}

inline bool IsGlobal(JSRegExp::Flags flags) {
  return (flags & JSRegExp::kGlobal) != 0;
}

inline bool DotAll(JSRegExp::Flags flags) {
  return (flags & JSRegExp::kDotAll) != 0;
}

inline bool Multiline(JSRegExp::Flags flags) {
  return (flags & JSRegExp::kMultiline) != 0;
}

inline bool NeedsUnicodeCaseEquivalents(JSRegExp::Flags flags) {
  // Both unicode and ignore_case flags are set. We need to use ICU to find
  // the closure over case equivalents.
  return IsUnicode(flags) && IgnoreCase(flags);
}

class RegExpImpl final {
 public:
  // Whether the irregexp engine generates native code or interpreter bytecode.
  static bool UsesNativeRegExp() { return !FLAG_regexp_interpret_all; }

  // Returns a string representation of a regular expression.
  // Implements RegExp.prototype.toString, see ECMA-262 section 15.10.6.4.
  // This function calls the garbage collector if necessary.
  static Handle<String> ToString(Handle<Object> value);

  // Parses the RegExp pattern and prepares the JSRegExp object with
  // generic data and choice of implementation - as well as what
  // the implementation wants to store in the data field.
  // Returns false if compilation fails.
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> Compile(
      Isolate* isolate, Handle<JSRegExp> re, Handle<String> pattern,
      JSRegExp::Flags flags);

  // See ECMA-262 section 15.10.6.2.
  // This function calls the garbage collector if necessary.
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Object> Exec(
      Isolate* isolate, Handle<JSRegExp> regexp, Handle<String> subject,
      int index, Handle<RegExpMatchInfo> last_match_info);

  // Prepares a JSRegExp object with Irregexp-specific data.
  static void IrregexpInitialize(Isolate* isolate, Handle<JSRegExp> re,
                                 Handle<String> pattern, JSRegExp::Flags flags,
                                 int capture_register_count);

  static void AtomCompile(Isolate* isolate, Handle<JSRegExp> re,
                          Handle<String> pattern, JSRegExp::Flags flags,
                          Handle<String> match_pattern);

  static int AtomExecRaw(Isolate* isolate, Handle<JSRegExp> regexp,
                         Handle<String> subject, int index, int32_t* output,
                         int output_size);

  static Handle<Object> AtomExec(Isolate* isolate, Handle<JSRegExp> regexp,
                                 Handle<String> subject, int index,
                                 Handle<RegExpMatchInfo> last_match_info);

  enum IrregexpResult { RE_FAILURE = 0, RE_SUCCESS = 1, RE_EXCEPTION = -1 };

  // Prepare a RegExp for being executed one or more times (using
  // IrregexpExecOnce) on the subject.
  // This ensures that the regexp is compiled for the subject, and that
  // the subject is flat.
  // Returns the number of integer spaces required by IrregexpExecOnce
  // as its "registers" argument.  If the regexp cannot be compiled,
  // an exception is set as pending, and this function returns negative.
  static int IrregexpPrepare(Isolate* isolate, Handle<JSRegExp> regexp,
                             Handle<String> subject);

  // Execute a regular expression on the subject, starting from index.
  // If matching succeeds, return the number of matches.  This can be larger
  // than one in the case of global regular expressions.
  // The captures and subcaptures are stored into the registers vector.
  // If matching fails, returns RE_FAILURE.
  // If execution fails, sets a pending exception and returns RE_EXCEPTION.
  static int IrregexpExecRaw(Isolate* isolate, Handle<JSRegExp> regexp,
                             Handle<String> subject, int index, int32_t* output,
                             int output_size);

  // Execute an Irregexp bytecode pattern.
  // On a successful match, the result is a JSArray containing
  // captured positions.  On a failure, the result is the null value.
  // Returns an empty handle in case of an exception.
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> IrregexpExec(
      Isolate* isolate, Handle<JSRegExp> regexp, Handle<String> subject,
      int index, Handle<RegExpMatchInfo> last_match_info);

  // Set last match info.  If match is nullptr, then setting captures is
  // omitted.
  static Handle<RegExpMatchInfo> SetLastMatchInfo(
      Isolate* isolate, Handle<RegExpMatchInfo> last_match_info,
      Handle<String> subject, int capture_count, int32_t* match);

  class GlobalCache {
   public:
    GlobalCache(Handle<JSRegExp> regexp,
                Handle<String> subject,
                Isolate* isolate);

    V8_INLINE ~GlobalCache();

    // Fetch the next entry in the cache for global regexp match results.
    // This does not set the last match info.  Upon failure, nullptr is
    // returned. The cause can be checked with Result().  The previous result is
    // still in available in memory when a failure happens.
    V8_INLINE int32_t* FetchNext();

    V8_INLINE int32_t* LastSuccessfulMatch();

    V8_INLINE bool HasException() { return num_matches_ < 0; }

   private:
    int AdvanceZeroLength(int last_index);

    int num_matches_;
    int max_matches_;
    int current_match_index_;
    int registers_per_match_;
    // Pointer to the last set of captures.
    int32_t* register_array_;
    int register_array_size_;
    Handle<JSRegExp> regexp_;
    Handle<String> subject_;
    Isolate* isolate_;
  };

  // For acting on the JSRegExp data FixedArray.
  static int IrregexpMaxRegisterCount(FixedArray re);
  static void SetIrregexpMaxRegisterCount(FixedArray re, int value);
  static void SetIrregexpCaptureNameMap(FixedArray re,
                                        Handle<FixedArray> value);
  static int IrregexpNumberOfCaptures(FixedArray re);
  static int IrregexpNumberOfRegisters(FixedArray re);
  static ByteArray IrregexpByteCode(FixedArray re, bool is_one_byte);
  static Code IrregexpNativeCode(FixedArray re, bool is_one_byte);

  // Limit the space regexps take up on the heap.  In order to limit this we
  // would like to keep track of the amount of regexp code on the heap.  This
  // is not tracked, however.  As a conservative approximation we track the
  // total regexp code compiled including code that has subsequently been freed
  // and the total executable memory at any point.
  static const size_t kRegExpExecutableMemoryLimit = 16 * MB;
  static const size_t kRegExpCompiledLimit = 1 * MB;
  static const int kRegExpTooLargeToOptimize = 20 * KB;

 private:
  static bool CompileIrregexp(Isolate* isolate, Handle<JSRegExp> re,
                              Handle<String> sample_subject, bool is_one_byte);
  static inline bool EnsureCompiledIrregexp(Isolate* isolate,
                                            Handle<JSRegExp> re,
                                            Handle<String> sample_subject,
                                            bool is_one_byte);
};

struct RegExpCompileData {
  RegExpCompileData()
      : tree(nullptr),
        node(nullptr),
        simple(true),
        contains_anchor(false),
        capture_count(0) {}
  RegExpTree* tree;
  RegExpNode* node;
  bool simple;
  bool contains_anchor;
  Handle<FixedArray> capture_name_map;
  Handle<String> error;
  int capture_count;
};

class RegExpEngine final : public AllStatic {
 public:
  struct CompilationResult {
    explicit CompilationResult(const char* error_message)
        : error_message(error_message) {}
    CompilationResult(const char* error_message, Object code, int registers)
        : error_message(error_message), code(code), num_registers(registers) {}

    static CompilationResult RegExpTooBig() {
      return CompilationResult("RegExp too big");
    }

    const char* const error_message = nullptr;
    Object const code;
    int const num_registers = 0;
  };

  V8_EXPORT_PRIVATE static CompilationResult Compile(
      Isolate* isolate, Zone* zone, RegExpCompileData* input,
      JSRegExp::Flags flags, Handle<String> pattern,
      Handle<String> sample_subject, bool is_one_byte);

  static bool TooMuchRegExpCode(Isolate* isolate, Handle<String> pattern);

  V8_EXPORT_PRIVATE static void DotPrint(const char* label, RegExpNode* node,
                                         bool ignore_case);
};

class RegExpResultsCache final : public AllStatic {
 public:
  enum ResultsCacheType { REGEXP_MULTIPLE_INDICES, STRING_SPLIT_SUBSTRINGS };

  // Attempt to retrieve a cached result.  On failure, 0 is returned as a Smi.
  // On success, the returned result is guaranteed to be a COW-array.
  static Object Lookup(Heap* heap, String key_string, Object key_pattern,
                       FixedArray* last_match_out, ResultsCacheType type);
  // Attempt to add value_array to the cache specified by type.  On success,
  // value_array is turned into a COW-array.
  static void Enter(Isolate* isolate, Handle<String> key_string,
                    Handle<Object> key_pattern, Handle<FixedArray> value_array,
                    Handle<FixedArray> last_match_cache, ResultsCacheType type);
  static void Clear(FixedArray cache);
  static const int kRegExpResultsCacheSize = 0x100;

 private:
  static const int kArrayEntriesPerCacheEntry = 4;
  static const int kStringOffset = 0;
  static const int kPatternOffset = 1;
  static const int kArrayOffset = 2;
  static const int kLastMatchOffset = 3;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_JSREGEXP_H_
