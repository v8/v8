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


// This file relies on the fact that the following declaration has been made
// in runtime.js:
// const $String = global.String;
// const $NaN = 0/0;


// Set the String function and constructor.
%SetCode($String, function(x) {
  var value = %_ArgumentsLength() == 0 ? '' : ToString(x);
  if (%IsConstructCall()) {
    %_SetValueOf(this, value);
  } else {
    return value;
  }
});

%FunctionSetPrototype($String, new $String());

%AddProperty($String.prototype, "constructor", $String, DONT_ENUM);

%AddProperty($String.prototype, "valueOf", function() {
  if (!IS_STRING(this) && %ClassOf(this) !== 'String')
    throw new $TypeError('String.prototype.valueOf is not generic');
  return %_ValueOf(this);
}, DONT_ENUM);


%AddProperty($String.prototype, "toString", $String.prototype.valueOf, DONT_ENUM);

// ECMA-262 section 15.5.4.5
%AddProperty($String.prototype, "charCodeAt", function(pos) {
  var fast_answer = %_FastCharCodeAt(this, pos);
  if (%_IsSmi(fast_answer)) {
    return fast_answer;
  }
  var subject = ToString(this);
  var index = TO_INTEGER(pos);
  return %StringCharCodeAt(subject, index);
}, DONT_ENUM);


// ECMA-262, section 15.5.4.6
%AddProperty($String.prototype, "concat", function() {
  var len = %_ArgumentsLength();
  var parts = new $Array(len + 1);
  parts[0] = ToString(this);
  for (var i = 0; i < len; i++)
    parts[i + 1] = ToString(%_Arguments(i));
  return parts.join('');
}, DONT_ENUM);

// Match ES3 and Safari
%FunctionSetLength($String.prototype.concat, 1);


// SubString is an internal function that returns the sub string of 'string'.
// If resulting string is of length 1, we use the one character cache
// otherwise we call the runtime system.
function SubString(string, start, end) {
  // Use the one character string cache.
  if (start + 1 == end) return %CharFromCode(%StringCharCodeAt(string, start));
  return %StringSlice(string, start, end);
}


// ECMA-262, section 15.5.4.11
%AddProperty($String.prototype, "replace", function (search, replace) {
  var subject = ToString(this);

  // Delegate to one of the regular expression variants if necessary.
  if (IS_REGEXP(search)) {
    if (IS_FUNCTION(replace)) {
      return StringReplaceRegExpWithFunction(subject, search, replace);
    } else {
      return StringReplaceRegExp(subject, search, replace);
    }
  }

  // Convert the search argument to a string and search for it.
  search = ToString(search);
  var start = %StringIndexOf(subject, search, 0);
  if (start < 0) return subject;
  var end = start + search.length;

  var builder = new StringBuilder();
  // prefix
  builder.add(SubString(subject, 0, start));

  // Compute the string to replace with.
  if (IS_FUNCTION(replace)) {
    builder.add(replace.call(null, search, start, subject));
  } else {
    ExpandReplacement(ToString(replace), subject, [ start, end ], builder);
  }

  // suffix
  builder.add(SubString(subject, end, subject.length));

  return builder.generate();
}, DONT_ENUM);


// Helper function for regular expressions in String.prototype.replace.
function StringReplaceRegExp(subject, regexp, replace) {
  // Compute an array of matches; each match is really a list of
  // captures - pairs of (start, end) indexes into the subject string.
  var matches;
  if (regexp.global) {
    matches = DoRegExpExecGlobal(regexp, subject);
    if (matches.length == 0) return subject;
  } else {
    var captures = DoRegExpExec(regexp, subject, 0);
    if (IS_NULL(captures)) return subject;
    matches = [ captures ];
  }

  // Determine the number of matches.
  var length = matches.length;

  // Build the resulting string of subject slices and replacements.
  var result = new StringBuilder();
  var previous = 0;
  // The caller of StringReplaceRegExp must ensure that replace is not a
  // function.
  replace = ToString(replace);
  for (var i = 0; i < length; i++) {
    var captures = matches[i];
    result.add(SubString(subject, previous, captures[0]));
    ExpandReplacement(replace, subject, captures, result);
    previous = captures[1];  // continue after match
  }
  result.add(SubString(subject, previous, subject.length));
  return result.generate();
};


// Expand the $-expressions in the string and return a new string with
// the result.
function ExpandReplacement(string, subject, captures, builder) {
  var next = %StringIndexOf(string, '$', 0);
  if (next < 0) {
    builder.add(string);
    return;
  }

  // Compute the number of captures; see ECMA-262, 15.5.4.11, p. 102.
  var m = captures.length >> 1;  // includes the match

  if (next > 0) builder.add(SubString(string, 0, next));
  var length = string.length;

  while (true) {
    var expansion = '$';
    var position = next + 1;
    if (position < length) {
      var peek = %StringCharCodeAt(string, position);
      if (peek == 36) {         // $$
        ++position;
      } else if (peek == 38) {  // $& - match
        ++position;
        expansion = SubString(subject, captures[0], captures[1]);
      } else if (peek == 96) {  // $` - prefix
        ++position;
        expansion = SubString(subject, 0, captures[0]);
      } else if (peek == 39) {  // $' - suffix
        ++position;
        expansion = SubString(subject, captures[1], subject.length);
      } else if (peek >= 48 && peek <= 57) {  // $n, 0 <= n <= 9
        ++position;
        var n = peek - 48;
        if (position < length) {
          peek = %StringCharCodeAt(string, position);
          // $nn, 01 <= nn <= 99
          if (n != 0 && peek == 48 || peek >= 49 && peek <= 57) {
            var nn = n * 10 + (peek - 48);
            if (nn < m) {
              // If the two digit capture reference is within range of
              // the captures, we use it instead of the single digit
              // one. Otherwise, we fall back to using the single
              // digit reference. This matches the behavior of
              // SpiderMonkey.
              ++position;
              n = nn;
            }
          }
        }
        if (0 < n && n < m) {
          expansion = CaptureString(subject, captures, n);
          if (IS_UNDEFINED(expansion)) expansion = "";
        } else {
          // Because of the captures range check in the parsing of two
          // digit capture references, we can only enter here when a
          // single digit capture reference is outside the range of
          // captures.
          --position;
        }
      }
    }

    // Append the $ expansion and go the the next $ in the string.
    builder.add(expansion);
    next = %StringIndexOf(string, '$', position);

    // Return if there are no more $ characters in the string. If we
    // haven't reached the end, we need to append the suffix.
    if (next < 0) {
      if (position < length) {
        builder.add(SubString(string, position, length));
      }
      return;
    }

    // Append substring between the previous and the next $ character.
    builder.add(SubString(string, position, next));
  }
};


// Compute the string of a given PCRE capture.
function CaptureString(string, captures, index) {
  // Scale the index.
  var scaled = index << 1;
  // Compute start and end.
  var start = captures[scaled];
  var end = captures[scaled + 1];
  // If either start or end is missing return undefined.
  if (start < 0 || end < 0) return;
  return SubString(string, start, end);
};


// Helper function for replacing regular expressions with the result of a
// function application in String.prototype.replace.  The function application
// must be interleaved with the regexp matching (contrary to ECMA-262
// 15.5.4.11) to mimic SpiderMonkey and KJS behavior when the function uses
// the static properties of the RegExp constructor.  Example:
//     'abcd'.replace(/(.)/g, function() { return RegExp.$1; }
// should be 'abcd' and not 'dddd' (or anything else).
function StringReplaceRegExpWithFunction(subject, regexp, replace) {
  var result = new ReplaceResultBuilder(subject);
  // Captures is an array of pairs of (start, end) indices for the match and
  // any captured substrings.
  var captures = DoRegExpExec(regexp, subject, 0);
  if (IS_NULL(captures)) return subject;

  // There's at least one match.  If the regexp is global, we have to loop
  // over all matches.  The loop is not in C++ code here like the one in
  // RegExp.prototype.exec, because of the interleaved function application.
  // Unfortunately, that means this code is nearly duplicated, here and in
  // jsregexp.cc.
  if (regexp.global) {
    var previous = 0;
    do {
      result.addSpecialSlice(previous, captures[0]);
      result.add(ApplyReplacementFunction(replace, captures, subject));
      // Continue with the next match.
      previous = captures[1];
      // Increment previous if we matched an empty string, as per ECMA-262
      // 15.5.4.10.
      if (captures[0] == captures[1]) previous++;

      // Per ECMA-262 15.10.6.2, if the previous index is greater than the
      // string length, there is no match
      captures = (previous > subject.length)
          ? null
          : DoRegExpExec(regexp, subject, previous);
    } while (!IS_NULL(captures));

    // Tack on the final right substring after the last match, if necessary.
    if (previous < subject.length) {
      result.addSpecialSlice(previous, subject.length);
    }
  } else { // Not a global regexp, no need to loop.
    result.addSpecialSlice(0, captures[0]);
    result.add(ApplyReplacementFunction(replace, captures, subject));
    result.addSpecialSlice(captures[1], subject.length);
  }

  return result.generate();
}


// Helper function to apply a string replacement function once.
function ApplyReplacementFunction(replace, captures, subject) {
  // Compute the parameter list consisting of the match, captures, index,
  // and subject for the replace function invocation.
  var index = captures[0];
  // The number of captures plus one for the match.
  var m = captures.length >> 1;
  if (m == 1) {
    var s = CaptureString(subject, captures, 0);
    return ToString(replace.call(null, s, index, subject));
  }
  var parameters = $Array(m + 2);
  for (var j = 0; j < m; j++) {
    parameters[j] = CaptureString(subject, captures, j);
  }
  parameters[j] = index;
  parameters[j + 1] = subject;
  return ToString(replace.apply(null, parameters));
}


// ECMA-262 section 15.5.4.7
%AddProperty($String.prototype, "indexOf", function(searchString /* position */) {  // length == 1
  var str = ToString(this);
  var searchStr = ToString(searchString);
  var index = 0;
  if (%_ArgumentsLength() > 1) {
    var arg1 = %_Arguments(1);  // position
    index = TO_INTEGER(arg1);
  }
  if (index < 0) index = 0;
  if (index > str.length) index = str.length;
  return %StringIndexOf(str, searchStr, index);
}, DONT_ENUM);


// ECMA-262 section 15.5.4.8
%AddProperty($String.prototype, "lastIndexOf", function(searchString /* position */) {  // length == 1
  var sub = ToString(this);
  var pat = ToString(searchString);
  var index = (%_ArgumentsLength() > 1)
      ? ToNumber(%_Arguments(1) /* position */)
      : $NaN;
  var firstIndex;
  if ($isNaN(index)) {
    firstIndex = sub.length - pat.length;
  } else {
    firstIndex = TO_INTEGER(index);
    if (firstIndex + pat.length > sub.length) {
      firstIndex = sub.length - pat.length;
    }
  }
  return %StringLastIndexOf(sub, pat, firstIndex);
}, DONT_ENUM);


// ECMA-262 section 15.5.4.9
//
// This function is implementation specific.  For now, we do not
// do anything locale specific.
%AddProperty($String.prototype, "localeCompare", function(other) {
  if (%_ArgumentsLength() === 0) return 0;

  var this_str = ToString(this);
  var other_str = ToString(other);
  return %StringLocaleCompare(this_str, other_str);
}, DONT_ENUM);


// ECMA-262 section 15.5.4.10
%AddProperty($String.prototype, "match", function(regexp) {
  if (!IS_REGEXP(regexp)) regexp = new ORIGINAL_REGEXP(regexp);
  var subject = ToString(this);

  if (!regexp.global) return regexp.exec(subject);
  var matches = DoRegExpExecGlobal(regexp, subject);

  // If the regexp did not match, return null.
  if (matches.length == 0) return null;

  // Build the result array.
  var result = new $Array(match_string);
  for (var i = 0; i < matches.length; ++i) {
    var match = matches[i];
    var match_string = subject.slice(match[0], match[1]);
    result[i] = match_string;
  }

  return result;
}, DONT_ENUM);


// ECMA-262 section 15.5.4.12
%AddProperty($String.prototype, "search", function(re) {
  var regexp = new ORIGINAL_REGEXP(re);
  var s = ToString(this);
  var last_idx = regexp.lastIndex; // keep old lastIndex
  regexp.lastIndex = 0;            // ignore re.global property
  var result = regexp.exec(s);
  regexp.lastIndex = last_idx;     // restore lastIndex
  if (result == null)
    return -1;
  else
    return result.index;
}, DONT_ENUM);


// ECMA-262 section 15.5.4.13
%AddProperty($String.prototype, "slice", function(start, end) {
  var s = ToString(this);
  var s_len = s.length;
  var start_i = TO_INTEGER(start);
  var end_i = s_len;
  if (end !== void 0)
    end_i = TO_INTEGER(end);

  if (start_i < 0) {
    start_i += s_len;
    if (start_i < 0)
      start_i = 0;
  } else {
    if (start_i > s_len)
      start_i = s_len;
  }

  if (end_i < 0) {
    end_i += s_len;
    if (end_i < 0)
      end_i = 0;
  } else {
    if (end_i > s_len)
      end_i = s_len;
  }

  var num_c = end_i - start_i;
  if (num_c < 0)
    num_c = 0;

  return SubString(s, start_i, start_i + num_c);
}, DONT_ENUM);


// ECMA-262 section 15.5.4.14
%AddProperty($String.prototype, "split", function(separator, limit) {
  var subject = ToString(this);
  var result = [];
  var lim = (limit === void 0) ? 0xffffffff : ToUint32(limit);
  
  if (lim === 0) return result;
  
  // ECMA-262 says that if separator is undefined, the result should
  // be an array of size 1 containing the entire string.  SpiderMonkey
  // and KJS have this behaviour only when no separator is given.  If
  // undefined is explicitly given, they convert it to a string and
  // use that.  We do as SpiderMonkey and KJS.
  if (%_ArgumentsLength() === 0) {
    result[result.length] = subject;
    return result;
  }
  
  var length = subject.length;
  var currentIndex = 0;
  var startIndex = 0;
  
  var sep = IS_REGEXP(separator) ? separator : ToString(separator);
  
  if (length === 0) {
    if (splitMatch(sep, subject, 0, 0) != null) return result;
    result[result.length] = subject;
    return result;
  }
  
  while (true) {

    if (startIndex === length) {
      result[result.length] = subject.slice(currentIndex, length);
      return result;
    }
    
    var match = splitMatch(sep, subject, currentIndex, startIndex);
    
    if (IS_NULL(match)) {
      result[result.length] = subject.slice(currentIndex, length);
      return result;
    }
    
    var endIndex = match[0];

    // We ignore a zero-length match at the currentIndex.
    if (startIndex === endIndex && endIndex === currentIndex) {
      startIndex++;
      continue;
    }

    result[result.length] = match[1];
    if (result.length === lim) return result;
    
    for (var i = 2; i < match.length; i++) {
      result[result.length] = match[i];
      if (result.length === lim) return result;
    }
    
    startIndex = currentIndex = endIndex;
  }
}, DONT_ENUM);


// ECMA-262 section 15.5.4.14
// Helper function used by split.
function splitMatch(separator, subject, current_index, start_index) {
  if (IS_REGEXP(separator)) {
    var ovector = DoRegExpExec(separator, subject, start_index);
    if (ovector == null) return null;
    var nof_results = ovector.length >> 1;
    var result = new $Array(nof_results + 1);
    result[0] = ovector[1];
    result[1] = subject.slice(current_index, ovector[0]);
    for (var i = 1; i < nof_results; i++) {
      var matching_start = ovector[2*i];
      var matching_end = ovector[2*i + 1];
      if (matching_start != -1 && matching_end != -1) {
        result[i + 1] = subject.slice(matching_start, matching_end);
      }
    }
    return result;
  }
  
  var separatorIndex = subject.indexOf(separator, start_index);
  if (separatorIndex === -1) return null;
  
  return [ separatorIndex + separator.length, subject.slice(current_index, separatorIndex) ];
};


// ECMA-262 section 15.5.4.15
%AddProperty($String.prototype, "substring", function(start, end) {
  var s = ToString(this);
  var s_len = s.length;
  var start_i = TO_INTEGER(start);
  var end_i = s_len;
  if (!IS_UNDEFINED(end))
    end_i = TO_INTEGER(end);

  if (start_i < 0) start_i = 0;
  if (start_i > s_len) start_i = s_len;
  if (end_i < 0) end_i = 0;
  if (end_i > s_len) end_i = s_len;

  if (start_i > end_i) {
    var tmp = end_i;
    end_i = start_i;
    start_i = tmp;
  }

  return SubString(s, start_i, end_i);
}, DONT_ENUM);


// This is not a part of ECMA-262.
%AddProperty($String.prototype, "substr", function(start, n) {
  var s = ToString(this);
  var len;

  // Correct n: If not given, set to string length; if explicitly
  // set to undefined, zero, or negative, returns empty string.
  if (n === void 0) {
    len = s.length;
  } else {
    len = TO_INTEGER(n);
    if (len <= 0) return '';
  }

  // Correct start: If not given (or undefined), set to zero; otherwise
  // convert to integer and handle negative case.
  if (start === void 0) {
    start = 0;
  } else {
    start = TO_INTEGER(start);
    // If positive, and greater than or equal to the string length,
    // return empty string.
    if (start >= s.length) return '';
    // If negative and absolute value is larger than the string length,
    // use zero.
    if (start < 0) {
      start += s.length;
      if (start < 0) start = 0;
    }
  }

  var end = start + len;
  if (end > s.length) end = s.length;

  return SubString(s, start, end);
}, DONT_ENUM);


// ECMA-262, 15.5.4.16
%AddProperty($String.prototype, "toLowerCase", function() {
  return %StringToLowerCase(ToString(this));
}, DONT_ENUM);


// ECMA-262, 15.5.4.17
%AddProperty($String.prototype, "toLocaleLowerCase", $String.prototype.toLowerCase, DONT_ENUM);


// ECMA-262, 15.5.4.18
%AddProperty($String.prototype, "toUpperCase", function() {
  return %StringToUpperCase(ToString(this));
}, DONT_ENUM);


// ECMA-262, 15.5.4.19
%AddProperty($String.prototype, "toLocaleUpperCase", $String.prototype.toUpperCase, DONT_ENUM);


// ECMA-262, section 15.5.3.2
%AddProperty($String, "fromCharCode", function(code) {
  var n = %_ArgumentsLength();
  if (n == 1) return %CharFromCode(ToNumber(code) & 0xffff)

  // NOTE: This is not super-efficient, but it is necessary because we
  // want to avoid converting to numbers from within the virtual
  // machine. Maybe we can find another way of doing this?
  var codes = new $Array(n);
  for (var i = 0; i < n; i++) codes[i] = ToNumber(%_Arguments(i));
  return %StringFromCharCodeArray(codes);
}, DONT_ENUM);


// ECMA-262, section 15.5.4.4
function CharAt(pos) {
  var subject = ToString(this);
  var index = TO_INTEGER(pos);
  if (index >= subject.length || index < 0) return "";
  return %CharFromCode(%StringCharCodeAt(subject, index));
};

%AddProperty($String.prototype, "charAt", CharAt, DONT_ENUM);


// Helper function for very basic XSS protection.
function HtmlEscape(str) {
  return ToString(str).replace(/</g, "&lt;")
                      .replace(/>/g, "&gt;")
                      .replace(/"/g, "&quot;")
                      .replace(/'/g, "&#039;");
};


// Compatibility support for KJS.
// Tested by mozilla/js/tests/js1_5/Regress/regress-276103.js.
%AddProperty($String.prototype, "link", function(link) {
  return "<a href=\"" + HtmlEscape(link) + "\">" + this + "</a>";
}, DONT_ENUM);


%AddProperty($String.prototype, "anchor", function(name) {
  return "<a name=\"" + HtmlEscape(name) + "\">" + this + "</a>";
}, DONT_ENUM);


%AddProperty($String.prototype, "fontcolor", function(color) {
  return "<font color=\"" + HtmlEscape(color) + "\">" + this + "</font>";
}, DONT_ENUM);


%AddProperty($String.prototype, "fontsize", function(size) {
  return "<font size=\"" + HtmlEscape(size) + "\">" + this + "</font>";
}, DONT_ENUM);


%AddProperty($String.prototype, "big", function() {
  return "<big>" + this + "</big>";
}, DONT_ENUM);


%AddProperty($String.prototype, "blink", function() {
  return "<blink>" + this + "</blink>";
}, DONT_ENUM);


%AddProperty($String.prototype, "bold", function() {
  return "<b>" + this + "</b>";
}, DONT_ENUM);


%AddProperty($String.prototype, "fixed", function() {
  return "<tt>" + this + "</tt>";
}, DONT_ENUM);


%AddProperty($String.prototype, "italics", function() {
  return "<i>" + this + "</i>";
}, DONT_ENUM);


%AddProperty($String.prototype, "small", function() {
  return "<small>" + this + "</small>";
}, DONT_ENUM);


%AddProperty($String.prototype, "strike", function() {
  return "<strike>" + this + "</strike>";
}, DONT_ENUM);


%AddProperty($String.prototype, "sub", function() {
  return "<sub>" + this + "</sub>";
}, DONT_ENUM);


%AddProperty($String.prototype, "sup", function() {
  return "<sup>" + this + "</sup>";
}, DONT_ENUM);


// StringBuilder support.

function StringBuilder() {
  this.elements = new $Array();
}


function ReplaceResultBuilder(str) {
  this.elements = new $Array();
  this.special_string = str;
}


ReplaceResultBuilder.prototype.add =
StringBuilder.prototype.add = function(str) {
  if (!IS_STRING(str)) str = ToString(str);
  if (str.length > 0) {
    var elements = this.elements;
    elements[elements.length] = str;
  }
}


ReplaceResultBuilder.prototype.addSpecialSlice = function(start, end) {
  var len = end - start;
  if (len == 0) return;
  var elements = this.elements;
  if (start >= 0 && len >= 0 && start < 0x80000 && len < 0x800) {
    elements[elements.length] = (start << 11) + len;
  } else {
    elements[elements.length] = SubString(this.special_string, start, end);
  }
}


StringBuilder.prototype.generate = function() {
  return %StringBuilderConcat(this.elements, "");
}


ReplaceResultBuilder.prototype.generate = function() {
  return %StringBuilderConcat(this.elements, this.special_string);
}
