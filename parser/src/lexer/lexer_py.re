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
identifier_start = [$_a-zA-Z:lit:];
identifier_char = [0-9:identifier_start:];
line_terminator = [\n\r];
digit = [0-9];
hex_digit = [0-9a-fA-F];
maybe_exponent = /([eE][\-+]?[:digit:]+)?/;
number =
  /0x[:hex_digit:]+/ | (
  /\.[:digit:]+/ maybe_exponent |
  /[:digit:]+(\.[:digit:]*)?/ maybe_exponent );

# grammar is
#   regex <action_on_state_entry|action_on_match|transition>
#
# actions can be c code enclosed in {} or identifiers to be passed to codegen
# transition must be in continue or the name of a subgraph

<<default>>
"|="          <|push_token(ASSIGN_BIT_OR)|>
"^="          <|push_token(ASSIGN_BIT_XOR)|>
"&="          <|push_token(ASSIGN_BIT_AND)|>
"+="          <|push_token(ASSIGN_ADD)|>
"-="          <|push_token(ASSIGN_SUB)|>
"*="          <|push_token(ASSIGN_MUL)|>
"/="          <|push_token(ASSIGN_DIV)|>
"%="          <|push_token(ASSIGN_MOD)|>

"==="         <|push_token(EQ_STRICT)|>
"=="          <|push_token(EQ)|>
"="           <|push_token(ASSIGN)|>
"!=="         <|push_token(NE_STRICT)|>
"!="          <|push_token(NE)|>
"!"           <|push_token(NOT)|>

"//"          <||SingleLineComment>
"/*"          <||MultiLineComment>
"<!--"        <||HtmlComment>

whitespace? "-->" <{
  if (!just_seen_line_terminator_) {
    PUSH_TOKEN(Token::DEC);
    start_ = cursor_ - 1;
    goto code_start;
  }
}||SingleLineComment>

">>>="        <|push_token(ASSIGN_SHR)|>
">>>"         <|push_token(SHR)|>
"<<="         <|push_token(ASSIGN_SHL)|>
">>="         <|push_token(ASSIGN_SAR)|>
"<="          <|push_token(LTE)|>
">="          <|push_token(GTE)|>
"<<"          <|push_token(SHL)|>
">>"          <|push_token(SAR)|>
"<"           <|push_token(LT)|>
">"           <|push_token(GT)|>

number        <|push_token(NUMBER)|>
# is this necessary?
number identifier_char   <|push_token(ILLEGAL)|>

"("           <|push_token(LPAREN)|>
")"           <|push_token(RPAREN)|>
"["           <|push_token(LBRACK)|>
"]"           <|push_token(RBRACK)|>
"{"           <|push_token(LBRACE)|>
"}"           <|push_token(RBRACE)|>
":"           <|push_token(COLON)|>
";"           <|push_token(SEMICOLON)|>
"."           <|push_token(PERIOD)|>
"?"           <|push_token(CONDITIONAL)|>
"++"          <|push_token(INC)|>
"--"          <|push_token(DEC)|>

"||"          <|push_token(OR)|>
"&&"          <|push_token(AND)|>

"|"           <|push_token(BIT_OR)|>
"^"           <|push_token(BIT_XOR)|>
"&"           <|push_token(BIT_AND)|>
"+"           <|push_token(ADD)|>
"-"           <|push_token(SUB)|>
"*"           <|push_token(MUL)|>
"/"           <|push_token(DIV)|>
"%"           <|push_token(MOD)|>
"~"           <|push_token(BIT_NOT)|>
","           <|push_token(COMMA)|>

line_terminator+  <|push_line_terminator|>
whitespace        <|skip|>

"\""           <||DoubleQuoteString>
"'"            <||SingleQuoteString>

# all keywords
"break"       <|push_token(BREAK)|>
"case"        <|push_token(CASE)|>
"catch"       <|push_token(CATCH)|>
"class"       <|push_token(FUTURE_RESERVED_WORD)|>
"const"       <|push_token(CONST)|>
"continue"    <|push_token(CONTINUE)|>
"debugger"    <|push_token(DEBUGGER)|>
"default"     <|push_token(DEFAULT)|>
"delete"      <|push_token(DELETE)|>
"do"          <|push_token(DO)|>
"else"        <|push_token(ELSE)|>
"enum"        <|push_token(FUTURE_RESERVED_WORD)|>
"export"      <|push_token(FUTURE_RESERVED_WORD)|>
"extends"     <|push_token(FUTURE_RESERVED_WORD)|>
"false"       <|push_token(FALSE_LITERAL)|>
"finally"     <|push_token(FINALLY)|>
"for"         <|push_token(FOR)|>
"function"    <|push_token(FUNCTION)|>
"if"          <|push_token(IF)|>
"implements"  <|push_token(FUTURE_STRICT_RESERVED_WORD)|>
"import"      <|push_token(FUTURE_RESERVED_WORD)|>
"in"          <|push_token(IN)|>
"instanceof"  <|push_token(INSTANCEOF)|>
"interface"   <|push_token(FUTURE_STRICT_RESERVED_WORD)|>
"let"         <|push_token(FUTURE_STRICT_RESERVED_WORD)|>
"new"         <|push_token(NEW)|>
"null"        <|push_token(NULL_LITERAL)|>
"package"     <|push_token(FUTURE_STRICT_RESERVED_WORD)|>
"private"     <|push_token(FUTURE_STRICT_RESERVED_WORD)|>
"protected"   <|push_token(FUTURE_STRICT_RESERVED_WORD)|>
"public"      <|push_token(FUTURE_STRICT_RESERVED_WORD)|>
"return"      <|push_token(RETURN)|>
"static"      <|push_token(FUTURE_STRICT_RESERVED_WORD)|>
"super"       <|push_token(FUTURE_RESERVED_WORD)|>
"switch"      <|push_token(SWITCH)|>
"this"        <|push_token(THIS)|>
"throw"       <|push_token(THROW)|>
"true"        <|push_token(TRUE_LITERAL)|>
"try"         <|push_token(TRY)|>
"typeof"      <|push_token(TYPEOF)|>
"var"         <|push_token(VAR)|>
"void"        <|push_token(VOID)|>
"while"       <|push_token(WHILE)|>
"with"        <|push_token(WITH)|>
"yield"       <|push_token(YIELD)|>

identifier_start <|push_token(IDENTIFIER)|Identifier>
/\\u[0-9a-fA-F]{4}/ <{
  if (V8_UNLIKELY(!ValidIdentifierStart())) {
    goto default_action;
  }
}|push_token(IDENTIFIER)|Identifier>

eof             <|terminate|>
default_action  <push_token(ILLEGAL)>

<<DoubleQuoteString>>
/\\\n\r?/ <||continue>
/\\\r\n?/ <||continue>
/\\./     <||continue>
/\n|\r/   <|push_token(ILLEGAL)|>
"\""      <|push_token(STRING)|>
eof       <|terminate_illegal|>
catch_all <||continue>

<<SingleQuoteString>>
/\\\n\r?/ <||continue>
/\\\r\n?/ <||continue>
/\\./     <||continue>
/\n|\r/   <|push_token(ILLEGAL)|>
"'"       <|push_token(STRING)|>
eof       <|terminate_illegal|>
catch_all <||continue>

<<Identifier>>
identifier_char <|push_token(IDENTIFIER)|continue>
/\\u[0-9a-fA-F]{4}/ <{
  if (V8_UNLIKELY(!ValidIdentifierPart())) {
    goto default_action;
  }
}|push_token(IDENTIFIER)|continue>

<<SingleLineComment>>
line_terminator  <|push_line_terminator|>
catch_all <||continue>

<<MultiLineComment>>
"*/"             <|skip|>
# TODO find a way to generate the below rule
/\*[^\/]/        <||continue>
line_terminator  <push_line_terminator||continue>
catch_all        <||continue>

<<HtmlComment>>
"-->"            <|skip|>
# TODO find a way to generate the below rules
/--./            <||continue>
/-./             <||continue>
line_terminator  <push_line_terminator||continue>
catch_all <||continue>
