// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

'use strict';

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GlobalRegExp = global.RegExp;
var GlobalRegExpPrototype = GlobalRegExp.prototype;
var RegExpExecJS = GlobalRegExp.prototype.exec;
var matchSymbol = utils.ImportNow("match_symbol");

// -------------------------------------------------------------------

// Property of the builtins object for recording the result of the last
// regexp match.  The property RegExpLastMatchInfo includes the matchIndices
// array of the last successful regexp match (an array of start/end index
// pairs for the match and all the captured substrings), the invariant is
// that there are at least two capture indices.  The array also contains
// the subject string for the last successful match.
// We use a JSObject rather than a JSArray so we don't have to manually update
// its length.
var RegExpLastMatchInfo = {
  REGEXP_NUMBER_OF_CAPTURES: 2,
  REGEXP_LAST_SUBJECT:       "",
  REGEXP_LAST_INPUT:         UNDEFINED,  // Settable with RegExpSetInput.
  CAPTURE0:                  0,
  CAPTURE1:                  0
};

// -------------------------------------------------------------------

// ES#sec-isregexp IsRegExp ( argument )
function IsRegExp(o) {
  if (!IS_RECEIVER(o)) return false;
  var is_regexp = o[matchSymbol];
  if (!IS_UNDEFINED(is_regexp)) return TO_BOOLEAN(is_regexp);
  return IS_REGEXP(o);
}


// ES#sec-regexpinitialize
// Runtime Semantics: RegExpInitialize ( obj, pattern, flags )
function RegExpInitialize(object, pattern, flags) {
  pattern = IS_UNDEFINED(pattern) ? '' : TO_STRING(pattern);
  flags = IS_UNDEFINED(flags) ? '' : TO_STRING(flags);
  %RegExpInitializeAndCompile(object, pattern, flags);
  return object;
}


// This is kind of performance sensitive, so we want to avoid unnecessary
// type checks on inputs. But we also don't want to inline it several times
// manually, so we use a macro :-)
macro RETURN_NEW_RESULT_FROM_MATCH_INFO(MATCHINFO, STRING)
  var numResults = NUMBER_OF_CAPTURES(MATCHINFO) >> 1;
  var start = MATCHINFO[CAPTURE0];
  var end = MATCHINFO[CAPTURE1];
  // Calculate the substring of the first match before creating the result array
  // to avoid an unnecessary write barrier storing the first result.
  var first = %_SubString(STRING, start, end);
  var result = %_RegExpConstructResult(numResults, start, STRING);
  result[0] = first;
  if (numResults == 1) return result;
  var j = REGEXP_FIRST_CAPTURE + 2;
  for (var i = 1; i < numResults; i++) {
    start = MATCHINFO[j++];
    if (start != -1) {
      end = MATCHINFO[j];
      result[i] = %_SubString(STRING, start, end);
    }
    j++;
  }
  return result;
endmacro

// ES#sec-getsubstitution
// GetSubstitution(matched, str, position, captures, replacement)
// Expand the $-expressions in the string and return a new string with
// the result.
function GetSubstitution(matched, string, position, captures, replacement) {
  var matchLength = matched.length;
  var stringLength = string.length;
  var capturesLength = captures.length;
  var tailPos = position + matchLength;
  var result = "";
  var pos, expansion, peek, next, scaledIndex, advance, newScaledIndex;

  var next = %StringIndexOf(replacement, '$', 0);
  if (next < 0) {
    result += replacement;
    return result;
  }

  if (next > 0) result += %_SubString(replacement, 0, next);

  while (true) {
    expansion = '$';
    pos = next + 1;
    if (pos < replacement.length) {
      peek = %_StringCharCodeAt(replacement, pos);
      if (peek == 36) {         // $$
        ++pos;
        result += '$';
      } else if (peek == 38) {  // $& - match
        ++pos;
        result += matched;
      } else if (peek == 96) {  // $` - prefix
        ++pos;
        result += %_SubString(string, 0, position);
      } else if (peek == 39) {  // $' - suffix
        ++pos;
        result += %_SubString(string, tailPos, stringLength);
      } else if (peek >= 48 && peek <= 57) {
        // Valid indices are $1 .. $9, $01 .. $09 and $10 .. $99
        scaledIndex = (peek - 48);
        advance = 1;
        if (pos + 1 < replacement.length) {
          next = %_StringCharCodeAt(replacement, pos + 1);
          if (next >= 48 && next <= 57) {
            newScaledIndex = scaledIndex * 10 + ((next - 48));
            if (newScaledIndex < capturesLength) {
              scaledIndex = newScaledIndex;
              advance = 2;
            }
          }
        }
        if (scaledIndex != 0 && scaledIndex < capturesLength) {
          var capture = captures.at(scaledIndex);
          if (!IS_UNDEFINED(capture)) result += capture;
          pos += advance;
        } else {
          result += '$';
        }
      } else {
        result += '$';
      }
    } else {
      result += '$';
    }

    // Go the the next $ in the replacement.
    next = %StringIndexOf(replacement, '$', pos);

    // Return if there are no more $ characters in the replacement. If we
    // haven't reached the end, we need to append the suffix.
    if (next < 0) {
      if (pos < replacement.length) {
        result += %_SubString(replacement, pos, replacement.length);
      }
      return result;
    }

    // Append substring between the previous and the next $ character.
    if (next > pos) {
      result += %_SubString(replacement, pos, next);
    }
  }
  return result;
}

// -------------------------------------------------------------------

%InstallToContext(["regexp_last_match_info", RegExpLastMatchInfo]);

// -------------------------------------------------------------------
// Internal

var InternalRegExpMatchInfo = {
  REGEXP_NUMBER_OF_CAPTURES: 2,
  REGEXP_LAST_SUBJECT:       "",
  REGEXP_LAST_INPUT:         UNDEFINED,
  CAPTURE0:                  0,
  CAPTURE1:                  0
};

function InternalRegExpMatch(regexp, subject) {
  var matchInfo = %_RegExpExec(regexp, subject, 0, InternalRegExpMatchInfo);
  if (!IS_NULL(matchInfo)) {
    RETURN_NEW_RESULT_FROM_MATCH_INFO(matchInfo, subject);
  }
  return null;
}

function InternalRegExpReplace(regexp, subject, replacement) {
  return %StringReplaceGlobalRegExpWithString(
      subject, regexp, replacement, InternalRegExpMatchInfo);
}

// -------------------------------------------------------------------
// Exports

utils.Export(function(to) {
  to.GetSubstitution = GetSubstitution;
  to.InternalRegExpMatch = InternalRegExpMatch;
  to.InternalRegExpReplace = InternalRegExpReplace;
  to.IsRegExp = IsRegExp;
  to.RegExpInitialize = RegExpInitialize;
  to.RegExpLastMatchInfo = RegExpLastMatchInfo;
});

})
