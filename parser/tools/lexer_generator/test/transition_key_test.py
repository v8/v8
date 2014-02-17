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
from lexer_generator.transition_keys import TransitionKey, KeyEncoding
from lexer_generator.regex_parser import RegexParser

class TransitionKeyTestCase(unittest.TestCase):

  __encoding = KeyEncoding.get('latin1')

  __equal_pairs = [
    (TransitionKey.epsilon(), TransitionKey.epsilon()),
    (TransitionKey.any(__encoding), TransitionKey.any(__encoding)),
    (TransitionKey.single_char(__encoding, ord('a')),
     TransitionKey.single_char(__encoding, ord('a'))),
  ]

  def test_eq(self):
    for (left, right) in self.__equal_pairs:
      self.assertEqual(left, right)

  def test_hash(self):
    for (left, right) in self.__equal_pairs:
      self.assertEqual(hash(left), hash(right))

  def test_compare(self):
    key = lambda x : TransitionKey.range(self.__encoding, x[0], x[1])
    t_key = lambda x : TransitionKey.unique(x)
    def check(a, b, expected):
      self.assertEqual(expected, TransitionKey.compare(key(a), key(b)))
      self.assertEqual(-expected, TransitionKey.compare(key(b), key(a)))
    check([1,1], [1,1], 0)
    check([1,3], [1,3], 0)
    check([1,3], [4,5], -2)
    check([1,4], [2,3], -3)
    check([1,4], [2,4], -3)
    check([1,3], [1,4], -4)
    check([1,3], [2,4], -5)
    check([1,3], [3,4], -5)
    check([1,3], [3,4], -5)

  def test_superset(self):
    r_key = lambda a, b : TransitionKey.range(self.__encoding, a, b)
    t_key = lambda x : TransitionKey.unique(x)
    def merge(*keys):
      return TransitionKey.merged_key(self.__encoding, keys)
    def check(a, b, expected1, expected_none = None):
      if expected1 == None:
        expected2 = expected_none
      elif expected1 == True:
        expected2 = True if a == b else None
      else:
        expected2 = False  # disjoint sets remain disjoint
      for expected in [expected1, expected2]:
        try:
          result = a.is_superset_of_key(b)
        except Exception:
          self.assertTrue(expected == None)
          return
        self.assertTrue(expected != None)
        self.assertEqual(expected, result)
        (a, b) = (b, a)
    # subset cases
    check(merge(r_key(1, 2), r_key(4, 5)), merge(r_key(1, 2)), True)
    check(merge(r_key(1, 7), r_key(9, 10)), merge(r_key(3, 4)), True)
    check(merge(r_key(1, 7), r_key(9, 10)),
          merge(r_key(1, 7), r_key(9, 10)), True)
    check(merge(r_key(1, 4)), merge(r_key(2, 3)), True)
    # disjoint cases
    check(merge(r_key(1, 4)), merge(r_key(5, 6)), False)
    check(merge(t_key('t1')), merge(t_key('t2')), False)
    check(merge(r_key(1, 4), t_key('t1')),
          merge(r_key(5, 6), t_key('t2')), False)
    check(merge(r_key(5, 6), t_key('t1')),
          merge(r_key(1, 4), t_key('t2')), False)
    # exception cases
    check(merge(t_key('t1')), merge(t_key('t1'), t_key('t2')), None, True)
    check(merge(t_key('t1'), t_key('t3')),
          merge(t_key('t1'), t_key('t2')), None)
    check(merge(t_key('t2'), t_key('t1')),
          merge(t_key('t2'), t_key('t3')), None)
    check(merge(r_key(1, 7), r_key(9, 10)),
          merge(r_key(2, 8)), None)
    check(merge(r_key(1, 7), r_key(9, 10)),
          merge(r_key(10, 11)), None)
    check(merge(r_key(1, 7), r_key(9, 10)),
          merge(r_key(8, 9)), None)

  def test_classes(self):
    # class regex, should match, should not match
    data = [
      ("1-2", "12", "ab"),
      ("a-zA-Z", "abyzABYZ" , "123"),
      ("a-zA-Z0g" , "abyzABYZ0" , "123"),
      ("a-z:whitespace::letter:" , "abc" , "123"),
    ]
    classes = {}
    encoding = self.__encoding
    for (string, match, no_match) in data:
      for invert in [False, True]:
        if invert:
          regex = "[^%s]" % string
          token = "NOT_CLASS"
        else:
          regex = "[%s]" % string
          token = "CLASS"
        term = RegexParser.parse(regex)
        assert term.name() == token
        key = TransitionKey.character_class(encoding, term, classes)
        for c in match:
          self.assertEqual(invert, not key.matches_char(c))
        for c in no_match:
          self.assertEqual(invert, key.matches_char(c))

  def test_self_defined_classes(self):
    encoding = self.__encoding
    graph = RegexParser.parse("[a-z]")
    classes = {
      'self_defined' : TransitionKey.character_class(encoding, graph, {})}
    graph = RegexParser.parse("[^:self_defined:]")
    key = TransitionKey.character_class(encoding, graph, classes)
    self.assertTrue(key.matches_char('A'))

  def test_disjoint_keys(self):
    encoding = self.__encoding
    key = lambda x, y: TransitionKey.range(encoding, x , y)
    disjoint_set = TransitionKey.disjoint_keys(encoding,
                                               [key(1, 10), key(5, 15)])
    self.assertTrue(key(1, 4) in disjoint_set)
    self.assertTrue(key(5, 10) in disjoint_set)
    self.assertTrue(key(11, 15) in disjoint_set)
