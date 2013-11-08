# Copyright 2013 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

whitespace_char = [ \t\v\f\r:ws:\240];
whitespace = whitespace_char+;
identifier_start = [$_a-zA-Z:lit:];   # TODO add relevant latin1 char codes
identifier_char = [0-9:identifier_start:];
line_terminator = [\n\r];
digit = [0-9];
hex_digit = [0-9a-fA-F];
maybe_exponent = ("e" [\-+]? digit+)?;
number = ("0x" hex_digit+) | (("." digit+ maybe_exponent) | (digit+ ("." digit*)? maybe_exponent));

<default>
"|="          { PUSH_TOKEN(ASSIGN_BIT_OR); }
"^="          { PUSH_TOKEN(ASSIGN_BIT_XOR); }
"&="          { PUSH_TOKEN(ASSIGN_BIT_AND); }
"+="          { PUSH_TOKEN(ASSIGN_ADD); }
"-="          { PUSH_TOKEN(ASSIGN_SUB); }
"*="          { PUSH_TOKEN(ASSIGN_MUL); }
"/="          { PUSH_TOKEN(ASSIGN_DIV); }
"%="          { PUSH_TOKEN(ASSIGN_MOD); }

"==="         { PUSH_TOKEN(EQ_STRICT); }
"=="          { PUSH_TOKEN(EQ); }
"="           { PUSH_TOKEN(ASSIGN); }
"!=="         { PUSH_TOKEN(NE_STRICT); }
"!="          { PUSH_TOKEN(NE); }
"!"           { PUSH_TOKEN(NOT); }

"//"          <<SingleLineComment>>   # TODO save offset?
"/*"          <<MultiLineComment>>
"<!--"        <<HtmlComment>>
#whitespace* "-->" { if (just_seen_line_terminator_) { YYSETCONDITION(kConditionSingleLineComment); goto yyc_SingleLineComment; } else { --cursor_; send(Token::DEC); start_ = cursor_; goto yyc_Normal; } }

">>>="        { PUSH_TOKEN(ASSIGN_SHR); }
">>>"         { PUSH_TOKEN(SHR); }
"<<="         { PUSH_TOKEN(ASSIGN_SHL); }
">>="         { PUSH_TOKEN(ASSIGN_SAR); }
"<="          { PUSH_TOKEN(LTE); }
">="          { PUSH_TOKEN(GTE); }
"<<"          { PUSH_TOKEN(SHL); }
">>"          { PUSH_TOKEN(SAR); }
"<"           { PUSH_TOKEN(LT); }
">"           { PUSH_TOKEN(GT); }

number        { PUSH_TOKEN(NUMBER); }
# number identifier_char   { PUSH_TOKEN(ILLEGAL); }

"("           { PUSH_TOKEN(LPAREN); }
")"           { PUSH_TOKEN(RPAREN); }
"["           { PUSH_TOKEN(LBRACK); }
"]"           { PUSH_TOKEN(RBRACK); }
"{"           { PUSH_TOKEN(LBRACE); }
"}"           { PUSH_TOKEN(RBRACE); }
":"           { PUSH_TOKEN(COLON); }
";"           { PUSH_TOKEN(SEMICOLON); }
"."           { PUSH_TOKEN(PERIOD); }
"?"           { PUSH_TOKEN(CONDITIONAL); }
"++"          { PUSH_TOKEN(INC); }
"--"          { PUSH_TOKEN(DEC); }

"||"          { PUSH_TOKEN(OR); }
"&&"          { PUSH_TOKEN(AND); }

"|"           { PUSH_TOKEN(BIT_OR); }
"^"           { PUSH_TOKEN(BIT_XOR); }
"&"           { PUSH_TOKEN(BIT_AND); }
"+"           { PUSH_TOKEN(ADD); }
"-"           { PUSH_TOKEN(SUB); }
"*"           { PUSH_TOKEN(MUL); }
"/"           { PUSH_TOKEN(DIV); }
"%"           { PUSH_TOKEN(MOD); }
"~"           { PUSH_TOKEN(BIT_NOT); }
","           { PUSH_TOKEN(COMMA); }

line_terminator+  { PUSH_LINE_TERMINATOR(); }
whitespace     <<continue>>

"\""           <<DoubleQuoteString>>
"'"            <<SingleQuoteString>>

# all keywords
"break"       { PUSH_TOKEN(BREAK); } <<break>>
"case"        { PUSH_TOKEN(CASE); } <<break>>
"catch"       { PUSH_TOKEN(CATCH); } <<break>>
"class"       { PUSH_TOKEN(FUTURE_RESERVED_WORD); } <<break>>
"const"       { PUSH_TOKEN(CONST); } <<break>>
"continue"    { PUSH_TOKEN(CONTINUE); } <<break>>
"debugger"    { PUSH_TOKEN(DEBUGGER); } <<break>>
"default"     { PUSH_TOKEN(DEFAULT); } <<break>>
"delete"      { PUSH_TOKEN(DELETE); } <<break>>
"do"          { PUSH_TOKEN(DO); } <<break>>
"else"        { PUSH_TOKEN(ELSE); } <<break>>
"enum"        { PUSH_TOKEN(FUTURE_RESERVED_WORD); } <<break>>
"export"      { PUSH_TOKEN(FUTURE_RESERVED_WORD); } <<break>>
"extends"     { PUSH_TOKEN(FUTURE_RESERVED_WORD); } <<break>>
"false"       { PUSH_TOKEN(FALSE_LITERAL); } <<break>>
"finally"     { PUSH_TOKEN(FINALLY); } <<break>>
"for"         { PUSH_TOKEN(FOR); } <<break>>
"function"    { PUSH_TOKEN(FUNCTION); } <<break>>
"if"          { PUSH_TOKEN(IF); } <<break>>
"implements"  { PUSH_TOKEN(FUTURE_STRICT_RESERVED_WORD); } <<break>>
"import"      { PUSH_TOKEN(FUTURE_RESERVED_WORD); } <<break>>
"in"          { PUSH_TOKEN(IN); } <<break>>
"instanceof"  { PUSH_TOKEN(INSTANCEOF); } <<break>>
"interface"   { PUSH_TOKEN(FUTURE_STRICT_RESERVED_WORD); } <<break>>
"let"         { PUSH_TOKEN(FUTURE_STRICT_RESERVED_WORD); } <<break>>
"new"         { PUSH_TOKEN(NEW); } <<break>>
"null"        { PUSH_TOKEN(NULL_LITERAL); } <<break>>
"package"     { PUSH_TOKEN(FUTURE_STRICT_RESERVED_WORD); } <<break>>
"private"     { PUSH_TOKEN(FUTURE_STRICT_RESERVED_WORD); } <<break>>
"protected"   { PUSH_TOKEN(FUTURE_STRICT_RESERVED_WORD); } <<break>>
"public"      { PUSH_TOKEN(FUTURE_STRICT_RESERVED_WORD); } <<break>>
"return"      { PUSH_TOKEN(RETURN); } <<break>>
"static"      { PUSH_TOKEN(FUTURE_STRICT_RESERVED_WORD); } <<break>>
"super"       { PUSH_TOKEN(FUTURE_RESERVED_WORD); } <<break>>
"switch"      { PUSH_TOKEN(SWITCH); } <<break>>
"this"        { PUSH_TOKEN(THIS); } <<break>>
"throw"       { PUSH_TOKEN(THROW); } <<break>>
"true"        { PUSH_TOKEN(TRUE_LITERAL); } <<break>>
"try"         { PUSH_TOKEN(TRY); } <<break>>
"typeof"      { PUSH_TOKEN(TYPEOF); } <<break>>
"var"         { PUSH_TOKEN(VAR); } <<break>>
"void"        { PUSH_TOKEN(VOID); } <<break>>
"while"       { PUSH_TOKEN(WHILE); } <<break>>
"with"        { PUSH_TOKEN(WITH); } <<break>>
"yield"       { PUSH_TOKEN(YIELD); } <<break>>

identifier_start     <<Identifier>>
/\\u[0-9a-fA-F]{4}/ {
  if (V8_LIKELY(ValidIdentifierStart())) {
    JUMP(Identifier);
  }
  PUSH_TOKEN(ILLEGAL);
} <<Identifier>>

eof           <<terminate>>
default       { PUSH_TOKEN(ILLEGAL); }

<DoubleQuoteString>
"\\"      <<continue>>
"\\\""    <<continue>>
"\""      { PUSH_TOKEN(STRING); } <<break>>
/\\\n\r?/ <<continue>>
/\\\r\n?/ <<continue>>
/\n|\r/    { PUSH_TOKEN(ILLEGAL); } <<break>>
eof       <<terminate_illegal>>
default   <<continue>>

<SingleQuoteString>
"\\"      <<continue>>
"\\'"     <<continue>>
"'"       { PUSH_TOKEN(STRING); } <<break>>
/\\\n\r?/ <<continue>>
/\\\r\n?/ <<continue>>
/\n|\r/    { PUSH_TOKEN(ILLEGAL); } <<break>>
eof       <<terminate_illegal>>
default   <<continue>>

<Identifier>
identifier_char+    <<continue>>
/\\u[0-9a-fA-F]{4}/ {
  if (V8_UNLIKELY(!ValidIdentifierStart())) {
    PUSH_TOKEN(ILLEGAL);
    JUMP(Normal);
  }
}
default             { PUSH_TOKEN(IDENTIFIER); }  <<break>>

<SingleLineComment>
line_terminator  { PUSH_LINE_TERMINATOR(); } <<break>>
eof              <<terminate>>
default          <<continue>>

<MultiLineComment>
"*/"             <<break>>
line_terminator+ { PUSH_LINE_TERMINATOR(); }
eof              <<terminate>>
default          <<continue>>

<HtmlComment>
"-->"            <<break>>
line_terminator+ { PUSH_LINE_TERMINATOR(); }
eof              <<terminate>>
default          <<continue>>
