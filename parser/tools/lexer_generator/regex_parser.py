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

import ply.lex as lex
import ply.yacc as yacc
from action import Term
from nfa_builder import NfaBuilder

class ParserBuilder:

  class Logger(object):
    def debug(self,msg,*args,**kwargs):
      pass

    def info(self,msg,*args,**kwargs):
      pass

    def warning(self,msg,*args,**kwargs):
      raise Exception("warning: "+ (msg % args) + "\n")

    def error(self,msg,*args,**kwargs):
      raise Exception("error: "+ (msg % args) + "\n")

  __static_instances = {}
  @staticmethod
  def parse(
      string, name, new_lexer, new_parser, preparse = None, postparse = None):
    if not name in ParserBuilder.__static_instances:
      logger = ParserBuilder.Logger()
      lexer_instance = new_lexer()
      lexer_instance.lex = lex.lex(module=lexer_instance)
      instance = new_parser()
      instance.yacc = yacc.yacc(
        module=instance, debug=True, write_tables=0,
        debuglog=logger, errorlog=logger)
      ParserBuilder.__static_instances[name] = (lexer_instance, instance)
    (lexer_instance, instance) = ParserBuilder.__static_instances[name]
    if preparse:
      preparse(instance)
    try:
      return_value = instance.yacc.parse(string, lexer=lexer_instance.lex)
    except Exception:
      del ParserBuilder.__static_instances[name]
      raise
    if postparse:
      postparse(instance)
    return return_value

def build_escape_map(chars):
  def add_escape(d, char):
    d['\\' + char] = char
    return d
  return reduce(add_escape, chars,
    {'\\t' : '\t', '\\r' : '\r', '\\n' : '\n', '\\v' : '\v', '\\f' : '\f'})

class RegexLexer:

  tokens = (

    'GROUP_BEGIN',
    'GROUP_END',

    'CLASS_BEGIN',
    'CLASS_END',

    'OR',
    'ONE_OR_MORE',
    'ZERO_OR_MORE',
    'ZERO_OR_ONE',
    'ANY',

    'REPEAT_BEGIN',
    'REPEAT_END',

    'NUMBER',
    'COMMA',
    'LITERAL',

    'RANGE',
    'NOT',
    'CLASS_LITERAL',
    'CHARACTER_CLASS',
  )

  states = (
    ('class','exclusive'),
    ('repeat','exclusive'),
  )

  __escaped_literals = build_escape_map("(){}[]?+.*|'\"\\")

  def t_ESCAPED_LITERAL(self, t):
    r'\\.'
    t.type = 'LITERAL'
    t.value = RegexLexer.__escaped_literals[t.value]
    return t

  t_GROUP_BEGIN = r'\('
  t_GROUP_END = r'\)'

  t_OR = r'\|'
  t_ONE_OR_MORE = r'\+'
  t_ZERO_OR_MORE = r'\*'
  t_ZERO_OR_ONE = r'\?'

  t_ANY = r'\.'

  t_LITERAL = r'.'

  def t_CLASS_BEGIN(self, t):
    r'\['
    self.lex.push_state('class')
    return t

  def t_class_CLASS_END(self, t):
    r'\]'
    self.lex.pop_state()
    return t

  t_class_RANGE = '-'
  t_class_NOT = '\^'
  t_class_CHARACTER_CLASS = r':\w+:'

  def t_class_CLASS_LITERAL_AS_OCTAL(self, t):
    r'\\\d+'
    t.type = 'CLASS_LITERAL'
    t.value = chr(int(t.value[1:], 8))
    return t

  __escaped_class_literals = build_escape_map("^[]-:\\")

  def t_class_ESCAPED_CLASS_LITERAL(self, t):
    r'\\.'
    t.type = 'CLASS_LITERAL'
    t.value = RegexLexer.__escaped_class_literals[t.value]
    return t

  t_class_CLASS_LITERAL = r'[\w *$_+\'\"/]'

  def t_REPEAT_BEGIN(self, t):
    r'\{'
    self.lex.push_state('repeat')
    return t

  def t_repeat_REPEAT_END(self, t):
    r'\}'
    self.lex.pop_state()
    return t

  t_repeat_NUMBER = r'[0-9]+'
  t_repeat_COMMA = r','

  t_ANY_ignore  = '\n'

  def t_ANY_error(self, t):
    raise Exception("Illegal character '%s'" % t.value[0])

class RegexParser:

  tokens = RegexLexer.tokens

  token_map = {
    '+': 'ONE_OR_MORE',
    '?': 'ZERO_OR_ONE',
    '*': 'ZERO_OR_MORE',
    '.': 'ANY',
  }

  def p_start(self, p):
    '''start : fragments OR fragments
             | fragments'''
    if len(p) == 2:
      p[0] = p[1]
    else:
      p[0] = NfaBuilder.or_terms([p[1], p[3]])

  def p_fragments(self, p):
    '''fragments : fragment
                 | fragment fragments'''
    if len(p) == 2:
      p[0] = p[1]
    else:
      p[0] = self.__cat(p[1], p[2])

  def p_fragment(self, p):
    '''fragment : literal maybe_modifier
                | class maybe_modifier
                | group maybe_modifier
                | any maybe_modifier
    '''
    if p[2]:
      if p[2][0] == 'REPEAT':
        p[0] = Term(p[2][0], p[2][1], p[2][2], p[1])
      else:
        p[0] = Term(p[2][0], p[1])
    else:
      p[0] = p[1]

  def p_maybe_modifier(self, p):
    '''maybe_modifier : ONE_OR_MORE
                      | ZERO_OR_ONE
                      | ZERO_OR_MORE
                      | REPEAT_BEGIN NUMBER REPEAT_END
                      | REPEAT_BEGIN NUMBER COMMA NUMBER REPEAT_END
                      | empty'''
    if len(p) == 4:
      p[0] = ("REPEAT", p[2], p[2])
    elif len(p) == 5:
      p[0] = ("REPEAT", p[2], p[4])
    elif p[1]:
      p[0] = (self.token_map[p[1]],)
    else:
      p[0] = None

  def p_literal(self, p):
    '''literal : LITERAL'''
    p[0] = Term('LITERAL', p[1])

  def p_any(self, p):
    '''any : ANY'''
    p[0] = Term(self.token_map[p[1]])

  def p_class(self, p):
    '''class : CLASS_BEGIN class_content CLASS_END
             | CLASS_BEGIN NOT class_content CLASS_END'''
    if len(p) == 4:
      p[0] = Term("CLASS", p[2])
    else:
      p[0] = Term("NOT_CLASS", p[3])

  def p_group(self, p):
    '''group : GROUP_BEGIN start GROUP_END'''
    p[0] = p[2]

  def p_class_content(self, p):
    '''class_content : CLASS_LITERAL RANGE CLASS_LITERAL maybe_class_content
                     | CLASS_LITERAL maybe_class_content
                     | CHARACTER_CLASS maybe_class_content
    '''
    if len(p) == 5:
      left = Term("RANGE", p[1], p[3])
    else:
      if len(p[1]) == 1:
        left = Term('LITERAL', p[1])
      else:
        left = Term('CHARACTER_CLASS', p[1][1:-1])
    p[0] = self.__cat(left, p[len(p)-1])

  def p_maybe_class_content(self, p):
    '''maybe_class_content : class_content
                           | empty'''
    p[0] = p[1]

  def p_empty(self, p):
    'empty :'

  def p_error(self, p):
    raise Exception("Syntax error in input '%s'" % str(p))

  @staticmethod
  def __cat(left, right):
    assert left
    return NfaBuilder.cat_terms([left] if not right else [left, right])

  @staticmethod
  def parse(string):
    new_lexer = lambda: RegexLexer()
    new_parser = lambda: RegexParser()
    return ParserBuilder.parse(string, "RegexParser", new_lexer, new_parser)
