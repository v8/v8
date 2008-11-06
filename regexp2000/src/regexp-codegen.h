// Copyright 2008 the V8 project authors. All rights reserved.
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

#ifndef V8_REGEXP_CODEGEN_H_
#define V8_REGEXP_CODEGEN_H_

namespace v8 { namespace internal {


struct DisjunctDecisionRow {
  RegExpCharacterClass cc;
  Label* on_match;
  int actions;
};


// Actions.  These are things that can be specified to happen on a
// match or failure when generating code.
static const int kNoAction                      = 0x00;
// Pop the current position in the subject from the backtracking stack.
static const int kPopCurrentPosition            = 0x01;
// Push the current position in the subject onto the backtracking stack.
static const int kPushCurrentPosition           = 0x04;
// As above, but in CheckCharacter and CheckCharacterClass which take an
// offset, the offset is added to the current position first.
static const int kPushCurrentPositionPlusOffset = 0x06;
// Pop a new state from the stack and go to it.
static const int kBacktrack                     = 0x08;
// Goto the label that is given in another argument.
static const int kGotoLabel                     = 0x10;
// Advance current position (by the offset + 1).
static const int kAdvanceCurrentPosition        = 0x20;
// Push the label that is given in another argument onto the backtrack stack.
static const int kPushBacktrackState            = 0x40;
// The entire regexp has succeeded.
static const int kSuccess                       = 0x80;
// The entire regexp has failed to match.
static const int kFailure                      = 0x100;


template <typename SubjectChar>
class RegexpCodeGenerator {
 public:
  RegexpCodeGenerator() { }
  virtual ~RegexpCodeGenerator() { }
  virtual void Bind(Label* label) = 0;
  // Writes the current position in the subject string into the given index of
  // the captures array.  The old value is pushed to the stack.
  virtual void WriteCaptureInfo(int index) = 0;
  // Pops the the given index of the capture array from the stack.
  virtual void PopCaptureInfo(int index) = 0;
  // Pushes the current position in the subject string onto the stack for later
  // retrieval.
  virtual void PushCurrentPosition() = 0;
  // Pops the current position in the subject string.
  virtual void PopCurrentPosition() = 0;
  // Check the current character for a match with a character class.  Take
  // one of the actions depending on whether there is a match.
  virtual void AdvanceCurrentPosition(int by) = 0;
  // Looks at the next character from the subject and performs the corresponding
  // action according to whether it matches.  Success_action can only be one of
  // kAdvanceCurrentPosition or kNoAction.
  virtual void CheckCharacterClass(
      RegExpCharacterClass* cclass,
      int success_action,
      int fail_action,
      int offset,                      // Offset from current subject position.
      Label* fail_state = NULL) = 0;   // Used by kGotoLabel on failure.
  // Check the current character for a match with a character class.  Take
  // one of the actions depending on whether there is a match.
  virtual void CheckCharacters(
      Vector<uc16> string,
      int fail_action,
      int offset,
      Label* state = NULL) = 0;        // Used by kGotoLabel on failure.
  // Perform an action unconditionally.
  virtual void Action(
      int action,
      Label* state = NULL) = 0;
  // Peek at the next character and find out which of the disjunct character
  // classes it is in.  Perform the corresponding actions on the corresponding
  // label.
  virtual void DisjunctCharacterPeekDispatch(
      Vector<DisjunctDecisionRow> outcomes) = 0;
 private:
};


} }  // namespace v8::internal

#endif  // V8_REGEXP_CODEGEN_H_
