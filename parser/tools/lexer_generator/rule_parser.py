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

import ply.yacc as yacc
from rule_lexer import RuleLexer

class RuleParser:

  tokens = RuleLexer.tokens

  aliases = dict()
  transitions = dict()

  def p_statement_alias(self, p):
    'statement : ALIAS EQUALS REGEX'
    regex = self.lexer.lexer.lexmatch.group('regex')
    self.aliases[p[1]] = regex

  def p_statement_condition_transition(self, p):
    'statement : CONDITION_BEGIN CONDITION CONDITION_END REGEX_AND_TRANSITION'
    old_condition = p[2]
    regex = self.lexer.lexer.lexmatch.group('regex')
    new_condition = self.lexer.lexer.lexmatch.group('new')
    if old_condition not in self.transitions:
      self.transitions[old_condition] = []
    self.transitions[old_condition].append((regex, new_condition))

  def p_statement_condition_body(self, p):
    'statement : CONDITION_BEGIN CONDITION CONDITION_END REGEX_AND_BODY'
    old_condition = p[2]
    regex = self.lexer.lexer.lexmatch.group('regex')
    body = self.lexer.lexer.lexmatch.group('body')
    if old_condition not in self.transitions:
      self.transitions[old_condition] = []
    self.transitions[old_condition].append((regex, body))

  def p_empty(self, p):
    'empty :'

  def p_error(self, p):
    raise Exception("Syntax error in input '%s'" % p)

  def build(self, **kwargs):
    self.parser = yacc.yacc(module=self, **kwargs)
    self.lexer = RuleLexer()
    self.lexer.build(**kwargs)

  def parse(self, data):
    return self.parser.parse(data, lexer=self.lexer.lexer)
