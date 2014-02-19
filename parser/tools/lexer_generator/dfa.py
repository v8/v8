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
from transition_key import TransitionKey

class DfaState(AutomatonState):

  def __init__(self, name, action, transitions):
    super(DfaState, self).__init__()
    self.__name = name
    self.__transitions = transitions
    self.__action = action
    assert isinstance(action, Action)

  def sort_transitions(self):
    self.__transitions = sorted(self.__transitions,
                                cmp = lambda (k1, v1), (k2, v2): cmp(k1, k2))

  def name(self):
    return self.__name

  def action(self):
    return self.__action

  def omega_transition(self):
    if (self.__transitions and
        self.__transitions[-1][0] == TransitionKey.omega()):
      return self.__transitions[-1][1]
    return None

  def match_action(self):
    '''returns an action if this state's omega transition terminates
    immediately and has an action'''
    omega_chain = list(self.omega_chain_iter())
    if len(omega_chain) == 1 and omega_chain[0][1] == 0:
      return omega_chain[0][0].action()
    return Action.empty_action()

  def omega_chain_iter(self):
    state = self
    while True:
      state = state.omega_transition()
      if not state:
        return
      transistion_count = len(state.__transitions)
      yield (state, transistion_count)
      if not (transistion_count == 0 or
              (transistion_count == 1 and state.omega_transition())):
        return

  def epsilon_closure_iter(self):
    return iter([])

  def transition_state_for_key(self, value):
    matches = list(self.transition_state_iter_for_key(value))
    assert len(matches) <= 1
    return matches[0] if matches else None

  def key_state_iter(
    self,
    key_filter = lambda x: True,
    state_filter = lambda x: True,
    match_func = lambda x, y: True,
    yield_func = lambda x, y: (x, y)):
    for (key, state) in self.__transitions:
      if key_filter(key) and state_filter(state) and match_func(key, state):
        yield yield_func(key, state)

class Dfa(Automaton):

  @staticmethod
  def __add_transition(transitions, key, state):
    assert key != None and key != TransitionKey.epsilon()
    transitions.append((key, state))

  def __init__(self, encoding, start_name, mapping):
    super(Dfa, self).__init__(encoding)
    self.__terminal_set = set()
    name_map = {}
    for name, node_data in mapping.items():
      transitions = []
      node = DfaState(name, node_data['action'], transitions)
      name_map[name] = (node, transitions)
      if node_data['terminal']:
        self.__terminal_set.add(node)
    all_keys = []
    for name, node_data in mapping.items():
      (node, transitions) = name_map[name]
      inversion = {}
      omega_state = None
      for key, state in node_data['transitions'].items():
        if key == TransitionKey.omega():
          omega_state = name_map[state][0]
        if not state in inversion:
          inversion[state] = []
        inversion[state].append(key)
      for state, keys in inversion.items():
        all_keys += keys
        merged_key = TransitionKey.merged_key(encoding, keys)
        self.__add_transition(transitions, merged_key, name_map[state][0])
      node.sort_transitions()
      assert node.omega_transition() == omega_state
    self.__start = name_map[start_name][0]
    self.__node_count = len(mapping)
    self.__disjoint_keys = sorted(
      TransitionKey.disjoint_keys(encoding, all_keys))
    self.__verify()

  def __verify(self):
    assert self.__terminal_set
    state_count = self.visit_all_states(lambda state, count: count + 1, 0)
    assert self.__node_count == state_count
    # assert keys are disjoint
    def f(state, remaining):
      remaining = set(TransitionKey.disjoint_keys(self.encoding(),
                                                  state.key_iter()))
      for key in state.key_iter():
        to_drop = set(filter(lambda x : key.is_superset_of_key(x), remaining))
        assert to_drop
        remaining -= to_drop
      assert not remaining
    self.visit_all_states(f, set(self.disjoint_keys_iter()))

  def disjoint_keys_iter(self):
    return iter(self.__disjoint_keys)

  def node_count(self):
    return self.__node_count

  def start_state(self):
    return self.__start

  def terminal_set(self):
    return set(self.__terminal_set)
