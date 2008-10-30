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

#ifndef V8_JSREGEXP_H_
#define V8_JSREGEXP_H_

namespace v8 { namespace internal {

class RegExpImpl {
 public:
  // Creates a regular expression literal in the old space.
  // This function calls the garbage collector if necessary.
  static Handle<Object> CreateRegExpLiteral(Handle<JSFunction> constructor,
                                            Handle<String> pattern,
                                            Handle<String> flags,
                                            bool* has_pending_exception);

  // Returns a string representation of a regular expression.
  // Implements RegExp.prototype.toString, see ECMA-262 section 15.10.6.4.
  // This function calls the garbage collector if necessary.
  static Handle<String> ToString(Handle<Object> value);

  static Handle<Object> Compile(Handle<JSRegExp> re,
                                Handle<String> pattern,
                                Handle<String> flags);

  // Implements RegExp.prototype.exec(string) function.
  // See ECMA-262 section 15.10.6.2.
  // This function calls the garbage collector if necessary.
  static Handle<Object> Exec(Handle<JSRegExp> regexp,
                             Handle<String> subject,
                             Handle<Object> index);

  // Call RegExp.prototyp.exec(string) in a loop.
  // Used by String.prototype.match and String.prototype.replace.
  // This function calls the garbage collector if necessary.
  static Handle<Object> ExecGlobal(Handle<JSRegExp> regexp,
                                   Handle<String> subject);

  static Handle<Object> AtomCompile(Handle<JSRegExp> re,
                                    Handle<String> pattern,
                                    JSRegExp::Flags flags);

  static Handle<Object> AtomExec(Handle<JSRegExp> regexp,
                                 Handle<String> subject,
                                 Handle<Object> index);

  static Handle<Object> AtomExecGlobal(Handle<JSRegExp> regexp,
                                       Handle<String> subject);

  static Handle<Object> JsreCompile(Handle<JSRegExp> re,
                                    Handle<String> pattern,
                                    JSRegExp::Flags flags);

  static Handle<Object> JsreExec(Handle<JSRegExp> regexp,
                                 Handle<String> subject,
                                 Handle<Object> index);

  static Handle<Object> JsreExecGlobal(Handle<JSRegExp> regexp,
                                       Handle<String> subject);

  static void NewSpaceCollectionPrologue();
  static void OldSpaceCollectionPrologue();

 private:
  // Converts a source string to a 16 bit flat string.  The string
  // will be either sequential or it will be a SlicedString backed
  // by a flat string.
  static Handle<String> StringToTwoByte(Handle<String> pattern);
  static Handle<String> CachedStringToTwoByte(Handle<String> pattern);

  static String* last_ascii_string_;
  static String* two_byte_cached_string_;

  // Returns the caputure from the re.
  static int JsreCapture(Handle<JSRegExp> re);
  static ByteArray* JsreInternal(Handle<JSRegExp> re);

  // Call jsRegExpExecute once
  static Handle<Object> JsreExecOnce(Handle<JSRegExp> regexp,
                                     int num_captures,
                                     Handle<String> subject,
                                     int previous_index,
                                     const uc16* utf8_subject,
                                     int* ovector,
                                     int ovector_length);

  // Set the subject cache.  The previous string buffer is not deleted, so the
  // caller should ensure that it doesn't leak.
  static void SetSubjectCache(String* subject, char* utf8_subject,
                              int uft8_length, int character_position,
                              int utf8_position);

  // A one element cache of the last utf8_subject string and its length.  The
  // subject JS String object is cached in the heap.  We also cache a
  // translation between position and utf8 position.
  static char* utf8_subject_cache_;
  static int utf8_length_cache_;
  static int utf8_position_;
  static int character_position_;
};


template <typename Char> class RegExpNode;
class CharacterClassAllocator;


class CharacterClass {
 public:

  enum Type { EMPTY = 0, FIELD = 1, RANGES = 2, UNION = 3 };

  // A closed range from and including 'from', to and including 'to'.
  class Range {
   public:
    Range() : from_(0), to_(0) { }
    Range(uc16 from, uc16 to) : from_(from), to_(to) { ASSERT(from <= to); }
    uc16 from() { return from_; }
    uc16 to() { return to_; }
   private:
    uc16 from_;
    uc16 to_;
  };

  CharacterClass() : type_(EMPTY) { }

  explicit CharacterClass(Type type) : type_(type) { }

  bool Contains(uc16 c);

  // Returns a character class with a single bit set
  static inline CharacterClass SingletonField(uc16 chr);

  // Returns a bitfield character class with a closed range set.  The
  // range must fit within one field, that is, fit between two adjacent
  // kFieldMax-aligned boundaries.
  static inline CharacterClass RangeField(Range range);

  static inline CharacterClass Union(CharacterClass* left,
                                     CharacterClass* right);

  // Initializes an empty charclass as a bitfield containing the
  // specified ranges.
  void InitializeFieldFrom(Vector<Range> ranges);

  // Initializes this character class to be the specified ranges.
  // This class must be empty.
  void InitializeRangesFrom(Vector<Range> ranges,
                            CharacterClassAllocator* alloc);

  // Creates a new character class containing the specified ranges
  // and allocating any sub-classes using the specified allocator.
  static CharacterClass Ranges(Vector<Range> boundaries,
                               CharacterClassAllocator* alloc);

  // Returns one of the built-in character classes such as '\w' or
  // '\S'.
  static CharacterClass* GetCharacterClass(uc16 tag);

  inline void write_nibble(int index, byte value);
  inline byte read_nibble(int index);

  static inline unsigned segment_of(uc16 value);
  static inline uc16 segment_start(unsigned segment);

 private:
  static const int kCharSize = 16;
  static const int kFieldSegmentIndexWidth = 10;
  static const int kFieldWidth = kCharSize - kFieldSegmentIndexWidth;
  static const int kFieldMax = (1 << kFieldWidth);
  static const int kSegmentMask = (1 << kFieldWidth) - 1;
  static const int kNibbleCount = kFieldMax / 4;
  STATIC_ASSERT(kFieldMax == 8 * sizeof(uint64_t));

  Type type() { return type_; }

  static inline uint64_t long_bit(int index) {
    return static_cast<uint64_t>(1) << index;
  }

  Type type_: 2;
  unsigned segment_ : 10;
  unsigned count_ : 4;
  union {
    // These have the same type to make it easier to change one without
    // touching the other.
    uint64_t u_field;
    uint64_t u_ranges;
    struct {
      CharacterClass* left;
      CharacterClass* right;
    } u_union;
  } data_;
};


STATIC_ASSERT(sizeof(CharacterClass) == 3 * kIntSize);


class CharacterClassAllocator {
 public:
  virtual CharacterClass* Allocate() = 0;
  virtual ~CharacterClassAllocator() { }
};


template <int kCount>
class StaticCharacterClassAllocator: public CharacterClassAllocator {
 public:
  StaticCharacterClassAllocator() : used_(0) { }
  virtual CharacterClass* Allocate();
 private:
  int used_;
  CharacterClass preallocated_[kCount];
};


class RegExpEngine: public AllStatic {
 public:
  template <typename Char>
  static RegExpNode<Char>* Compile(RegExpTree* regexp);

  template <typename Char>
  static bool Execute(RegExpNode<Char>* start, Vector<Char> input);
};


} }  // namespace v8::internal

#endif  // V8_JSREGEXP_H_
