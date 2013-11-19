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
from automaton import *
from transition_keys import TransitionKey

class DfaState(AutomatonState):

  def __init__(self, name, action):
    super(DfaState, self).__init__()
    self.__name = name
    self.__transitions = {}
    self.__action = action

  def transitions_to_multiple_states(self):
    return False

  def name(self):
    return self.__name

  def action(self):
    return self.__action

  def add_transition(self, key, state):
    assert not self.__transitions.has_key(key)
    self.__transitions[key] = state

  def transitions(self):
    return self.__transitions

  # TODO abstract state matching
  def __matches(self, match_func, value):
    # f collects states whose corresponding TransitionKey matches 'value'.
    f = (lambda acc, (key, state):
           acc | set([state]) if match_func(key, value) else acc)
    matches = reduce(f, self.__transitions.items(), set())
    # Since this is a dfa, we should have at most one such state. Return it.
    if not matches:
      return None
    assert len(matches) == 1
    return iter(matches).next()

  def next_state_with_char(self, value):
    return self.__matches(lambda k, v : k.matches_char(v), value)

  def key_matches(self, value):
    return self.__matches(lambda k, v : k.is_superset_of_key(v), value)

class Dfa(Automaton):

  def __init__(self, start_name, mapping):
    super(Dfa, self).__init__()
    self.__terminal_set = set()
    name_map = {}
    for name, node_data in mapping.items():
      node = DfaState(name, node_data['action'])
      name_map[name] = node
      if node_data['terminal']:
        self.__terminal_set.add(node)
    for name, node_data in mapping.items():
      node = name_map[name]
      inversion = {}
      for key, state in node_data['transitions'].items():
        if not state in inversion:
          inversion[state] = []
        inversion[state].append(key)
      for state, keys in inversion.items():
        merged_key = TransitionKey.merged_key(keys)
        node.add_transition(merged_key, name_map[state])
    self.__start = name_map[start_name]
    self.__node_count = len(mapping)
    self.__verify()

  def __verify(self):
    assert self.__terminal_set
    state_count = self.visit_all_states(lambda state, count: count + 1, 0)
    assert self.__node_count == state_count

  def node_count(self):
    return self.__node_count

  def start_state(self):
    return self.__start

  def start_set(self):
    return set([self.__start])

  def terminal_set(self):
    return set(self.__terminal_set)

  @staticmethod
  def __match_char(state, char):
    match = list(state.state_iter(key_filter = lambda k: k.matches_char(char)))
    if not match: return None
    assert len(match) == 1
    return match[0]

  @staticmethod
  def terminal_action():
    return Action(None, ('TERMINATE', None))

  @staticmethod
  def miss_action():
    return Action(None, ('Miss', None))

  def collect_actions(self, string):
    state = self.__start
    for c in string:
      state = Dfa.__match_char(state, c)
      if not state:
        yield self.miss_action()
        return
      if state.action():
        yield state.action()
    if state in self.__terminal_set:
      yield self.terminal_action()
    else:
      yield self.miss_action()

  def matches(self, string):
    actions = list(self.collect_actions(string))
    return actions and actions[-1] == self.terminal_action()

  def lex(self, string):
    state = self.__start
    last_position = 0
    for pos, c in enumerate(string):
      next = Dfa.__match_char(state, c)
      if not next:
        assert state.action() # must invoke default action here
        yield (state.action(), last_position, pos)
        last_position = pos
        # lex next token
        next = Dfa.__match_char(self.__start, c)
        assert next
      state = next
    assert state.action() # must invoke default action here
    yield (state.action(), last_position, len(string))

  def minimize(self):
    return DfaMinimizer(self).minimize()

class StatePartition(object):

  def __init__(self, node_numbers):
    self.__node_numbers = node_numbers
    assert self.__node_numbers
    self.__hash = None

  def set(self):
    return self.__node_numbers

  def __hash__(self):
    if not self.__hash:
      self.__hash = reduce(lambda acc, x: acc ^ hash(x), self.__node_numbers, 0)
    return self.__hash

  def __eq__(self, other):
    return (isinstance(other, self.__class__) and
            self.__node_numbers == other.__node_numbers)

  def __len__(self):
    return len(self.__node_numbers)

  def __iter__(self):
    return self.__node_numbers.__iter__()

  def __str__(self):
    return str([x for x in self.__node_numbers])

class DfaMinimizer:

  __verify = True

  @staticmethod
  def set_verify(verify):
    DfaMinimizer.__verify = verify

  def __init__(self, dfa):
    self.__dfa = dfa
    self.__id_map = None
    self.__reverse_id_map = None
    self.__alphabet = None

  def __partition(self):
    assert not self.__id_map
    assert not self.__reverse_id_map
    assert not self.__alphabet
    action_map = {}
    id_map = {}
    terminal_set = self.__dfa.terminal_set()
    all_keys = []
    def f(state, visitor_state):
      state_id = len(id_map)
      id_map[state_id] = state
      action = state.action()
      all_keys.append(state.key_iter())
      if action:
        if state not in terminal_set:
          assert action.entry_action()
          key = ("nonterminal action", action)
        else:
          key = ("terminal action", action)
      elif state in terminal_set:
        key = ("terminal set",)
      else:
        key = ("nonterminal set",)
      if not key in action_map:
        action_map[key] = set()
      action_map[key].add(state_id)
    self.__dfa.visit_all_states(f)
    partitions = set()
    working_set = set()
    for k, p in action_map.items():
      p = StatePartition(p)
      partitions.add(p)
      if k[0] == "terminal_set" or k[0] == "terminal action" or True:
        working_set.add(p)
    reverse_id_map = {v : k for k, v in id_map.items()}
    assert len(id_map) == len(reverse_id_map)
    self.__reverse_id_map = reverse_id_map
    self.__id_map = id_map
    self.__alphabet = list(TransitionKey.disjoint_keys(chain(*all_keys)))
    # map transitions wrt alphabet mapping
    transitions = {}
    for state_id, state in id_map.items():
      def f((key_id, key)):
        transition = state.key_matches(key)
        if transition:
          return reverse_id_map[transition]
        return None
      transitions[state_id] = map(f, enumerate(self.__alphabet))
    self.__transitions = transitions
    # verify created structures
    if self.__verify:
      for partition in partitions:
        for state_id in partition:
          transition_array = transitions[state_id]
          state = id_map[state_id]
          for key_id, key in enumerate(self.__alphabet):
            transition_id = transition_array[key_id]
            transition_state = state.key_matches(key)
            if not transition_state:
              assert transition_id == None
            else:
              assert transition_id != None
              assert transition_id == reverse_id_map[transition_state]
    return (working_set, partitions)

  @staticmethod
  def __partition_count(partitions):
    return len(reduce(lambda acc, p: acc | p.set(), partitions, set()))

  def __merge_partitions(self, partitions):
    id_map = self.__id_map
    reverse_id_map = self.__reverse_id_map
    verify = self.__verify
    mapping = {}
    name_map = {}
    reverse_partition_map = {}
    for partition in partitions:
      name_map[partition] = str(partition)
      for state_id in partition:
        reverse_partition_map[state_id] = partition
    transitions = self.__transitions
    for partition in partitions:
      state_ids = list(partition)
      state_id = state_ids[0]
      state = id_map[state_id]
      node = {
        'transitions' : {},
        'terminal' : state in self.__dfa.terminal_set(),
        'action' : state.action(),
      }
      mapping[str(partition)] = node
      transition_array = transitions[state_id]
      for key_id, key in enumerate(self.__alphabet):
        transition_id = transition_array[key_id]
        if transition_id == None:
          if verify:
            assert not state.key_matches(key)
            for s_id in state_ids:
              assert not id_map[s_id].key_matches(key)
          continue
        transition_partition = reverse_partition_map[transition_id]
        assert transition_partition
        if verify:
          for s_id in state_ids:
            transition = id_map[s_id].key_matches(key)
            assert transition
            test_partition = reverse_partition_map[reverse_id_map[transition]]
            assert test_partition == transition_partition
        node['transitions'][key] = name_map[transition_partition]
    start_id = reverse_id_map[self.__dfa.start_state()]
    start_name = name_map[reverse_partition_map[start_id]]
    return (start_name, mapping)

  def minimize(self):
    (working_set, partitions) = self.__partition()
    node_count = self.__dfa.node_count()
    id_map = self.__id_map
    reverse_id_map = self.__reverse_id_map
    transitions = self.__transitions
    key_range = range(0, len(self.__alphabet))
    while working_set:
      assert working_set <= partitions
      assert self.__partition_count(partitions) == node_count
      test_partition = iter(working_set).next()
      working_set.remove(test_partition)
      test_array = [False for x in range(0, len(id_map))]
      for x in test_partition:
        test_array[x] = True
      for key_index in key_range:
        map_into_partition = set()
        for state_id, transition_array in transitions.items():
          transition_id = transition_array[key_index]
          if transition_id != None and test_array[transition_id]:
            map_into_partition.add(state_id)
        if not map_into_partition:
          continue
        new_partitions = set()
        old_partitions = set()
        for p in partitions:
          intersection = p.set().intersection(map_into_partition)
          if not intersection:
            continue
          difference = p.set().difference(map_into_partition)
          if not difference:
            continue
          intersection = StatePartition(intersection)
          difference = StatePartition(difference)
          old_partitions.add(p)
          new_partitions.add(intersection)
          new_partitions.add(difference)
          if p in working_set:
            working_set.remove(p)
            working_set.add(intersection)
            working_set.add(difference)
          elif len(intersection) <= len(difference):
            working_set.add(intersection)
          else:
            working_set.add(difference)
        if old_partitions:
          partitions -= old_partitions
        if new_partitions:
          partitions |= new_partitions
    (start_name, mapping) = self.__merge_partitions(partitions)
    return Dfa(start_name, mapping)
