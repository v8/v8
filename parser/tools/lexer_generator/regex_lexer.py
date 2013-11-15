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
    'CLASS_LITERAL_AS_OCTAL',
    'CHARACTER_CLASS',
  )

  states = (
    ('class','exclusive'),
    ('repeat','exclusive'),
  )

  __escaped_literals = build_escape_map("(){}[]?+.*|\\")

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
    self.lexer.push_state('class')
    return t

  def t_class_CLASS_END(self, t):
    r'\]'
    self.lexer.pop_state()
    return t

  t_class_RANGE = '-'
  t_class_NOT = '\^'
  t_class_CHARACTER_CLASS = r':\w+:'

  def t_class_CLASS_LITERAL_AS_OCTAL(self, t):
    r'\\\d+'
    return t

  __escaped_class_literals = build_escape_map("^[]-:\\")

  def t_class_ESCAPED_CLASS_LITERAL(self, t):
    r'\\.'
    t.type = 'CLASS_LITERAL'
    t.value = RegexLexer.__escaped_class_literals[t.value]
    return t

  t_class_CLASS_LITERAL = r'[\w *$_+\'/]'

  def t_REPEAT_BEGIN(self, t):
    r'\{'
    self.lexer.push_state('repeat')
    return t

  def t_repeat_REPEAT_END(self, t):
    r'\}'
    self.lexer.pop_state()
    return t

  t_repeat_NUMBER = r'[0-9]+'
  t_repeat_COMMA = r','

  t_ANY_ignore  = '\n'

  def t_ANY_error(self, t):
    raise Exception("Illegal character '%s'" % t.value[0])

  def build(self, **kwargs):
    self.lexer = lex.lex(module=self, **kwargs)
