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
from lexer_generator.transition_keys import KeyEncoding
from lexer_generator.rule_parser import RuleParserState, RuleParser
from lexer_generator.regex_parser import RegexParser

class RuleParserTestCase(unittest.TestCase):

   def setUp(self):
     self.state = RuleParserState(KeyEncoding.get('latin1'))

   def parse(self, string):
    return RuleParser.parse(string, self.state)

   def test_basic(self):
     self.parse('''
alias = /regex/;
<<cond1>> /regex/ <||cond2>
<<cond1>> alias <||cond2>
<<cond2>> /regex/ <|body|>
<<cond2>> alias <|body|>
<<cond3>> /regex/ <body||>
<<cond3>> alias <body||>''')

     self.assertTrue(len(self.state.aliases), 1)
     self.assertTrue('alias' in self.state.aliases)
     self.assertEquals(self.state.aliases['alias'], RegexParser.parse('regex'))

     self.assertTrue(len(self.state.rules), 2)
     self.assertTrue('cond1' in self.state.rules)
     self.assertEquals(len(self.state.rules['cond1']['trees']), 2)

     self.assertTrue('cond2' in self.state.rules)
     self.assertEquals(len(self.state.rules['cond2']['trees']), 2)

     self.assertTrue('cond3' in self.state.rules)
     self.assertEquals(len(self.state.rules['cond3']['trees']), 2)

   def test_more_complicated(self):
     self.parse('''
alias = "regex;with;semicolon";
<<cond1>> "regex3}with}braces}" <|body|>
<<cond1>> "regex4{with{braces}" <body||>''')

     self.assertEquals(self.state.aliases['alias'],
                       RegexParser.parse("regex;with;semicolon"))
     self.assertTrue('cond1' in self.state.rules)
     self.assertEquals(len(self.state.rules['cond1']['trees']), 2)
