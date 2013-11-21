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

from string import printable

class TransitionKey:
  '''Represents a transition from a state in DFA or NFA to another state.

  A transition key has a list of character ranges and a list of class ranges
  (e.g., "whitespace"), defining for which characters the transition
  happens. When we generate code based on the transition key, the character
  ranges generate simple checks and the class ranges generate more complicated
  conditions, e.g., function calls.'''

  __class_bounds = {
    'latin_1' : (1, 255),
    # These are not real ranges; they just need to be separate from any real
    # ranges.
    'non_latin_1_whitespace' : (256, 256),
    'non_latin_1_letter' : (257, 257),
    'non_latin1_identifier_part_not_letter' : (258, 258),
    'non_latin1_line_terminator' : (259, 259),
    'eos' : (260, 260),
    'zero' : (261, 261),
  }
  __lower_bound = min(__class_bounds.values(), key=lambda item: item[0])[0]
  __upper_bound = max(__class_bounds.values(), key=lambda item: item[1])[1]

  __cached_keys = {}

  __unique_key_counter = -1

  __predefined_ranges = {
    'whitespace' : [
        (9, 9), (11, 12), (32, 32), (133, 133), (160, 160),
        __class_bounds['non_latin_1_whitespace']],
    'letter' : [
        (65, 90), (97, 122), (170, 170), (181, 181),
        (186, 186), (192, 214), (216, 246), (248, 255),
        __class_bounds['non_latin_1_letter']],
    'line_terminator' : [
        (10, 10), (13, 13),
        __class_bounds['non_latin1_line_terminator']],
    'identifier_part_not_letter' : [
        (48, 57), (95, 95),
        __class_bounds['non_latin1_identifier_part_not_letter']],
  }

  @staticmethod
  def __in_latin_1(char):
    bound = TransitionKey.__class_bounds['latin_1']
    return (bound[0] <= char and char <= bound[1])

  @staticmethod
  def __is_class_range(r):
    return r[0] == r[1] and not TransitionKey.__in_latin_1(r[0])

  @staticmethod
  def __is_unique_range(r):
    return r[0] == r[1] and r[0] < TransitionKey.__lower_bound

  @staticmethod
  def __verify_ranges(ranges, check_merged):
    assert ranges
    if len(ranges) == 1 and TransitionKey.__is_class_range(ranges[0]):
      return
    last = None
    for r in ranges:
      assert TransitionKey.__lower_bound <= r[0]
      assert r[1] <= TransitionKey.__upper_bound
      assert r[0] <= r[1]
      r_is_class = TransitionKey.__is_class_range(r)
      # Assert that the ranges are in order.
      if last != None and check_merged:
        assert last[1] + 1 < r[0] or r_is_class
      if not TransitionKey.__in_latin_1(r[0]):
        assert r_is_class
      if not TransitionKey.__in_latin_1(r[1]):
        assert r_is_class
      last = r

  def __is_unique(self):
    return len(self.__ranges) == 1 and self.__is_unique_range(self.__ranges[0])

  @staticmethod
  def __cached_key(name, bounds_getter):
    if not name in TransitionKey.__cached_keys:
      bounds = bounds_getter(name)
      TransitionKey.__cached_keys[name] = TransitionKey(bounds, name)
    return TransitionKey.__cached_keys[name]

  @staticmethod
  def epsilon():
    '''Returns a TransitionKey for the epsilon (empty) transition.'''
    return TransitionKey.__cached_key('epsilon', lambda name : [])

  @staticmethod
  def any():
    '''Returns a TransitionKey which matches any character.'''
    return TransitionKey.__cached_key(
        'any',
        lambda name : sorted(TransitionKey.__class_bounds.values()))

  @staticmethod
  def single_char(char):
    '''Returns a TransitionKey for a single-character transition.'''
    char = ord(char)
    assert TransitionKey.__in_latin_1(char)
    return TransitionKey([(char, char)])

  @staticmethod
  def unique(name):
    '''Returns a unique TransitionKey for the given name (and creates it if it
    doesn't exist yet). The TransitionKey won't have any real character range,
    but a newly-created "mock" character range which is separate from all other
    character ranges.'''
    def get_bounds(name):
      bound = TransitionKey.__unique_key_counter
      TransitionKey.__unique_key_counter -= 1
      return [(bound, bound)]
    name = '__' + name
    return TransitionKey.__cached_key(name, get_bounds)

  @staticmethod
  def __process_graph(graph, ranges, key_map):
    key = graph[0]
    if key == 'RANGE':
      ranges.append((ord(graph[1]), ord(graph[2])))
    elif key == 'LITERAL':
      ranges.append((ord(graph[1]), ord(graph[1])))
    elif key == 'CAT':
      for x in [graph[1], graph[2]]:
        TransitionKey.__process_graph(x, ranges, key_map)
    elif key == 'CHARACTER_CLASS':
      class_name = graph[1]
      if class_name in TransitionKey.__class_bounds:
        if class_name in key_map:
          assert (key_map[class_name] ==
              TransitionKey([TransitionKey.__class_bounds[class_name]]))
        ranges.append(TransitionKey.__class_bounds[class_name])
      elif class_name in TransitionKey.__predefined_ranges:
        if class_name in key_map:
          assert (key_map[class_name] ==
              TransitionKey(TransitionKey.__predefined_ranges[class_name]))
        ranges += TransitionKey.__predefined_ranges[class_name]
      elif class_name in key_map:
        ranges += key_map[class_name].__ranges
      else:
        raise Exception('unknown character class [%s]' % graph[1])
    else:
      raise Exception('bad key [%s]' % key)

  @staticmethod
  def character_class(graph, key_map):
    '''Processes 'graph' (a representation of a character class in the rule
    file), and constructs a TransitionKey based on it. 'key_map' contains
    already constructed aliases for character classes (they can be used in the
    new character class). It is a map from strings (character class names) to
    TransitionKeys. For example, graph might represent the character class
    [a-z:digit:] where 'digit' is a previously constructed character class found
    in "key_map".'''
    ranges = []
    assert graph[0] == 'CLASS' or graph[0] == 'NOT_CLASS'
    invert = graph[0] == 'NOT_CLASS'
    TransitionKey.__process_graph(graph[1], ranges, key_map)
    return TransitionKey.__key_from_ranges(invert, ranges)

  def matches_char(self, char):
    char = ord(char)
    # TODO class checks
    for r in self.__ranges:
      if r[0] <= char and char <= r[1]: return True
    return False

  def is_superset_of_key(self, key):
    '''Returns true if 'key' is a sub-key of this TransitionKey.'''
    assert isinstance(key, self.__class__)
    assert key != TransitionKey.epsilon() and not key.__is_unique()
    assert len(key.__ranges) == 1
    subkey = key.__ranges[0]
    matches = False
    for k in self.__ranges:
      if k[0] <= subkey[0]:
        assert subkey[1] <= k[1] or subkey[0] > k[1]
      if subkey[0] < k[0]:
        assert subkey[1] < k[0]
      if k[0] <= subkey[0] and k[1] >= subkey[1]:
        assert not matches
        matches = True
    return matches

  @staticmethod
  def canonical_compare(left, right):
    i = 0
    left_length = len(left.__ranges)
    right_length = len(right.__ranges)
    while i < left_length and i < right_length:
      l = left.__ranges[i]
      r = right.__ranges[i]
      c = cmp(l, r)
      if c:
        return c
      i += 1
    if i == left_length and i == right_length:
      return 0
    return 1 if i != left_length else -1

  def __hash__(self):
    if self.__cached_hash == None:
      initial_hash = hash((-1, TransitionKey.__upper_bound + 1))
      f = lambda acc, r: acc ^ hash(r)
      self.__cached_hash = reduce(f, self.__ranges, initial_hash)
    return self.__cached_hash

  def __eq__(self, other):
    return isinstance(other, self.__class__) and self.__ranges == other.__ranges

  @staticmethod
  def __class_name(r):
    for name, v in TransitionKey.__class_bounds.items():
      if r == v: return name
    assert False

  def range_iter(self):
    assert not self == TransitionKey.epsilon() and not self.__is_unique()
    for r in self.__ranges:
      if self.__is_class_range(r):
        yield ('CLASS', TransitionKey.__class_name(r))
      else:
        yield ('LATIN_1', r)

  __printable_cache = {
    ord('\t') : '\\t',
    ord('\n') : '\\n',
    ord('\r') : '\\r',
  }

  @staticmethod
  def __range_str(r):
    if TransitionKey.__is_class_range(r):
      return TransitionKey.__class_name(r)
    def to_str(x):
      assert TransitionKey.__in_latin_1(x)
      if not x in TransitionKey.__printable_cache:
        res = "'%s'" % chr(x) if chr(x) in printable else str(x)
        TransitionKey.__printable_cache[x] = res
      return TransitionKey.__printable_cache[x]
    if r[0] == r[1]:
      return '%s' % to_str(r[0])
    else:
      return '[%s-%s]' % (to_str(r[0]), to_str(r[1]))

  def __init__(self, ranges, name = None):
    if not ranges:
      assert name == 'epsilon'
      assert not name in TransitionKey.__cached_keys
    else:
      TransitionKey.__verify_ranges(ranges, True)
    self.__name = name
    self.__ranges = tuple(ranges) # immutable
    self.__cached_hash = None

  def __str__(self):
    if self.__name:
      return self.__name
    return ', '.join(TransitionKey.__range_str(x) for x in self.__ranges)

  @staticmethod
  def __disjoint_keys(range_map):
    '''Takes a set of possibly overlapping ranges, returns a list of ranges
    which don't overlap and which cover the same points as the original
    set. range_map is a map from lower bounds to a list of upper bounds.'''
    sort = lambda x : sorted(set(x))
    range_map = sorted(map(lambda (k, v): (k, sort(v)), range_map.items()))
    ranges = []
    upper_bound = TransitionKey.__upper_bound + 1
    for i in range(len(range_map)):
      (left, left_values) = range_map[i]
      next = range_map[i + 1][0] if i != len(range_map) - 1 else upper_bound
      to_push = []
      for v in left_values:
        assert left <= next
        if v >= next:
          if not to_push and left < next:
            ranges.append((left, next - 1))
          to_push.append(v)
        else:
          ranges.append((left, v))
          left = v + 1
      if to_push:
        current = range_map[i + 1]
        range_map[i + 1] = (current[0], sort(current[1] + to_push))
    return ranges

  @staticmethod
  def __disjoint_ranges_from_key_set(key_set):
    if not key_set:
      return []
    range_map = {}
    for x in key_set:
      assert not x.__is_unique()
      assert x != TransitionKey.epsilon
      for r in x.__ranges:
        if not r[0] in range_map:
          range_map[r[0]] = []
        range_map[r[0]].append(r[1])
    ranges = TransitionKey.__disjoint_keys(range_map)
    TransitionKey.__verify_ranges(ranges, False)
    return ranges

  @staticmethod
  def disjoint_keys(key_set):
    '''Takes a set of possibly overlapping TransitionKeys, returns a list of
    TransitionKeys which don't overlap and whose union is the same as the union
    of the original key_set. In addition, TransitionKeys are not merged, only
    split.

    For example, if key_set contains two TransitionKeys for ranges [1-10] and
    [5-15], disjoint_keys returns a set of three TransitionKeys: [1-4], [5-10],
    [11-16].'''
    ranges = TransitionKey.__disjoint_ranges_from_key_set(key_set)
    return map(lambda x : TransitionKey([x]), ranges)

  @staticmethod
  def inverse_key(key_set):
    '''Returns a TransitionKey which matches represents the inverse of the union
    of 'key_set'. The TransitionKeys contain a set of character ranges and a set
    of classes. The character ranges are inverted in relation to the latin_1
    character range, and the character classes are inverted in relation to all
    character classes in __class_bounds.'''
    ranges = TransitionKey.__disjoint_ranges_from_key_set(key_set)
    inverse = TransitionKey.__invert_ranges(ranges)
    if not inverse:
      return None
    return TransitionKey(inverse)

  @staticmethod
  def __key_from_ranges(invert, ranges):
    range_map = {}
    for r in ranges:
      if not r[0] in range_map:
        range_map[r[0]] = []
      range_map[r[0]].append(r[1])
    ranges = TransitionKey.__disjoint_keys(range_map)
    ranges = TransitionKey.__merge_ranges(ranges)
    if invert:
      ranges = TransitionKey.__invert_ranges(ranges)
    return TransitionKey(ranges)

  @staticmethod
  def __merge_ranges(ranges):
    merged = []
    last = None
    for r in ranges:
      assert not TransitionKey.__is_unique_range(r)
      if last == None:
        last = r
      elif (last[1] + 1 == r[0] and not TransitionKey.__is_class_range(r)):
        last = (last[0], r[1])
      else:
        merged.append(last)
        last = r
    if last != None:
      merged.append(last)
    return merged

  @staticmethod
  def merged_key(keys):
    f = lambda acc, key: acc + list(key.__ranges)
    return TransitionKey.__key_from_ranges(False, reduce(f, keys, []))

  @staticmethod
  def __invert_ranges(ranges):
    inverted = []
    last = None
    # Extract character classes (as opposed to character ranges) from
    # __class_bounds. Since latin_1 is the only real character range, we can do
    # this.
    classes = set(TransitionKey.__class_bounds.values())
    latin_1 = TransitionKey.__class_bounds['latin_1']
    classes.remove(latin_1)
    for r in ranges:
      assert not TransitionKey.__is_unique_range(r)
      if TransitionKey.__is_class_range(r):
        classes.remove(r)
        continue
      if last == None:
        if r[0] != TransitionKey.__lower_bound:
          inverted.append((TransitionKey.__lower_bound, r[0] - 1))
      elif last[1] + 1 < r[0]:
        inverted.append((last[1] + 1, r[0] - 1))
      last = r
    upper_bound = latin_1[1]
    if last == None:
      inverted.append(latin_1)
    elif last[1] < upper_bound:
      inverted.append((last[1] + 1, upper_bound))
    inverted += list(classes)
    return inverted
