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


class RegExpEngine: public AllStatic {
 public:
  template <typename Char>
  static RegExpNode<Char>* Compile(RegExpTree* regexp);

  template <typename Char>
  static bool Execute(RegExpNode<Char>* start, Vector<Char> input);
};


} }  // namespace v8::internal

#endif  // V8_JSREGEXP_H_
