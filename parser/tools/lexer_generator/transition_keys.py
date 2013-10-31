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

class TransitionKey:

  __lower_bound = 0
  __latin_1_upper_bound = 255
  __unicode_whitespace_bounds = (256, 256)
  __unicode_literal_bounds = (257, 257)
  __upper_bound = 257

  @staticmethod
  def __verify_ranges(ranges):
    last = (TransitionKey.__lower_bound - 1, TransitionKey.__lower_bound - 1)
    for r in ranges:
      assert TransitionKey.__lower_bound <= r[0]
      assert r[1] <= TransitionKey.__upper_bound
      assert r[0] <= r[1]
      assert last[1] < r[0]
      # TODO check classes are always disjoint points
      last = r

  @staticmethod
  def __create(ranges):
    TransitionKey.__verify_ranges(ranges)
    key = TransitionKey()
    key.__ranges = ranges
    key.__cached_hash = None
    return key

  __cached_epsilon = None
  @staticmethod
  def epsilon():
    if (TransitionKey.__cached_epsilon == None):
      TransitionKey.__cached_epsilon = TransitionKey.__create([])
    return TransitionKey.__cached_epsilon

  __cached_any = None
  @staticmethod
  def any():
    if (TransitionKey.__cached_any == None):
      bounds = [
        (TransitionKey.__lower_bound, TransitionKey.__latin_1_upper_bound),
        TransitionKey.__unicode_whitespace_bounds,
        TransitionKey.__unicode_literal_bounds,
      ]
      TransitionKey.__cached_any = TransitionKey.__create(bounds)
    return TransitionKey.__cached_any

  @staticmethod
  def single_char(char):
    char = ord(char)
    assert (TransitionKey.__lower_bound <= char and
            char <= TransitionKey.__latin_1_upper_bound)
    return TransitionKey.__create([(char, char)])

  @staticmethod
  def character_class(invert, graph):
    # TODO
    return TransitionKey.__create([(129, 129)])

  def matches_char(self, char):
    char = ord(char)
    # TODO class checks
    for r in self.__ranges:
      if r[0] <= char and char <= r[1]: return True
    return False

  def matches_key(self, key):
    assert isinstance(key, self.__class__)
    assert key != TransitionKey.epsilon()
    assert len(key.__ranges) == 1
    subkey = key.__ranges[0]
    for k in self.__ranges:
      if k[0] <= subkey[0] and k[1] >= subkey[1]: return True
    return False

  def __hash__(self):
    if self.__cached_hash == None:
      initial_hash = hash((-1, TransitionKey.__upper_bound + 1))
      f = lambda acc, r: acc ^ hash(r)
      self.__cached_hash = reduce(f, self.__ranges, initial_hash)
    return self.__cached_hash

  def __eq__(self, other):
    return isinstance(other, self.__class__) and self.__ranges == other.__ranges

  @staticmethod
  def __print_range(r):
    def to_str(x):
      if x <= TransitionKey.__latin_1_upper_bound:
        return chr(x)
      if x == TransitionKey.__unicode_literal_bounds[0]:
        return "literal"
      if x == TransitionKey.__unicode_whitespace_bounds[0]:
        return "whitespace"
      assert False
    if r[0] == r[1]:
      return "'%s'" % to_str(r[0])
    else:
      return "['%s'-'%s']" % (to_str(r[0]), to_str(r[1]))

  def __str__(self):
    if self == self.epsilon():
      return "epsilon"
    if self == self.any():
      return "any"
    return ", ".join(TransitionKey.__print_range(x) for x in self.__ranges)

  @staticmethod
  def disjoint_keys(key_set):
    ranges = sorted(reduce(lambda acc, x : acc + x.__ranges, key_set, []))
    for i in range(0, len(ranges)):
      right = ranges[i][1]
      limit = i
      for j in range(i + 1, len(ranges)):
        if ranges[j][0] > right:
          break
        assert ranges[j][0] >= ranges[i][0]
        limit = j
      if limit == i: continue
      for j in range(i + 1, limit + 1):
        ranges[j] = (right, ranges[j][1])
    TransitionKey.__verify_ranges(ranges)
    return map(lambda x : TransitionKey.__create([x]), ranges)
