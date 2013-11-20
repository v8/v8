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

from transition_keys import TransitionKey
from automaton import *

class NfaState(AutomatonState):

  def __init__(self):
    super(NfaState, self).__init__()
    self.__transitions = {}
    self.__unclosed = set()
    self.__epsilon_closure = None
    self.__action = None

  def transitions_to_multiple_states(self):
    return True

  def epsilon_closure_iter(self):
    return iter(self.__epsilon_closure)

  def set_epsilon_closure(self, closure):
    assert self.is_closed()
    assert self.__epsilon_closure == None
    self.__epsilon_closure = frozenset(closure)

  def action(self):
    assert self.is_closed()
    return self.__action

  def set_action(self, action):
    assert not self.is_closed()
    assert not self.__action
    self.__action = action

  def transitions(self):
    assert self.is_closed()
    return self.__transitions

  def __add_transition(self, key, next_state):
    if next_state == None:
      assert not self.is_closed(), "already closed"
      self.__unclosed.add(key)
      return
    if not key in self.__transitions:
      self.__transitions[key] = set()
    self.__transitions[key].add(next_state)

  def add_unclosed_transition(self, key):
    assert key != TransitionKey.epsilon()
    self.__add_transition(key, None)

  def add_epsilon_transition(self, state):
    assert state != None
    self.__add_transition(TransitionKey.epsilon(), state)

  def is_closed(self):
    return self.__unclosed == None

  def close(self, end):
    assert not self.is_closed()
    unclosed, self.__unclosed = self.__unclosed, None
    if end == None:
      assert not unclosed
      return
    for key in unclosed:
      self.__add_transition(key, end)
    if not unclosed:
      self.__add_transition(TransitionKey.epsilon(), end)

  def __matches(self, match_func, value):
    # f collects states whose corresponding TransitionKey matches 'value'.
    items = self.__transitions.items()
    iters = [iter(states) for (key, states) in items if match_func(key, value)]
    return chain(*iters)

  def transition_state_iter_for_char(self, value):
    return self.__matches(lambda k, v : k.matches_char(v), value)

  def transition_state_iter_for_key(self, value):
    return self.__matches(lambda k, v : k.is_superset_of_key(v), value)

class Nfa(Automaton):

  def __init__(self, start, end, nodes_created):
    super(Nfa, self).__init__()
    self.__start = start
    self.__end = end
    self.__verify(nodes_created)

  def start_state(self):
    return self.__start

  def terminal_set(self):
    return set([self.__end])

  def __verify(self, nodes_created):
    def f(node, count):
      assert node.is_closed()
      return count + 1
    count = self.visit_all_states(f, 0)
    assert count == nodes_created

  @staticmethod
  def __gather_transition_keys(state_set):
    keys = set(chain(*map(lambda state: state.key_iter(), state_set)))
    keys.discard(TransitionKey.epsilon())
    return TransitionKey.disjoint_keys(keys)

  @staticmethod
  def __to_dfa(nfa_state_set, dfa_nodes, end_node):
    nfa_state_set = Automaton.epsilon_closure(nfa_state_set)
    assert nfa_state_set
    name = ".".join(str(x.node_number()) for x in sorted(nfa_state_set))
    if name in dfa_nodes:
      return name
    dfa_nodes[name] = {
      'transitions': {},
      'terminal': end_node in nfa_state_set,
      'action' : Action.dominant_action(nfa_state_set)}
    for key in Nfa.__gather_transition_keys(nfa_state_set):
      match_states = set()
      f = lambda state: state.transition_state_iter_for_key(key)
      match_states |= set(chain(*map(f, nfa_state_set)))
      transition_state = Nfa.__to_dfa(match_states, dfa_nodes, end_node)
      dfa_nodes[name]['transitions'][key] = transition_state
    return name

  def compute_dfa(self):
    dfa_nodes = {}
    start_name = self.__to_dfa(set([self.__start]), dfa_nodes, self.__end)
    return (start_name, dfa_nodes)
