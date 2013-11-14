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
from automaton import Action
from rule_parser import RuleProcessor
from nfa_builder import NfaBuilder
from dfa import Dfa

def process_rules(rules):
  rule_map = {}
  for name, automata in RuleProcessor.parse(rules).automata_iter():
    rule_map[name] = automata
  return rule_map

class ActionTestCase(unittest.TestCase):

    def __verify_last_action(self, automata, string, expected_code):
      expected_code = (expected_code, None)
      for automaton in [automata.dfa(), automata.minimal_dfa()]:
        actions = list(automaton.collect_actions(string))
        self.assertEqual(actions[-1], Action('TERMINATE'))
        self.assertEqual(actions[-2].match_action(), expected_code)

    def test_action_precedence(self):
      rules = '''<<default>>
                 "key" <|KEYWORD|>
                 /[a-z]+/ <|ID|>'''
      automata_for_conditions = process_rules(rules)
      self.assertEqual(len(automata_for_conditions), 1)
      self.assertTrue('default' in automata_for_conditions)
      automata = automata_for_conditions['default']

      self.__verify_last_action(automata, 'foo', 'ID')
      self.__verify_last_action(automata, 'key', 'KEYWORD')
      self.__verify_last_action(automata, 'k', 'ID')
      self.__verify_last_action(automata, 'ke', 'ID')
      self.__verify_last_action(automata, 'keys', 'ID')

    def test_wrong_action_precedence(self):
      rules = '''<<default>>
                 /[a-z]+/ <|ID|>
                 "key" <|KEYWORD|>'''
      automata_for_conditions = process_rules(rules)
      self.assertEqual(len(automata_for_conditions), 1)
      self.assertTrue('default' in automata_for_conditions)
      automata = automata_for_conditions['default']

      # The keyword is not recognized because of the rule preference order (ID
      # is preferred over KEYWORD).
      self.__verify_last_action(automata, 'foo', 'ID')
      self.__verify_last_action(automata, 'key', 'ID')
