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

from itertools import chain
from encoding import KeyEncoding
from action import Term

class TransitionKey(object):
  '''Represents a transition from a state in DFA or NFA to another state.

  A transition key has a list of character ranges and a list of class ranges
  (e.g., "whitespace"), defining for which characters the transition
  happens. When we generate code based on the transition key, the character
  ranges generate simple checks and the class ranges generate more complicated
  conditions, e.g., function calls.'''

  __cached_keys = {
    'no_encoding' : {},
    'latin1' : {},
    'utf8' : {},
    'utf16' : {},
  }

  @staticmethod
  def __cached_key(encoding, name, components_getter):
    encoding_name = encoding.name() if encoding else 'no_encoding'
    cache = TransitionKey.__cached_keys[encoding_name]
    if not name in cache:
      cache[name] = TransitionKey(encoding, components_getter())
    return cache[name]

  @staticmethod
  def epsilon():
    '''Returns a TransitionKey for the epsilon (empty) transition.'''
    return TransitionKey.__cached_key(None, 'epsilon',
      lambda : Term("EPSILON_KEY"))

  @staticmethod
  def omega():
    '''Always matches.'''
    return TransitionKey.__cached_key(None, 'omega', lambda : Term("OMEGA_KEY"))

  @staticmethod
  def any(encoding):
    '''Returns a TransitionKey which matches any encoded character.'''
    return TransitionKey.__cached_key(encoding, 'any',
        lambda : encoding.all_components_iter())

  @staticmethod
  def single_char(encoding, char):
    '''Returns a TransitionKey for a single-character transition.'''
    return TransitionKey.range(encoding, char, char)

  @staticmethod
  def range(encoding, a, b):
    '''Returns a TransitionKey for a single-character transition.'''
    return TransitionKey(encoding, encoding.numeric_range_term(a, b))

  @staticmethod
  def unique(term):  # TODO(dcarney): rename
    '''Returns a unique TransitionKey for the given name (and creates it if it
    doesn't exist yet). The TransitionKey won't have any real character range,
    but a newly-created "mock" character range which is separate from all other
    character ranges.'''
    return TransitionKey(None, Term("TERM_KEY", term))

  @staticmethod
  def __process_term(encoding, term, components, key_map):
    key = term.name()
    args = term.args()
    if key == 'RANGE':
      components.append(encoding.numeric_range_term(args[0], args[1]))
    elif key == 'LITERAL':
      components += map(lambda x : encoding.numeric_range_term(x, x), args)
    elif key == 'CAT':
      for x in args:
        TransitionKey.__process_term(encoding, x, components, key_map)
    elif key == 'CHARACTER_CLASS':
      class_name = args[0]
      if encoding.named_range(class_name):
        c = encoding.named_range(class_name)
        if class_name in key_map:
          assert key_map[class_name] == TransitionKey(encoding, c)
        components.append(c)
      elif encoding.predefined_range_iter(class_name):
        cs = list(encoding.predefined_range_iter(class_name))
        if class_name in key_map:
          assert key_map[class_name] == TransitionKey(encoding, cs)
        components += cs
      elif class_name in key_map:
        components += key_map[class_name].__flatten()
      else:
        raise Exception('unknown character class [%s]' % args[0])
    else:
      raise Exception('bad key [%s]' % key)

  @staticmethod
  def character_class(encoding, term, key_map):
    '''Processes 'term' (a representation of a character class in the rule
    file), and constructs a TransitionKey based on it. 'key_map' contains
    already constructed aliases for character classes (they can be used in the
    new character class). It is a map from strings (character class names) to
    TransitionKeys. For example, graph might represent the character class
    [a-z:digit:] where 'digit' is a previously constructed character class found
    in "key_map".'''
    components = []
    assert term.name() == 'CLASS' or term.name() == 'NOT_CLASS'
    invert = term.name() == 'NOT_CLASS'
    assert len(term.args()) == 1
    TransitionKey.__process_term(encoding, term.args()[0], components, key_map)
    key = TransitionKey.__key_from_components(encoding, invert, components)
    assert key != None, "not invertible %s " % str(term)
    return key

  def matches_char(self, char):
    'this is just for simple lexer testing and is incomplete'
    char = ord(char)
    assert 0 <= char and char < 128
    for c in self.__flatten():
      if c.name() == 'NUMERIC_RANGE_KEY':
        r = c.args()
        if r[0] <= char and char <= r[1]:
          return True
    return False

  # (disjoint, subset, advance_left, advance_right)
  __is_superset_of_key_helper = (
    (True, True, False, False),   # -5 : error
    (True, True, False, False),   # -4 : error
    (False, True, False, True),   # -3 :
    (False, False, True, False),  # -2 :
    (False, False, True, False),  # -1 :
    (False, True, True, True),    #  0 :
    (True, False, False, True),   #  1 :
    (True, False, False, True),   #  2 :
    (True, True, False, False),   #  3 : error
    (False, True, False, True),   #  4 :
    (True, True, False, False),   #  5 : error
  )

  def is_superset_of_key(self, key):
    '''Returns true if 'key' is a sub-key of this TransitionKey.
    must be called on a key that is either a subset or disjoint'''
    helper = TransitionKey.__is_superset_of_key_helper
    (left, right) = (self.__flatten(), key.__flatten())
    (disjoint, subset, advance_left, advance_right) = (False, False, True, True)
    (right_exhausted, left_exhausted) = (False, False)
    while advance_left or advance_right:
      if advance_right:
        try:
          r = right.next()
        except StopIteration:
          right_exhausted = True
      if advance_left:
        try:
          l = left.next()
        except StopIteration:
          left_exhausted = True
      if right_exhausted or left_exhausted:
        break
      c = TransitionKey.__component_compare(l, r)
      (d, s, advance_left, advance_right) = helper[c + 5]
      disjoint |= d
      subset |= s
    if not right_exhausted:
      disjoint = True
    if disjoint and subset:
      raise Exception('not a subset and not disjoint')
    return subset

  @staticmethod
  def compare(self, other):
    left = list(self.__flatten())
    right = list(other.__flatten())
    offset = 0
    while offset < len(left) and offset < len(right):
      c = TransitionKey.__component_compare(left[offset], right[offset])
      if c != 0:
        return c
      offset += 1
    return TransitionKey.__cmp(len(left), len(right))

  def __cmp__(self, other):
    return TransitionKey.compare(self, other)

  def __hash__(self):
    return hash(self.__term)

  def __ne__(self, other):
    return not self == other

  def __eq__(self, other):
    return isinstance(other, TransitionKey) and self.__term == other.__term

  def range_iter(self, encoding):
    for c in self.__flatten():
      if c.name() == 'NUMERIC_RANGE_KEY':
        yield ('PRIMARY_RANGE', (c.args()[0], c.args()[1]))
      elif c.name() == 'NAMED_RANGE_KEY':
        yield ('CLASS', c.args()[0])
      elif c.name() == 'TERM_KEY':
        yield ('UNIQUE', c.args()[0])
      elif c.name() == 'OMEGA_KEY':
        yield ('OMEGA', ())
      else:
        assert False, 'unimplemented %s' % c

  @staticmethod
  def __component_str(encoding, component):
    if component.name() == 'TERM_KEY':
      return component.args()[0]
    elif component.name() == 'NAMED_RANGE_KEY':
      return component.args()[0]
    elif component.name() == 'EPSILON_KEY':
      return 'epsilon'
    elif component.name() == 'OMEGA_KEY':
      return 'omega'
    elif component.name() == 'NUMERIC_RANGE_KEY':
      r = component.args()
      to_str = lambda x: KeyEncoding.to_str(encoding, x)
      if r[0] == r[1]:
        return "'%s'" % to_str(r[0])
      return "['%s'-'%s']" % (to_str(r[0]), to_str(r[1]))
    raise Exception('unprintable %s' % component)

  def __flatten(self):
    return self.__flatten_components([self.__term])

  @staticmethod
  def __flatten_components(components):
    for component in components:
      if component.name() != 'COMPOSITE_KEY':
        yield component
      else:
        for arg in component.args():
          yield arg

  __component_name_order = {
    'EPSILON_KEY' : 0,
    'NUMERIC_RANGE_KEY' : 1,
    'NAMED_RANGE_KEY' : 2,
    'TERM_KEY' : 3,
    'OMEGA_KEY' : 4
  }

  @staticmethod
  def __cmp(a, b):
    'wraps standard cmp function to return correct results for components'
    c = cmp(a, b)
    return 0 if c == 0 else (-1 if c < 0 else 1)

  @staticmethod
  def __component_name_compare(a, b):
    return TransitionKey.__cmp(TransitionKey.__component_name_order[a],
                               TransitionKey.__component_name_order[b])

  @staticmethod
  def __component_compare(a, b):
    '''component-wise compare function, returns the following values when
    comparing non identical numerical ranges:
      non-overlapping    : -2  -- a0 <= a1 < b0 <= b1
      b subset of a      : -3  -- a0 < b0 <= b1 <= a1
      a subset of b      : -4  -- a0 == b0 and a1 < b1
      a overlaps to left : -5  -- a0 < b0 and b0 <= a1 < b1
    otherwise a value in [-1, 1] is returned'''
    if a.name() != b.name():
      return TransitionKey.__component_name_compare(a.name(), b.name())
    if a.name() != 'NUMERIC_RANGE_KEY':
      return TransitionKey.__cmp(str(a), str(b))
    (a, b) = (a.args(), b.args())
    c0 = TransitionKey.__cmp(a[0], b[0])
    if c0 == 0:
      return 4 * TransitionKey.__cmp(a[1], b[1])  # either == or a subset
    sign = -1
    if c0 > 0:
      (a, b, sign) = (b, a, 1) # normalize ordering so that a0 < b0
    assert a[0] < b[0]
    if b[1] <= a[1]:  # subset
      return 3 * sign
    if a[1] < b[0]:  # non overlap
      return 2 * sign
    return 5 * sign  # partial overlap

  @staticmethod
  def __is_composable(term):
    return term.name() != 'EPSILON_KEY' and term.name() != 'OMEGA_KEY'

  @staticmethod
  def __construct(encoding, components):
    if isinstance(components, Term):
      components = [components]
    is_unique = False
    acc = []
    last = Term.empty_term()
    for current in TransitionKey.__flatten_components(components):
      name = current.name()
      # verify arguments
      if name == 'EPSILON_KEY' or name == 'OMEGA_KEY' or name == 'TERM_KEY':
        pass
      elif name == 'NUMERIC_RANGE_KEY':
        assert encoding.is_primary_range(current.args())
        if last.name() == 'NUMERIC_RANGE_KEY':
          assert last.args()[1] + 1 < current.args()[0], 'ranges must be merged'
      elif name == 'NAMED_RANGE_KEY':
        assert encoding.named_range(current.args()[0])
      else:
        raise Exception('illegal component %s' % str(current))
      # verify ordering, composability
      if last:
        assert TransitionKey.__is_composable(current), 'cannot compose'
        if len(acc) == 1:
          assert TransitionKey.__is_composable(last), 'cannot compose'
        c = TransitionKey.__component_compare(last, current)
        assert c == -1 or c == -2, 'bad order %s %s' % (str(last), str(current))
      acc.append(current)
      last = current
    assert acc, "must have components"
    return acc[0] if len(acc) == 1 else Term('COMPOSITE_KEY', *acc)

  def __init__(self, encoding, components):
    self.__term = TransitionKey.__construct(encoding, components)
    self.__cached_hash = None

  def to_string(self, encoding):
    return ', '.join(map(lambda x : TransitionKey.__component_str(encoding, x),
                         self.__flatten()))

  def __str__(self):
    return self.to_string(None)

  @staticmethod
  def __disjoint_keys(encoding, range_map):
    '''Takes a set of possibly overlapping ranges, returns a list of ranges
    which don't overlap and which cover the same points as the original
    set. range_map is a map from lower bounds to a list of upper bounds.'''
    sort = lambda x : sorted(set(x))
    range_map = sorted(map(lambda (k, v): (k, sort(v)), range_map.items()))
    upper_bound = encoding.upper_bound() + 1
    for i in range(len(range_map)):
      (left, left_values) = range_map[i]
      next = range_map[i + 1][0] if i != len(range_map) - 1 else upper_bound
      to_push = []
      for v in left_values:
        assert left <= next
        if v >= next:
          if not to_push and left < next:
            yield (left, next - 1)
          to_push.append(v)
        else:
          yield (left, v)
          left = v + 1
      if to_push:
        current = range_map[i + 1]
        range_map[i + 1] = (current[0], sort(current[1] + to_push))

  @staticmethod
  def __merge_ranges(encoding, ranges):
    last = None
    for r in ranges:
      if last == None:
        last = r
      elif last[1] + 1 == r[0]:
        last = (last[0], r[1])
      else:
        yield last
        last = r
    if last != None:
      yield last

  @staticmethod
  def __flatten_keys(keys):
    return chain(*map(lambda x : x.__flatten(), keys))

  @staticmethod
  def __disjoint_components_from_keys(encoding, keys, merge_ranges = False):
    return TransitionKey.__disjoint_components(
      encoding, TransitionKey.__flatten_keys(keys), merge_ranges)

  @staticmethod
  def __disjoint_components(encoding, components, merge_ranges):
    range_map = {}
    other_keys = set([])
    for x in components:
      if x.name() != 'NUMERIC_RANGE_KEY':
        other_keys.add(x)
        continue
      (start, end) = x.args()
      if not start in range_map:
        range_map[start] = []
      range_map[start].append(end)
    ranges = TransitionKey.__disjoint_keys(encoding, range_map)
    if merge_ranges:
      ranges = TransitionKey.__merge_ranges(encoding, ranges)
      other_keys = sorted(other_keys, cmp=TransitionKey.__component_compare)
    range_terms = map(
      lambda x : encoding.numeric_range_term(x[0], x[1]), ranges)
    return chain(iter(range_terms), iter(other_keys))

  @staticmethod
  def __key_from_components(encoding, invert, components):
    components = TransitionKey.__disjoint_components(encoding, components, True)
    if invert:
      components = TransitionKey.__invert_components(encoding, components)
    return None if not components else TransitionKey(encoding, components)

  @staticmethod
  def disjoint_keys(encoding, keys):
    '''Takes a set of possibly overlapping TransitionKeys, returns a list of
    TransitionKeys which don't overlap and whose union is the same as the union
    of the original key_set. In addition, TransitionKeys are not merged, only
    split.

    For example, if key_set contains two TransitionKeys for ranges [1-10] and
    [5-15], disjoint_keys returns a set of three TransitionKeys: [1-4], [5-10],
    [11-16].'''
    return map(lambda x : TransitionKey(encoding, x),
      TransitionKey.__disjoint_components_from_keys(encoding, keys))

  @staticmethod
  def inverse_key(encoding, keys):
    '''Returns a TransitionKey which matches represents the inverse of the union
    of 'keys'. The TransitionKeys contain a set of character ranges and a set
    of classes. The character ranges are inverted in relation to the latin_1
    character range, and the character classes are inverted in relation to all
    character classes in __class_bounds.'''
    return TransitionKey.__key_from_components(
      encoding, True, TransitionKey.__flatten_keys(keys))

  @staticmethod
  def merged_key(encoding, keys):
    return TransitionKey(encoding,
      TransitionKey.__disjoint_components_from_keys(encoding, keys, True))

  @staticmethod
  def __invert_components(encoding, components):
    key = lambda x, y: encoding.numeric_range_term(x, y)
    last = None
    classes = set(encoding.named_range_value_iter())
    for c in components:
      if c in classes:
        classes.remove(c)
        continue
      assert c.name() == 'NUMERIC_RANGE_KEY', 'unencoded key not invertible'
      r = c.args()
      assert encoding.is_primary_range(r)
      if last == None:
        if r[0] != encoding.lower_bound():
          yield key(encoding.lower_bound(), r[0] - 1)
      elif last[1] + 1 < r[0]:
        yield key(last[1] + 1, r[0] - 1)
      last = r
    upper_bound = encoding.primary_range()[1]
    if last == None:
      r = encoding.primary_range()
      yield key(r[0], r[1])
    elif last[1] < upper_bound:
      yield key(last[1] + 1, upper_bound)
    for c in sorted(classes, TransitionKey.__component_compare):
      yield c
