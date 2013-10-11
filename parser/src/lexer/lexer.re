/*!re2c


  re2c:define:YYCTYPE  = "uint8_t";
  re2c:define:YYCURSOR = p;
  re2c:yyfill:enable   = 0;
  re2c:yych:conversion = 0;
  re2c:indent:top      = 1;


  eof = [\000];
  any = [\000-\377];
  whitespace_char = [ \h\t\v\f\r];
  whitespace = whitespace_char+;
  identifier_start = [$_\\a-zA-z];
  identifier_char = [$_\\a-zA-z0-9];
  number_start = [0-9];
  line_terminator = [\n\r]+;


  <NORMAL> "("                     { PUSH_T(LPAREN); }
  <NORMAL> ")"                     { PUSH_T(RPAREN); }
  <NORMAL> "["                     { PUSH_T(LBRACK); }
  <NORMAL> "]"                     { PUSH_T(RBRACK); }
  <NORMAL> "{"                     { PUSH_T(LBRACE); }
  <NORMAL> "}"                     { PUSH_T(RBRACE); }
  <NORMAL> ":"                     { PUSH_T(COLON); }
  <NORMAL> ";"                     { PUSH_T(SEMICOLON); }
  <NORMAL> "."                     { PUSH_T(PERIOD); }
  <NORMAL> "?"                     { PUSH_T(CONDITIONAL); }
  <NORMAL> "++"                    { PUSH_T(INC); }
  <NORMAL> "--"                    { PUSH_T(DEC); }


  <NORMAL> "="                     { PUSH_T(ASSIGN); }
  <NORMAL> "|="                    { PUSH_T(ASSIGN_BIT_OR); }
  <NORMAL> "^="                    { PUSH_T(ASSIGN_BIT_XOR); }
  <NORMAL> "&="                    { PUSH_T(ASSIGN_BIT_AND); }
  <NORMAL> "<<="                   { PUSH_T(ASSIGN_SHL); }
  <NORMAL> ">>="                   { PUSH_T(ASSIGN_SAR); }
  <NORMAL> ">>>="                  { PUSH_T(ASSIGN_SHR); }
  <NORMAL> "+="                    { PUSH_T(ASSIGN_ADD); }
  <NORMAL> "-="                    { PUSH_T(ASSIGN_SUB); }
  <NORMAL> "*="                    { PUSH_T(ASSIGN_MUL); }
  <NORMAL> "/="                    { PUSH_T(ASSIGN_DIV); }
  <NORMAL> "%="                    { PUSH_T(ASSIGN_MOD); }


  <NORMAL> ","                     { PUSH_T(COMMA); }
  <NORMAL> "||"                    { PUSH_T(OR); }
  <NORMAL> "&&"                    { PUSH_T(AND); }
  <NORMAL> "|"                     { PUSH_T(BIT_OR); }
  <NORMAL> "^"                     { PUSH_T(BIT_XOR); }
  <NORMAL> "&"                     { PUSH_T(BIT_AND); }
  <NORMAL> "<<"                    { PUSH_T(SHL); }
  <NORMAL> ">>"                    { PUSH_T(SAR); }
  <NORMAL> "+"                     { PUSH_T(ADD); }
  <NORMAL> "-"                     { PUSH_T(SUB); }
  <NORMAL> "*"                     { PUSH_T(MUL); }
  <NORMAL> "/"                     { PUSH_T(DIV); }
  <NORMAL> "%"                     { PUSH_T(MOD); }


  <NORMAL> "=="                    { PUSH_T(EQ); }
  <NORMAL> "!="                    { PUSH_T(NE); }
  <NORMAL> "==="                   { PUSH_T(EQ_STRICT); }
  <NORMAL> "!=="                   { PUSH_T(NE_STRICT); }
  <NORMAL> "<"                     { PUSH_T(LT); }
  <NORMAL> ">"                     { PUSH_T(GT); }
  <NORMAL> "<="                    { PUSH_T(LTE); }
  <NORMAL> ">="                    { PUSH_T(GTE); }


  <NORMAL> "!"                     { PUSH_T(NOT); }
  <NORMAL> "~"                     { PUSH_T(BIT_NOT); }

  <NORMAL> line_terminator+        { PUSH_LINE_TERMINATOR(); }

  <NORMAL> whitespace              {}


  <NORMAL> "//"                    :=> SINGLE_LINE_COMMENT
  <NORMAL> "/*"                    :=> MULTILINE_COMMENT
  <NORMAL> "<!--"                  :=> HTML_COMMENT


  <NORMAL> ["]                     :=> STRING
  <NORMAL> [']                     :=> SINGLE_QUOTE_STRING


  <NORMAL> identifier_start        :=> IDENTIFIER

  <NORMAL> number_start            :=> NUMBER


  <NORMAL> eof                     { PUSH_T(EOS); }
  <NORMAL> any                     { TERMINATE_ILLEGAL(); }



  <STRING> "\\\""                {}
  <STRING> ["]                   { PUSH_STRING(); TRANSITION(NORMAL); }
  <STRING> any                   {}


  <SINGLE_QUOTE_STRING> "\\'"    {}
  <SINGLE_QUOTE_STRING> "'"      { PUSH_STRING(); TRANSITION(NORMAL); }
  <SINGLE_QUOTE_STRING> any      {}



  <IDENTIFIER> identifier_char+  {}
  <IDENTIFIER> any               { PUSH_IDENTIFIER(); TRANSITION(NORMAL); }



  <SINGLE_LINE_COMMENT> line_terminator
                                 { PUSH_LINE_TERMINATOR(); TRANSITION(NORMAL); }

  <SINGLE_LINE_COMMENT> any+     {}



  <MULTILINE_COMMENT> [*][//]      { PUSH_LINE_TERMINATOR(); TRANSITION(NORMAL); }
  <MULTILINE_COMMENT> eof { TERMINATE_ILLEGAL(); }
  <MULTILINE_COMMENT> any+       {}

*/