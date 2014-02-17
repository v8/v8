# Copyright 2014 the V8 project authors. All rights reserved.
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

from types import IntType
from itertools import chain
from string import printable
from term import Term

class KeyEncoding(object):

  __encodings = {}

  __printable_cache = {
    ord('\t') : '\\t',
    ord('\n') : '\\n',
    ord('\r') : '\\r',
  }

  @staticmethod
  def to_str(encoding, x):
    assert not encoding or encoding.in_primary_range(x, x)
    if x > 127:
      return str(x)
    if not x in KeyEncoding.__printable_cache:
      res = "%s" % chr(x) if chr(x) in printable else str(x)
      KeyEncoding.__printable_cache[x] = res
    return KeyEncoding.__printable_cache[x]

  @staticmethod
  def get(name):
    if not KeyEncoding.__encodings:
      Latin1Encoding()
      Utf16Encoding()
      Utf8Encoding()
    return KeyEncoding.__encodings[name]

  def __init__(self, name, primary_range, named_ranges, predefined_ranges):
    assert not name in KeyEncoding.__encodings
    assert primary_range[0] <= primary_range[1]
    KeyEncoding.__encodings[name] = self
    self.__name = name
    self.__primary_range = primary_range
    self.__lower_bound = primary_range[0]
    self.__upper_bound = primary_range[1]
    self.__primary_range_component = self.numeric_range_term(primary_range[0],
                                                             primary_range[1])
    self.__named_ranges = {
      k : Term('NAMED_RANGE_KEY', k) for k in named_ranges }
    def f(v):
      if len(v) == 2:
        return self.numeric_range_term(v[0], v[1])
      elif len(v) == 1:
        assert v[0] in self.__named_ranges
        return self.__named_ranges[v[0]]
      raise Exception('bad args %s' % str(v))
    self.__predefined_ranges = {
      k : map(f, v) for k, v in predefined_ranges.iteritems() }

  def name(self):
    return self.__name

  def lower_bound(self):
    return self.__lower_bound

  def upper_bound(self):
    return self.__upper_bound

  def primary_range(self):
    return self.__primary_range

  def named_range(self, name):
    ranges = self.__named_ranges
    return Term.empty_term() if not name in ranges else ranges[name]

  def named_range_iter(self):
    return self.__named_range.iteritems()

  def named_range_key_iter(self):
    return self.__named_ranges.iterkeys()

  def named_range_value_iter(self):
    return self.__named_ranges.itervalues()

  def predefined_range_iter(self, name):
    ranges = self.__predefined_ranges
    return None if not name in ranges else iter(ranges[name])

  def __primary_range_iter(self):
    yield self.__primary_range_component

  def all_components_iter(self):
    return chain(self.__primary_range_iter(), self.__named_ranges.itervalues())

  def is_primary_range(self, r):
    assert len(r) == 2
    return self.in_primary_range(r[0], r[1])

  def in_primary_range(self, a, b):
    return self.lower_bound() <= a and b <= self.upper_bound()

  def numeric_range_term(self, a, b):
    assert type(a) == IntType and type(b) == IntType
    assert self.in_primary_range(a, b)
    return Term('NUMERIC_RANGE_KEY', a, b)

class Latin1Encoding(KeyEncoding):

  def __init__(self):
    super(Latin1Encoding, self).__init__(
      'latin1',
      (0, 255),
      [],
      {
        'whitespace':
          [(9, 9), (11, 12), (32, 32), (133, 133), (160, 160)],
        'letter':
          [(65, 90), (97, 122), (170, 170), (181, 181),
           (186, 186), (192, 214), (216, 246), (248, 255)],
        'line_terminator':
          [(10, 10), (13, 13)],
        'identifier_part_not_letter':
          [(48, 57), (95, 95)]
      })

class Utf16Encoding(KeyEncoding):

  def __init__(self):
    super(Utf16Encoding, self).__init__(
      'utf16',
      (0, 255),
      ['non_primary_whitespace',
       'non_primary_letter',
       'non_primary_identifier_part_not_letter',
       'non_primary_line_terminator',
       'non_primary_everything_else'],
      {
        'whitespace':
          [(9, 9), (11, 12), (32, 32), (133, 133), (160, 160),
           ('non_primary_whitespace',)],
        'letter':
          [(65, 90), (97, 122), (170, 170), (181, 181),
           (186, 186), (192, 214), (216, 246), (248, 255),
           ('non_primary_letter',)],
        'line_terminator':
          [(10, 10), (13, 13), ('non_primary_line_terminator',)],
        'identifier_part_not_letter':
          [(48, 57), (95, 95), ('non_primary_identifier_part_not_letter',)],
      })

class Utf8Encoding(KeyEncoding):

  def __init__(self):
    super(Utf8Encoding, self).__init__(
      'utf8',
      (0, 127),
      ['non_primary_whitespace',
       'non_primary_letter',
       'non_primary_identifier_part_not_letter',
       'non_primary_line_terminator',
       'non_primary_everything_else'],
      {
        'whitespace':
          [(9, 9), (11, 12), (32, 32), ('non_primary_whitespace',)],
        'letter':
          [(65, 90), (97, 122), ('non_primary_letter',)],
        'line_terminator':
          [(10, 10), (13, 13), ('non_primary_line_terminator',)],
        'identifier_part_not_letter':
          [(48, 57), (95, 95), ('non_primary_identifier_part_not_letter',)],
      })
