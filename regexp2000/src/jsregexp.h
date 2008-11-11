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

  // Stores an uncompiled RegExp pattern in the JSRegExp object.
  // It will be compiled by JSCRE when first executed.
  static Handle<Object> JsrePrepare(Handle<JSRegExp> re,
                                    Handle<String> pattern,
                                    JSRegExp::Flags flags);

  // Compile the pattern using JSCRE and store the result in the
  // JSRegExp object.
  static Handle<Object> JsreCompile(Handle<JSRegExp> re);

  static Handle<Object> AtomCompile(Handle<JSRegExp> re,
                                    Handle<String> pattern,
                                    JSRegExp::Flags flags,
                                    Handle<String> match_pattern);
  static Handle<Object> AtomExec(Handle<JSRegExp> regexp,
                                 Handle<String> subject,
                                 Handle<Object> index);

  static Handle<Object> AtomExecGlobal(Handle<JSRegExp> regexp,
                                       Handle<String> subject);

  static Handle<Object> JsreCompile(Handle<JSRegExp> re,
                                    Handle<String> pattern,
                                    JSRegExp::Flags flags);

  // Execute a compiled JSCRE pattern.
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


class CharacterRange {
 public:
  // For compatibility with the CHECK_OK macro
  CharacterRange(void* null) { ASSERT_EQ(NULL, null); }  //NOLINT
  CharacterRange(uc16 from, uc16 to)
    : from_(from),
      to_(to) {
  }
  static void AddClassEscape(uc16 type, ZoneList<CharacterRange>* ranges);
  static inline CharacterRange Singleton(uc16 value) {
    return CharacterRange(value, value);
  }
  static inline CharacterRange Range(uc16 from, uc16 to) {
    ASSERT(from <= to);
    return CharacterRange(from, to);
  }
  uc16 from() { return from_; }
  void set_from(uc16 value) { from_ = value; }
  uc16 to() { return to_; }
  void set_to(uc16 value) { to_ = value; }
  bool is_valid() { return from_ <= to_; }
  bool IsSingleton() { return (from_ == to_); }
 private:
  uc16 from_;
  uc16 to_;
};


template <typename Node, class Callback>
static void DoForEach(Node* node, Callback callback);


// A zone splay tree.  The config type parameter encapsulates the
// different configurations of a concrete splay tree:
//
//   typedef Key: the key type
//   typedef Value: the value type
//   static const kNoKey: the dummy key used when no key is set
//   static const kNoValue: the dummy value used to initialize nodes
//   int (Compare)(Key& a, Key& b) -> {-1, 0, 1}: comparison function
//
template <typename Config>
class ZoneSplayTree : public ZoneObject {
 public:
  typedef typename Config::Key Key;
  typedef typename Config::Value Value;

  class Locator;

  ZoneSplayTree() : root_(NULL) { }

  // Inserts the given key in this tree with the given value.  Returns
  // true if a node was inserted, otherwise false.  If found the locator
  // is enabled and provides access to the mapping for the key.
  bool Insert(const Key& key, Locator* locator);

  // Looks up the key in this tree and returns true if it was found,
  // otherwise false.  If the node is found the locator is enabled and
  // provides access to the mapping for the key.
  bool Find(const Key& key, Locator* locator);

  // Finds the mapping with the greatest key less than or equal to the
  // given key.
  bool FindGreatestLessThan(const Key& key, Locator* locator);

  // Find the mapping with the greatest key in this tree.
  bool FindGreatest(Locator* locator);

  // Finds the mapping with the least key greater than or equal to the
  // given key.
  bool FindLeastGreaterThan(const Key& key, Locator* locator);

  // Find the mapping with the least key in this tree.
  bool FindLeast(Locator* locator);

  // Remove the node with the given key from the tree.
  bool Remove(const Key& key);

  bool is_empty() { return root_ == NULL; }

  // Perform the splay operation for the given key. Moves the node with
  // the given key to the top of the tree.  If no node has the given
  // key, the last node on the search path is moved to the top of the
  // tree.
  void Splay(const Key& key);

  class Node : public ZoneObject {
   public:
    Node(const Key& key, const Value& value)
      : key_(key),
        value_(value),
        left_(NULL),
        right_(NULL) { }
     Key key() { return key_; }
     Value value() { return value_; }
     Node* left() { return left_; }
     Node* right() { return right_; }
   private:
    friend class ZoneSplayTree;
    friend class Locator;
    Key key_;
    Value value_;
    Node* left_;
    Node* right_;
  };

  // A locator provides access to a node in the tree without actually
  // exposing the node.
  class Locator {
   public:
    Locator(Node* node) : node_(node) { }
    Locator() : node_(NULL) { }
    const Key& key() { return node_->key_; }
    Value& value() { return node_->value_; }
    void set_value(const Value& value) { node_->value_ = value; }
    inline void bind(Node* node) { node_ = node; }
   private:
    Node* node_;
  };

  template <class Callback>
  void ForEach(Callback c) {
    DoForEach<typename ZoneSplayTree<Config>::Node, Callback>(root_, c);
  }

 private:
  Node* root_;
};


// A set of unsigned integers that behaves especially well on small
// integers (< 32).  May do zone-allocation.
class OutSet {
 public:
  OutSet() : first_(0), remaining_(NULL) { }
  explicit inline OutSet(unsigned value);
  static inline OutSet empty() { return OutSet(); }
  void Set(unsigned value);
  bool Get(unsigned value);
  OutSet Clone();
  static const unsigned kFirstLimit = 32;
 private:
  OutSet(uint32_t first, ZoneList<unsigned>* remaining)
    : first_(first), remaining_(remaining) { }
  uint32_t first_;
  ZoneList<unsigned>* remaining_;
};


// A mapping from integers, specified as ranges, to a set of integers.
// Used for mapping character ranges to choices.
class DispatchTable {
 public:
  class Entry {
   public:
    Entry()
      : from_(0), to_(0) { }
    Entry(uc16 from, uc16 to, OutSet out_set)
      : from_(from), to_(to), out_set_(out_set) { }
    inline Entry(uc16 from, uc16 to, unsigned value);
    uc16 from() { return from_; }
    uc16 to() { return to_; }
    void set_to(uc16 value) { to_ = value; }
    void AddValue(int value) { out_set_.Set(value); }
    OutSet out_set() { return out_set_; }
   private:
    uc16 from_;
    uc16 to_;
    OutSet out_set_;
  };
  class Config {
   public:
    typedef uc16 Key;
    typedef Entry Value;
    static const uc16 kNoKey;
    static const Entry kNoValue;
    static inline int Compare(uc16 a, uc16 b) {
      if (a == b)
        return 0;
      else if (a < b)
        return -1;
      else
        return 1;
    }
  };
  void AddRange(CharacterRange range, int value);
  OutSet Get(uc16 value);
  void Dump();
 private:
  ZoneSplayTree<Config>* tree() { return &tree_; }
  ZoneSplayTree<Config> tree_;
};


struct RegExpParseResult {
  RegExpTree* tree;
  bool has_character_escapes;
  Handle<String> error;
  int capture_count;
};


class RegExpEngine: public AllStatic {
 public:
  static RegExpNode* Compile(RegExpParseResult* input);
  static void DotPrint(const char* label, RegExpNode* node);
};


} }  // namespace v8::internal

#endif  // V8_JSREGEXP_H_
