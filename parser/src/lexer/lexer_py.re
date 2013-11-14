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
maybe_exponent = ([eE] [\-+]? digit+)?;
number = ("0x" hex_digit+) | (("." digit+ maybe_exponent) | (digit+ ("." digit*)? maybe_exponent));

<default>
"|="          { PUSH_TOKEN(Token::ASSIGN_BIT_OR); }
"^="          { PUSH_TOKEN(Token::ASSIGN_BIT_XOR); }
"&="          { PUSH_TOKEN(Token::ASSIGN_BIT_AND); }
"+="          { PUSH_TOKEN(Token::ASSIGN_ADD); }
"-="          { PUSH_TOKEN(Token::ASSIGN_SUB); }
"*="          { PUSH_TOKEN(Token::ASSIGN_MUL); }
"/="          { PUSH_TOKEN(Token::ASSIGN_DIV); }
"%="          { PUSH_TOKEN(Token::ASSIGN_MOD); }

"==="         { PUSH_TOKEN(Token::EQ_STRICT); }
"=="          { PUSH_TOKEN(Token::EQ); }
"="           { PUSH_TOKEN(Token::ASSIGN); }
"!=="         { PUSH_TOKEN(Token::NE_STRICT); }
"!="          { PUSH_TOKEN(Token::NE); }
"!"           { PUSH_TOKEN(Token::NOT); }

"//"          <<SingleLineComment>>
"/*"          <<MultiLineComment>>
"<!--"        <<HtmlComment>>

#whitespace* "-->" { if (just_seen_line_terminator_) { YYSETCONDITION(kConditionSingleLineComment); goto yyc_SingleLineComment; } else { --cursor_; send(Token::DEC); start_ = cursor_; goto yyc_Normal; } }

">>>="        { PUSH_TOKEN(Token::ASSIGN_SHR); }
">>>"         { PUSH_TOKEN(Token::SHR); }
"<<="         { PUSH_TOKEN(Token::ASSIGN_SHL); }
">>="         { PUSH_TOKEN(Token::ASSIGN_SAR); }
"<="          { PUSH_TOKEN(Token::LTE); }
">="          { PUSH_TOKEN(Token::GTE); }
"<<"          { PUSH_TOKEN(Token::SHL); }
">>"          { PUSH_TOKEN(Token::SAR); }
"<"           { PUSH_TOKEN(Token::LT); }
">"           { PUSH_TOKEN(Token::GT); }

number        { PUSH_TOKEN(Token::NUMBER); }
# number identifier_char   { PUSH_TOKEN(Token::ILLEGAL); }

"("           { PUSH_TOKEN(Token::LPAREN); }
")"           { PUSH_TOKEN(Token::RPAREN); }
"["           { PUSH_TOKEN(Token::LBRACK); }
"]"           { PUSH_TOKEN(Token::RBRACK); }
"{"           { PUSH_TOKEN(Token::LBRACE); }
"}"           { PUSH_TOKEN(Token::RBRACE); }
":"           { PUSH_TOKEN(Token::COLON); }
";"           { PUSH_TOKEN(Token::SEMICOLON); }
"."           { PUSH_TOKEN(Token::PERIOD); }
"?"           { PUSH_TOKEN(Token::CONDITIONAL); }
"++"          { PUSH_TOKEN(Token::INC); }
"--"          { PUSH_TOKEN(Token::DEC); }

"||"          { PUSH_TOKEN(Token::OR); }
"&&"          { PUSH_TOKEN(Token::AND); }

"|"           { PUSH_TOKEN(Token::BIT_OR); }
"^"           { PUSH_TOKEN(Token::BIT_XOR); }
"&"           { PUSH_TOKEN(Token::BIT_AND); }
"+"           { PUSH_TOKEN(Token::ADD); }
"-"           { PUSH_TOKEN(Token::SUB); }
"*"           { PUSH_TOKEN(Token::MUL); }
"/"           { PUSH_TOKEN(Token::DIV); }
"%"           { PUSH_TOKEN(Token::MOD); }
"~"           { PUSH_TOKEN(Token::BIT_NOT); }
","           { PUSH_TOKEN(Token::COMMA); }

line_terminator+  { PUSH_LINE_TERMINATOR(); }
whitespace     { SKIP(); } # TODO implement skip

"\""           <<DoubleQuoteString>>
"'"            <<SingleQuoteString>>

# all keywords
"break"       { PUSH_TOKEN(Token::BREAK); }
"case"        { PUSH_TOKEN(Token::CASE); }
"catch"       { PUSH_TOKEN(Token::CATCH); }
"class"       { PUSH_TOKEN(Token::FUTURE_RESERVED_WORD); }
"const"       { PUSH_TOKEN(Token::CONST); }
"continue"    { PUSH_TOKEN(Token::CONTINUE); }
"debugger"    { PUSH_TOKEN(Token::DEBUGGER); }
"default"     { PUSH_TOKEN(Token::DEFAULT); }
"delete"      { PUSH_TOKEN(Token::DELETE); }
"do"          { PUSH_TOKEN(Token::DO); }
"else"        { PUSH_TOKEN(Token::ELSE); }
"enum"        { PUSH_TOKEN(Token::FUTURE_RESERVED_WORD); }
"export"      { PUSH_TOKEN(Token::FUTURE_RESERVED_WORD); }
"extends"     { PUSH_TOKEN(Token::FUTURE_RESERVED_WORD); }
"false"       { PUSH_TOKEN(Token::FALSE_LITERAL); }
"finally"     { PUSH_TOKEN(Token::FINALLY); }
"for"         { PUSH_TOKEN(Token::FOR); }
"function"    { PUSH_TOKEN(Token::FUNCTION); }
"if"          { PUSH_TOKEN(Token::IF); }
"implements"  { PUSH_TOKEN(Token::FUTURE_STRICT_RESERVED_WORD); }
"import"      { PUSH_TOKEN(Token::FUTURE_RESERVED_WORD); }
"in"          { PUSH_TOKEN(Token::IN); }
"instanceof"  { PUSH_TOKEN(Token::INSTANCEOF); }
"interface"   { PUSH_TOKEN(Token::FUTURE_STRICT_RESERVED_WORD); }
"let"         { PUSH_TOKEN(Token::FUTURE_STRICT_RESERVED_WORD); }
"new"         { PUSH_TOKEN(Token::NEW); }
"null"        { PUSH_TOKEN(Token::NULL_LITERAL); }
"package"     { PUSH_TOKEN(Token::FUTURE_STRICT_RESERVED_WORD); }
"private"     { PUSH_TOKEN(Token::FUTURE_STRICT_RESERVED_WORD); }
"protected"   { PUSH_TOKEN(Token::FUTURE_STRICT_RESERVED_WORD); }
"public"      { PUSH_TOKEN(Token::FUTURE_STRICT_RESERVED_WORD); }
"return"      { PUSH_TOKEN(Token::RETURN); }
"static"      { PUSH_TOKEN(Token::FUTURE_STRICT_RESERVED_WORD); }
"super"       { PUSH_TOKEN(Token::FUTURE_RESERVED_WORD); }
"switch"      { PUSH_TOKEN(Token::SWITCH); }
"this"        { PUSH_TOKEN(Token::THIS); }
"throw"       { PUSH_TOKEN(Token::THROW); }
"true"        { PUSH_TOKEN(Token::TRUE_LITERAL); }
"try"         { PUSH_TOKEN(Token::TRY); }
"typeof"      { PUSH_TOKEN(Token::TYPEOF); }
"var"         { PUSH_TOKEN(Token::VAR); }
"void"        { PUSH_TOKEN(Token::VOID); }
"while"       { PUSH_TOKEN(Token::WHILE); }
"with"        { PUSH_TOKEN(Token::WITH); }
"yield"       { PUSH_TOKEN(Token::YIELD); }

identifier_start { PUSH_TOKEN(Token::IDENTIFIER); } <<Identifier>>
/\\u[0-9a-fA-F]{4}/ {
  if (V8_UNLIKELY(!ValidIdentifierStart())) {
    PUSH_TOKEN(Token::ILLEGAL);
  }
} <<Identifier>>

eof             { PUSH_TOKEN(Token::EOS); return 0; }
default_action  { PUSH_TOKEN(Token::ILLEGAL); }

<DoubleQuoteString>
/\\\n\r?/ <<continue>>
/\\\r\n?/ <<continue>>
/\\./     <<continue>>
/\n|\r/   { PUSH_TOKEN(Token::ILLEGAL); }
"\""      { PUSH_TOKEN(Token::STRING); }
eof       <<terminate_illegal>>
catch_all <<continue>>

<SingleQuoteString>
/\\\n\r?/ <<continue>>
/\\\r\n?/ <<continue>>
/\\./     <<continue>>
/\n|\r/   { PUSH_TOKEN(Token::ILLEGAL); }
"'"       { PUSH_TOKEN(Token::STRING); }
eof       <<terminate_illegal>>
catch_all <<continue>>

<Identifier>
identifier_char    <<continue>>
/\\u[0-9a-fA-F]{4}/ {
  if (V8_UNLIKELY(!ValidIdentifierStart())) {
    PUSH_TOKEN(Token::ILLEGAL);
  }
} <<continue>>
default_action { PUSH_TOKEN(Token::IDENTIFIER); }

<SingleLineComment>
line_terminator  { PUSH_LINE_TERMINATOR(); }
eof              <<terminate>>
catch_all <<continue>>

<MultiLineComment>
"*/"             { SKIP(); BACK(); goto code_start;}
/\*[^\057]/      <<continue>>
# need to force action
line_terminator+ { PUSH_LINE_TERMINATOR(); } <<continue>>
eof              <<terminate>>
catch_all <<continue>>

<HtmlComment>
"-->"            { SKIP(); }
/--./            <<continue>>
/-./             <<continue>>
# need to force action
line_terminator+ { PUSH_LINE_TERMINATOR(); } <<continue>>
eof              <<terminate>>
catch_all <<continue>>
