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

import unittest
from lexer import Lexer

class LexerTestCase(unittest.TestCase):

  def __verify_action_stream(self, stream, expected_stream):
    for (ix, item) in enumerate(expected_stream):
      self.assertEquals(stream[ix][0], item[0])
      self.assertEquals(stream[ix][4], item[1])

  def test_simple(self):
    rules = '''
    <default>
    "("           { LBRACE }
    ")"           { RBRACE }

    "foo"         { FOO }
    eof           <<terminate>>'''

    lexer = Lexer(rules)

    string = 'foo()\0'
    self.__verify_action_stream(
        lexer.lex(string),
        [('FOO', 'foo'), ('LBRACE', '('), ('RBRACE', ')')])

  def test_maximal_matching(self):
    rules = '''
    <default>
    "<"           { LT }
    "<<"          { SHL }
    " "           { SPACE }
    eof           <<terminate>>'''

    lexer = Lexer(rules)

    string = '<< <\0'
    self.__verify_action_stream(
        lexer.lex(string),
        [('SHL', '<<'), ('SPACE', ' '), ('LT', '<')])


  def test_consecutive_epsilon_transitions(self):
    # This rule set will create a NFA where we first have an epsilon transition,
    # then following that, an epsilon transition with an action. This will test
    # that the action is correctly pulled forward when creating the dfa.
    rules = '''
    digit = [0-9];
    number = (digit+ ("." digit+)?);
    <default>
    number        { NUMBER }
    eof           <<terminate>>'''

    lexer = Lexer(rules)

    string = '555\0'
    self.__verify_action_stream(
        lexer.lex(string),
        [('NUMBER', '555')])
