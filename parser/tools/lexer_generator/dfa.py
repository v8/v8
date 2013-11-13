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

  def collect_actions(self, string):
    state = self.__start
    for c in string:
      state = Dfa.__match_char(state, c)
      if not state:
        yield Action('MISS')
        return
      if state.action():
        yield state.action()
    if state in self.__terminal_set:
      yield Action('TERMINATE')
    else:
      yield Action('MISS')

  def matches(self, string):
    actions = list(self.collect_actions(string))
    return actions and actions[-1].type() == 'TERMINATE'

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
    paritions = []
    working_set = []
    action_map = {}
    id_map = {}
    def f(state, visitor_state):
      node_number = state.node_number()
      assert not node_number in id_map
      id_map[node_number] = state
      action = state.action()
      if not action in action_map:
        action_map[action] = set()
      action_map[action].add(node_number)
    self.visit_all_states(f)
    total = 0
    for p in action_map.values():
      paritions.append(p)
      total += len(p)
    assert total == self.__node_count

