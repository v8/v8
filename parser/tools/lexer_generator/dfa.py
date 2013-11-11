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
from nfa import Nfa
from transition_keys import TransitionKey

class DfaState(AutomatonState):

  def __init__(self, name, node_number, actions):
    super(DfaState, self).__init__(node_number)
    self.__name = name
    self.__transitions = {}
    self.__actions = actions

  def name(self):
    return self.__name

  def action(self):
    return self.__actions[0] if self.__actions else None

  def actions(self):
    return self.__actions

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
    for i, (name, node_data) in enumerate(mapping.items()):
      node = DfaState(name, i, node_data['actions'])
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
    assert self.__terminal_set

  @staticmethod
  def __match_char(state, char):
    match = [s for k, s in state.transitions().items() if k.matches_char(char)]
    if not match: return None
    assert len(match) == 1
    return match[0]

  def collect_actions(self, string):
    state = self.__start
    for c in string:
      state = Dfa.__match_char(state, c)
      if not state:
        yield ('MISS',)
        return
      if state.action():
        yield state.action()
    if state in self.__terminal_set:
      yield ('TERMINATE', )
    else:
      yield ('MISS',)

  def matches(self, string):
    actions = list(self.collect_actions(string))
    return actions and actions[-1][0] == 'TERMINATE'

  def lex(self, string):
    state = self.__start
    last_position = 0
    for pos, c in enumerate(string):
      next = Dfa.__match_char(state, c)
      if not next:
        assert state.action() # must invoke default action here
        yield (state.action()[1], last_position, pos)
        last_position = pos
        # lex next token
        next = Dfa.__match_char(self.__start, c)
        assert next
      state = next
    assert state.action() # must invoke default action here
    yield (state.action()[1], last_position, len(string))

  def __visit_all_edges(self, visitor, state):
    edge = set([self.__start])
    next_edge = lambda node: set(node.transitions().values())
    return self.visit_edges(edge, next_edge, visitor, state)

  def to_dot(self):
    iterator = lambda visitor, state: self.__visit_all_edges(visitor, state)
    state_iterator = lambda x : [x]
    return self.generate_dot(self.__start, self.__terminal_set, iterator, state_iterator)
