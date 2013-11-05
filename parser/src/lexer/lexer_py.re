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

whitespace_char = [ \t\v\f\r:ws:]; # TODO put back \240
whitespace = whitespace_char+;
identifier_start = [$_a-zA-Z:lit:];
identifier_char = [$_a-zA-Z0-9:lit:];
not_identifier_char = [^:identifier_char:];
line_terminator = [\n\r]+;
digit = [0-9];
hex_digit = [0-9a-fA-F];
maybe_exponent = ("e" [\-+]? digit+)?;
number = ("0x" hex_digit+) | (("." digit+ maybe_exponent) | (digit+ ("." digit*)? maybe_exponent));

<Normal> "break" not_identifier_char      { PUSH_TOKEN_LOOKAHEAD(Token::BREAK); }
<Normal> "case" not_identifier_char       { PUSH_TOKEN_LOOKAHEAD(Token::CASE); }
<Normal> "catch" not_identifier_char      { PUSH_TOKEN_LOOKAHEAD(Token::CATCH); }
<Normal> "class" not_identifier_char      { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_RESERVED_WORD); }
<Normal> "const" not_identifier_char      { PUSH_TOKEN_LOOKAHEAD(Token::CONST); }
<Normal> "continue" not_identifier_char   { PUSH_TOKEN_LOOKAHEAD(Token::CONTINUE); }
<Normal> "debugger" not_identifier_char   { PUSH_TOKEN_LOOKAHEAD(Token::DEBUGGER); }
<Normal> "default" not_identifier_char    { PUSH_TOKEN_LOOKAHEAD(Token::DEFAULT); }
<Normal> "delete" not_identifier_char     { PUSH_TOKEN_LOOKAHEAD(Token::DELETE); }
<Normal> "do" not_identifier_char         { PUSH_TOKEN_LOOKAHEAD(Token::DO); }
<Normal> "else" not_identifier_char       { PUSH_TOKEN_LOOKAHEAD(Token::ELSE); }
<Normal> "enum" not_identifier_char       { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_RESERVED_WORD); }
<Normal> "export" not_identifier_char     { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_RESERVED_WORD); }
<Normal> "extends" not_identifier_char    { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_RESERVED_WORD); }
<Normal> "false" not_identifier_char      { PUSH_TOKEN_LOOKAHEAD(Token::FALSE_LITERAL); }
<Normal> "finally" not_identifier_char    { PUSH_TOKEN_LOOKAHEAD(Token::FINALLY); }
<Normal> "for" not_identifier_char        { PUSH_TOKEN_LOOKAHEAD(Token::FOR); }
<Normal> "function" not_identifier_char   { PUSH_TOKEN_LOOKAHEAD(Token::FUNCTION); }
<Normal> "if" not_identifier_char         { PUSH_TOKEN_LOOKAHEAD(Token::IF); }
<Normal> "implements" not_identifier_char { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_STRICT_RESERVED_WORD); }
<Normal> "import" not_identifier_char     { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_RESERVED_WORD); }
<Normal> "in" not_identifier_char         { PUSH_TOKEN_LOOKAHEAD(Token::IN); }
<Normal> "instanceof" not_identifier_char { PUSH_TOKEN_LOOKAHEAD(Token::INSTANCEOF); }
<Normal> "interface" not_identifier_char  { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_STRICT_RESERVED_WORD); }
<Normal> "let" not_identifier_char        { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_STRICT_RESERVED_WORD); }
<Normal> "new" not_identifier_char        { PUSH_TOKEN_LOOKAHEAD(Token::NEW); }
<Normal> "null" not_identifier_char       { PUSH_TOKEN_LOOKAHEAD(Token::NULL_LITERAL); }
<Normal> "package" not_identifier_char    { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_STRICT_RESERVED_WORD); }
<Normal> "private" not_identifier_char    { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_STRICT_RESERVED_WORD); }
<Normal> "protected" not_identifier_char  { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_STRICT_RESERVED_WORD); }
<Normal> "public" not_identifier_char     { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_STRICT_RESERVED_WORD); }
<Normal> "return" not_identifier_char     { PUSH_TOKEN_LOOKAHEAD(Token::RETURN); }
<Normal> "static" not_identifier_char     { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_STRICT_RESERVED_WORD); }
<Normal> "super" not_identifier_char      { PUSH_TOKEN_LOOKAHEAD(Token::FUTURE_RESERVED_WORD); }
<Normal> "switch" not_identifier_char     { PUSH_TOKEN_LOOKAHEAD(Token::SWITCH); }
<Normal> "this" not_identifier_char       { PUSH_TOKEN_LOOKAHEAD(Token::THIS); }
<Normal> "throw" not_identifier_char      { PUSH_TOKEN_LOOKAHEAD(Token::THROW); }
<Normal> "true" not_identifier_char       { PUSH_TOKEN_LOOKAHEAD(Token::TRUE_LITERAL); }
<Normal> "try" not_identifier_char        { PUSH_TOKEN_LOOKAHEAD(Token::TRY); }
<Normal> "typeof" not_identifier_char     { PUSH_TOKEN_LOOKAHEAD(Token::TYPEOF); }
<Normal> "var" not_identifier_char        { PUSH_TOKEN_LOOKAHEAD(Token::VAR); }
<Normal> "void" not_identifier_char       { PUSH_TOKEN_LOOKAHEAD(Token::VOID); }
<Normal> "while" not_identifier_char      { PUSH_TOKEN_LOOKAHEAD(Token::WHILE); }
<Normal> "with" not_identifier_char       { PUSH_TOKEN_LOOKAHEAD(Token::WITH); }
<Normal> "yield" not_identifier_char      { PUSH_TOKEN_LOOKAHEAD(Token::YIELD); }

<Normal> "|="          { PUSH_TOKEN(Token::ASSIGN_BIT_OR); }
<Normal> "^="          { PUSH_TOKEN(Token::ASSIGN_BIT_XOR); }
<Normal> "&="          { PUSH_TOKEN(Token::ASSIGN_BIT_AND); }
<Normal> "+="          { PUSH_TOKEN(Token::ASSIGN_ADD); }
<Normal> "-="          { PUSH_TOKEN(Token::ASSIGN_SUB); }
<Normal> "*="          { PUSH_TOKEN(Token::ASSIGN_MUL); }
<Normal> "/="          { PUSH_TOKEN(Token::ASSIGN_DIV); }
<Normal> "%="          { PUSH_TOKEN(Token::ASSIGN_MOD); }

<Normal> "==="         { PUSH_TOKEN(Token::EQ_STRICT); }
<Normal> "=="          { PUSH_TOKEN(Token::EQ); }
<Normal> "="           { PUSH_TOKEN(Token::ASSIGN); }
<Normal> "!=="         { PUSH_TOKEN(Token::NE_STRICT); }
<Normal> "!="          { PUSH_TOKEN(Token::NE); }
<Normal> "!"           { PUSH_TOKEN(Token::NOT); }

<Normal> "//"          :=> SingleLineComment
<Normal> whitespace* "-->" { if (just_seen_line_terminator_) { YYSETCONDITION(kConditionSingleLineComment); goto yyc_SingleLineComment; } else { --cursor_; send(Token::DEC); start_ = cursor_; goto yyc_Normal; } }
<Normal> "/*"          :=> MultiLineComment
<Normal> "<!--"        :=> HtmlComment

<Normal> ">>>="        { PUSH_TOKEN(Token::ASSIGN_SHR); }
<Normal> ">>>"         { PUSH_TOKEN(Token::SHR); }
<Normal> "<<="         { PUSH_TOKEN(Token::ASSIGN_SHL); }
<Normal> ">>="         { PUSH_TOKEN(Token::ASSIGN_SAR); }
<Normal> "<="          { PUSH_TOKEN(Token::LTE); }
<Normal> ">="          { PUSH_TOKEN(Token::GTE); }
<Normal> "<<"          { PUSH_TOKEN(Token::SHL); }
<Normal> ">>"          { PUSH_TOKEN(Token::SAR); }
<Normal> "<"           { PUSH_TOKEN(Token::LT); }
<Normal> ">"           { PUSH_TOKEN(Token::GT); }

<Normal> number not_identifier_char { PUSH_TOKEN_LOOKAHEAD(Token::NUMBER); }
<Normal> number identifier_char   { PUSH_TOKEN_LOOKAHEAD(Token::ILLEGAL); }

<Normal> "("           { PUSH_TOKEN(Token::LPAREN); }
<Normal> ")"           { PUSH_TOKEN(Token::RPAREN); }
<Normal> "["           { PUSH_TOKEN(Token::LBRACK); }
<Normal> "]"           { PUSH_TOKEN(Token::RBRACK); }
<Normal> "{"           { PUSH_TOKEN(Token::LBRACE); }
<Normal> "}"           { PUSH_TOKEN(Token::RBRACE); }
<Normal> ":"           { PUSH_TOKEN(Token::COLON); }
<Normal> ";"           { PUSH_TOKEN(Token::SEMICOLON); }
<Normal> "."           { PUSH_TOKEN(Token::PERIOD); }
<Normal> "?"           { PUSH_TOKEN(Token::CONDITIONAL); }
<Normal> "++"          { PUSH_TOKEN(Token::INC); }
<Normal> "--"          { PUSH_TOKEN(Token::DEC); }

<Normal> "||"          { PUSH_TOKEN(Token::OR); }
<Normal> "&&"          { PUSH_TOKEN(Token::AND); }

<Normal> "|"           { PUSH_TOKEN(Token::BIT_OR); }
<Normal> "^"           { PUSH_TOKEN(Token::BIT_XOR); }
<Normal> "&"           { PUSH_TOKEN(Token::BIT_AND); }
<Normal> "+"           { PUSH_TOKEN(Token::ADD); }
<Normal> "-"           { PUSH_TOKEN(Token::SUB); }
<Normal> "*"           { PUSH_TOKEN(Token::MUL); }
<Normal> "/"           { PUSH_TOKEN(Token::DIV); }
<Normal> "%"           { PUSH_TOKEN(Token::MOD); }
<Normal> "~"           { PUSH_TOKEN(Token::BIT_NOT); }
<Normal> ","           { PUSH_TOKEN(Token::COMMA); }

<Normal> line_terminator  { PUSH_LINE_TERMINATOR(); }
<Normal> whitespace       { SKIP(); }

<Normal> "\""           :=> DoubleQuoteString
<Normal> "'"           :=> SingleQuoteString

<Normal> identifier_start     :=> Identifier
<Normal> /\\u[0-9a-fA-F]{4}/ { if (ValidIdentifierStart()) { YYSETCONDITION(kConditionIdentifier); goto yyc_Identifier; } send(Token::ILLEGAL); start_ = cursor_; goto yyc_Normal; }
<Normal> "\\"                 { PUSH_TOKEN(Token::ILLEGAL); }

<Normal> eof           { PUSH_EOF_AND_RETURN();}
<Normal> any           { PUSH_TOKEN(Token::ILLEGAL); }

<DoubleQuoteString> "\\\\"  { goto yyc_DoubleQuoteString; }
<DoubleQuoteString> "\\\""  { goto yyc_DoubleQuoteString; }
<DoubleQuoteString> "\""     { PUSH_TOKEN(Token::STRING);}
<DoubleQuoteString> /\\\n\r?/ { goto yyc_DoubleQuoteString; }
<DoubleQuoteString> /\\\r\n?/ { goto yyc_DoubleQuoteString; }
<DoubleQuoteString> "\n"    => Normal { PUSH_TOKEN_LOOKAHEAD(Token::ILLEGAL); }
<DoubleQuoteString> "\r"    => Normal { PUSH_TOKEN_LOOKAHEAD(Token::ILLEGAL); }
<DoubleQuoteString> eof     { TERMINATE_ILLEGAL(); }
<DoubleQuoteString> any     { goto yyc_DoubleQuoteString; }

<SingleQuoteString> "\\"  { goto yyc_SingleQuoteString; }
<SingleQuoteString> "\\'"   { goto yyc_SingleQuoteString; }
<SingleQuoteString> "'"     { PUSH_TOKEN(Token::STRING); }
<SingleQuoteString> /\\\n\r?/ { goto yyc_SingleQuoteString; }
<SingleQuoteString> /\\\r\n?/ { goto yyc_SingleQuoteString; }
<SingleQuoteString> "\n"    => Normal { PUSH_TOKEN_LOOKAHEAD(Token::ILLEGAL); }
<SingleQuoteString> "\r"    => Normal { PUSH_TOKEN_LOOKAHEAD(Token::ILLEGAL); }
<SingleQuoteString> eof     { TERMINATE_ILLEGAL(); }
<SingleQuoteString> any     { goto yyc_SingleQuoteString; }

<Identifier> identifier_char+  { goto yyc_Identifier; }
<Identifier> /\\u[0-9a-fA-F]{4}/ { if (ValidIdentifierPart()) { goto yyc_Identifier; } YYSETCONDITION(kConditionNormal); send(Token::ILLEGAL); start_ = cursor_; goto yyc_Normal; }
<Identifier> "\\"              { PUSH_TOKEN(Token::ILLEGAL); }
<Identifier> any               { PUSH_TOKEN_LOOKAHEAD(Token::IDENTIFIER); }

<SingleLineComment> line_terminator { PUSH_LINE_TERMINATOR();}
<SingleLineComment> eof             { start_ = cursor_ - 1; PUSH_TOKEN(Token::EOS); }
<SingleLineComment> any             { goto yyc_SingleLineComment; }

<MultiLineComment> "*/"  { PUSH_LINE_TERMINATOR();}
<MultiLineComment> eof      { start_ = cursor_ - 1; PUSH_TOKEN(Token::EOS); }
<MultiLineComment> any      { goto yyc_MultiLineComment; }

<HtmlComment> "-->"      { PUSH_LINE_TERMINATOR();}
<HtmlComment> line_terminator+ { PUSH_LINE_TERMINATOR();}
<HtmlComment> eof        { start_ = cursor_ - 1; PUSH_TOKEN(Token::EOS); }
<HtmlComment> any        { goto yyc_HtmlComment; }
