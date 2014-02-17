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
from lexer_generator.dot_utilities import *
from lexer_generator.automaton import Action
from lexer_generator.regex_parser import RegexParser
from lexer_generator.transition_key import TransitionKey, KeyEncoding
from lexer_generator.nfa_builder import NfaBuilder
from lexer_generator.dfa import Dfa
from lexer_generator.dfa_minimizer import DfaMinimizer

class AutomataTestCase(unittest.TestCase):

  __encoding = KeyEncoding.get('latin1')

  @staticmethod
  def __build_automata(string):
    encoding = AutomataTestCase.__encoding
    trees = {'main' : RegexParser.parse(string)}
    nfa = NfaBuilder.nfa(encoding, {}, trees, 'main')
    (start_name, dfa_nodes) = nfa.compute_dfa()
    dfa = Dfa(encoding, start_name, dfa_nodes)
    return (nfa, dfa, DfaMinimizer(dfa).minimize())

  # (pattern, should match, should not match)
  __test_data = [
    ("a", ["a"], ["b", ""]),
    ("ab", ["ab"], ["bb", ""]),
    ("a+b", ["ab", "aab", "aaab"], ["a", "b", ""]),
    ("a?b", ["ab", "b"], ["a", "c", ""]),
    ("a*b", ["ab", "aaab", "b"], ["a", "c", ""]),
    ("a|b", ["a", "b"], ["ab", "c", ""]),
    (".", ["a", "b"], ["", "aa"]),
    (".*", ["", "a", "abcaabbcc"], []),
    ("a.b", ["aab", "abb", "acb"], ["ab", ""]),
    ("a.?b", ["aab", "abb", "acb", "ab"], ["aaab", ""]),
    ("a.+b", ["aab", "abb", "acb"], ["aaac", "ab", ""]),
    (".|.", ["a", "b"], ["aa", ""]),
    ("//.", ["//a"], ["aa", ""]),
    ("[ab]{2}", ["aa", "ab", "ba", "bb"], ["", "a", "b", "aaa", "bbb"]),
    ("[ab]{2,3}", ["aa", "ab", "ba", "bb", "aab", "baa", "bbb"],
     ["", "a", "b", "aaaa", "bbba"]),
    ("[ab]{2,4}", ["aa", "ab", "ba", "bb", "aab", "baa", "bbb", "abab"],
     ["", "a", "b", "aaaba", "bbbaa"]),
    ("[\\101]", ["A"], ["B"])
  ]

  def test_matches(self):
    for (regex, matches, not_matches) in self.__test_data:
      automata = self.__build_automata(regex)
      for string in matches:
        for automaton in automata:
          self.assertTrue(automaton.matches(string))
      for string in not_matches:
        for automaton in automata:
          self.assertFalse(automaton.matches(string))

  def test_can_construct_dot(self):
    for (regex, matches, not_matches) in self.__test_data:
      for automaton in self.__build_automata(regex):
        automaton_to_dot(automaton)

  def test_minimization(self):
    encoding = self.__encoding
    def empty_node():
      return {
        'transitions' : {},
        'terminal' : False,
        'action' : Action.empty_action() }
    mapping = { k : empty_node() for k in ['S_0', 'S_1', 'S_2', 'S_3'] }
    key_a = TransitionKey.single_char(encoding, ord('a'))
    key_b = TransitionKey.single_char(encoding, ord('b'))
    key_c = TransitionKey.single_char(encoding, ord('c'))

    mapping['S_0']['transitions'][key_a] = 'S_1'
    mapping['S_0']['transitions'][key_b] = 'S_2'
    mapping['S_1']['transitions'][key_c] = 'S_3'
    mapping['S_2']['transitions'][key_c] = 'S_3'
    mapping['S_3']['terminal'] = True

    mdfa = DfaMinimizer(Dfa(encoding, 'S_0', mapping)).minimize()
    self.assertEqual(3, mdfa.node_count())
