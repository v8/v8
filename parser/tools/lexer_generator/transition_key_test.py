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
from transition_keys import TransitionKey
from regex_parser import RegexParser

class TransitionKeyTestCase(unittest.TestCase):

  __equal_pairs = [
    (TransitionKey.epsilon(), TransitionKey.epsilon()),
    (TransitionKey.any(), TransitionKey.any()),
    (TransitionKey.single_char('a'), TransitionKey.single_char('a')),
  ]

  def test_eq(self):
    for (left, right) in self.__equal_pairs:
      self.assertEqual(left, right)

  def test_hash(self):
    for (left, right) in self.__equal_pairs:
      self.assertEqual(hash(left), hash(right))

  def test_classes(self):
    # class regex, should match, should not match
    data = [
      ("1-2", "12", "ab"),
      ("a-zA-Z", "abyzABYZ" , "123"),
      ("a-zA-Z0g" , "abyzABYZ0" , "123"),
    ]
    for (string, match, no_match) in data:
      for invert in [False, True]:
        if invert:
          regex = "[^%s]" % string
          token = "NOT_CLASS"
        else:
          regex = "[%s]" % string
          token = "CLASS"
        graph = RegexParser.parse(regex)
        assert graph[0] == token
        key = TransitionKey.character_class(invert, graph[1])
        for c in match:
          self.assertEqual(invert, not key.matches_char(c))
        for c in no_match:
          self.assertEqual(invert, key.matches_char(c))

if __name__ == '__main__':
    unittest.main()
