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

line_terminator = [:line_terminator:];
identifier_start = [$_:letter:];
identifier_char = [:identifier_start::identifier_part_not_letter:];
digit = [0-9];
hex_digit = [0-9a-fA-F];
single_escape_char = ['"\\bfnrtv];
maybe_exponent = /([eE][\-+]?[:digit:]+)?/;
octal_number = /0[0-7]+/;
number =
  /0[xX][:hex_digit:]+/ | (
  /\.[:digit:]+/ maybe_exponent |
  /[:digit:]+(\.[:digit:]*)?/ maybe_exponent );
harmony_number = "0"[bBoO][:digit:]+;
line_terminator_sequence = /[:line_terminator:]|\r\n/;
eos = [:eos:];

# grammar is
#   regex <action_on_state_entry|action_on_match|transition>
#
# actions can be c code enclosed in {} or identifiers to be passed to codegen
# transition must be in continue or the name of a subgraph

<<default>>

"|="          <|token(ASSIGN_BIT_OR)|>
"^="          <|token(ASSIGN_BIT_XOR)|>
"&="          <|token(ASSIGN_BIT_AND)|>
"+="          <|token(ASSIGN_ADD)|>
"-="          <|token(ASSIGN_SUB)|>
"*="          <|token(ASSIGN_MUL)|>
"/="          <|token(ASSIGN_DIV)|>
"%="          <|token(ASSIGN_MOD)|>

"==="         <|token(EQ_STRICT)|>
"=="          <|token(EQ)|>
"="           <|token(ASSIGN)|>
"!=="         <|token(NE_STRICT)|>
"!="          <|token(NE)|>
"!"           <|token(NOT)|>

"//"          <||SingleLineComment>
"/*"          <set_marker(2)||MultiLineComment>
"<!--"        <||SingleLineComment>

"<!-"        <|{
  BACKWARD(2);
  DO_TOKEN(Token::LT);
}|>

"<!"        <|{
  BACKWARD(1);
  DO_TOKEN(Token::LT);
}|>

"-->" <{
  if (!has_line_terminator_before_next_) {
    BACKWARD(1);
    DO_TOKEN(Token::DEC);
  }
}||SingleLineComment>

">>>="        <|token(ASSIGN_SHR)|>
">>>"         <|token(SHR)|>
"<<="         <|token(ASSIGN_SHL)|>
">>="         <|token(ASSIGN_SAR)|>
"<="          <|token(LTE)|>
">="          <|token(GTE)|>
"<<"          <|token(SHL)|>
">>"          <|token(SAR)|>
"<"           <|token(LT)|>
">"           <|token(GT)|>

octal_number            <|octal_number|>
number                  <|token(NUMBER)|>
number identifier_char  <|token(ILLEGAL)|>
number "\\"             <|token(ILLEGAL)|>

harmony_number                  <|harmony_token(numeric_literals, NUMBER, ILLEGAL)|>
harmony_number identifier_char  <|token(ILLEGAL)|>
harmony_number "\\"             <|token(ILLEGAL)|>

"("           <|token(LPAREN)|>
")"           <|token(RPAREN)|>
"["           <|token(LBRACK)|>
"]"           <|token(RBRACK)|>
"{"           <|token(LBRACE)|>
"}"           <|token(RBRACE)|>
":"           <|token(COLON)|>
";"           <|token(SEMICOLON)|>
"."           <|token(PERIOD)|>
"?"           <|token(CONDITIONAL)|>
"++"          <|token(INC)|>
"--"          <|token(DEC)|>

"||"          <|token(OR)|>
"&&"          <|token(AND)|>

"|"           <|token(BIT_OR)|>
"^"           <|token(BIT_XOR)|>
"&"           <|token(BIT_AND)|>
"+"           <|token(ADD)|>
"-"           <|token(SUB)|>
"*"           <|token(MUL)|>
"/"           <|token(DIV)|>
"%"           <|token(MOD)|>
"~"           <|token(BIT_NOT)|>
","           <|token(COMMA)|>

line_terminator+   <|line_terminator|>
/[:whitespace:]+/  <|skip|>

"\""           <set_marker(1)||DoubleQuoteString>
"'"            <set_marker(1)||SingleQuoteString>

# all keywords
"break"       <|token(BREAK)|>
"case"        <|token(CASE)|>
"catch"       <|token(CATCH)|>
"class"       <|token(FUTURE_RESERVED_WORD)|>
"const"       <|token(CONST)|>
"continue"    <|token(CONTINUE)|>
"debugger"    <|token(DEBUGGER)|>
"default"     <|token(DEFAULT)|>
"delete"      <|token(DELETE)|>
"do"          <|token(DO)|>
"else"        <|token(ELSE)|>
"enum"        <|token(FUTURE_RESERVED_WORD)|>
"export"      <|harmony_token(modules, EXPORT, FUTURE_RESERVED_WORD)|>
"extends"     <|token(FUTURE_RESERVED_WORD)|>
"false"       <|token(FALSE_LITERAL)|>
"finally"     <|token(FINALLY)|>
"for"         <|token(FOR)|>
"function"    <|token(FUNCTION)|>
"if"          <|token(IF)|>
"implements"  <|token(FUTURE_STRICT_RESERVED_WORD)|>
"import"      <|harmony_token(modules, IMPORT, FUTURE_RESERVED_WORD)|>
"in"          <|token(IN)|>
"instanceof"  <|token(INSTANCEOF)|>
"interface"   <|token(FUTURE_STRICT_RESERVED_WORD)|>
"let"         <|harmony_token(scoping, LET, FUTURE_STRICT_RESERVED_WORD)|>
"new"         <|token(NEW)|>
"null"        <|token(NULL_LITERAL)|>
"package"     <|token(FUTURE_STRICT_RESERVED_WORD)|>
"private"     <|token(FUTURE_STRICT_RESERVED_WORD)|>
"protected"   <|token(FUTURE_STRICT_RESERVED_WORD)|>
"public"      <|token(FUTURE_STRICT_RESERVED_WORD)|>
"return"      <|token(RETURN)|>
"static"      <|token(FUTURE_STRICT_RESERVED_WORD)|>
"super"       <|token(FUTURE_RESERVED_WORD)|>
"switch"      <|token(SWITCH)|>
"this"        <|token(THIS)|>
"throw"       <|token(THROW)|>
"true"        <|token(TRUE_LITERAL)|>
"try"         <|token(TRY)|>
"typeof"      <|token(TYPEOF)|>
"var"         <|token(VAR)|>
"void"        <|token(VOID)|>
"while"       <|token(WHILE)|>
"with"        <|token(WITH)|>
"yield"       <|token(YIELD)|>

identifier_start <|token(IDENTIFIER)|Identifier>
/\\u[:hex_digit:]{4}/ <{
  if (V8_UNLIKELY(!ValidIdentifierStart())) {
    goto default_action;
  }
  next_.has_escapes = true;
}|token(IDENTIFIER)|Identifier>

eos             <|terminate|>
default_action  <do_token_and_go_forward(ILLEGAL)>

<<DoubleQuoteString>>
"\\" line_terminator_sequence <set_has_escapes||continue>
/\\[x][:hex_digit:]{2}/       <set_has_escapes||continue>
/\\[u][:hex_digit:]{4}/       <set_has_escapes||continue>
/\\[1-7]/                     <octal_inside_string||continue>
/\\[0-7][0-7]+/               <octal_inside_string||continue>
/\\[^xu1-7:line_terminator:]/ <set_has_escapes||continue>
"\\"                          <|token(ILLEGAL)|>
line_terminator               <|token(ILLEGAL)|>
"\""                          <|token(STRING)|>
eos                           <|terminate_illegal|>
catch_all                     <||continue>

<<SingleQuoteString>>
# TODO subgraph for '\'
"\\" line_terminator_sequence <set_has_escapes||continue>
/\\[x][:hex_digit:]{2}/       <set_has_escapes||continue>
/\\[u][:hex_digit:]{4}/       <set_has_escapes||continue>
/\\[1-7]/                     <octal_inside_string||continue>
/\\[0-7][0-7]+/               <octal_inside_string||continue>
/\\[^xu1-7:line_terminator:]/ <set_has_escapes||continue>
"\\"                          <|token(ILLEGAL)|>
line_terminator               <|token(ILLEGAL)|>
"'"                           <|token(STRING)|>
eos                           <|terminate_illegal|>
catch_all                     <||continue>

<<Identifier>>
identifier_char <|token(IDENTIFIER)|continue>
/\\u[:hex_digit:]{4}/ <{
  if (V8_UNLIKELY(!ValidIdentifierPart())) {
    goto default_action;
  }
  next_.has_escapes = true;
}|token(IDENTIFIER)|continue>

<<SingleLineComment>>
line_terminator  <|line_terminator|>
eos              <|skip_and_terminate|>
catch_all        <||continue>

<<MultiLineComment>>
/\*+\//          <|skip|>
# TODO find a way to generate the below rule
/\*+[^\/*]/      <||continue>
line_terminator  <line_terminator||continue>
eos              <|terminate_illegal|>
catch_all        <||continue>
