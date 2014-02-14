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
    self.__action = Action.empty_action()

  def action(self):
    assert self.__is_closed()
    return self.__action

  def set_action(self, action):
    assert not self.__is_closed()
    assert not self.__action
    assert isinstance(action, Action)
    self.__action = action

  def __add_transition(self, key, next_state):
    assert key != None
    if next_state == None:
      assert not self.__is_closed(), "already closed"
      self.__unclosed.add(key)
      return
    if not key in self.__transitions:
      self.__transitions[key] = set()
    else:
      assert key == TransitionKey.epsilon()
    self.__transitions[key].add(next_state)

  def add_unclosed_transition(self, key):
    assert key != TransitionKey.epsilon()
    self.__add_transition(key, None)

  def add_transition(self, key, state):
    assert not self.__is_closed(), "already closed"
    assert state != None
    self.__add_transition(key, state)

  def add_epsilon_transition(self, state):
    self.add_transition(TransitionKey.epsilon(), state)

  def __is_closed(self):
    return self.__unclosed == None

  def close(self, end):
    assert not self.__is_closed()
    unclosed, self.__unclosed = self.__unclosed, None
    if end == None:
      assert not unclosed
      return
    for key in unclosed:
      self.__add_transition(key, end)
    if not unclosed:
      self.__add_transition(TransitionKey.epsilon(), end)

  def post_creation_verify(self):
    assert self.__is_closed()
    assert self.__epsilon_closure != None

  def state_iter_for_key(self, key):
    assert self.__is_closed()
    if not key in self.__transitions:
      return iter([])
    return iter(self.__transitions[key])

  def key_state_iter(
    self,
    key_filter = lambda x: True,
    state_filter = lambda x: True,
    match_func = lambda x, y: True,
    yield_func = lambda x, y: (x, y)):
    assert self.__is_closed()
    for key, states in self.__transitions.items():
      if key_filter(key):
        for state in states:
          if state_filter(state) and match_func(key, state):
            yield yield_func(key, state)

  def epsilon_closure_iter(self):
    return iter(self.__epsilon_closure)

  def set_epsilon_closure(self, closure):
    assert self.__is_closed()
    assert self.__epsilon_closure == None
    self.__epsilon_closure = frozenset(closure)

  def swap_key(self, old_key, new_key):
    'this is one of the few mutations allowed after closing'
    self.post_creation_verify()
    assert not old_key == TransitionKey.epsilon(), "changes epsilon closure"
    assert not new_key == TransitionKey.epsilon(), "changes epsilon closure"
    value = self.__transitions[old_key]
    del self.__transitions[old_key]
    self.__transitions[new_key] = value

class Nfa(Automaton):

  def __init__(self, encoding, start, end, nodes_created):
    super(Nfa, self).__init__(encoding)
    self.__start = start
    self.__end = end
    self.__verify(nodes_created)

  def start_state(self):
    return self.__start

  def terminal_set(self):
    return set([self.__end])

  def __verify(self, nodes_created):
    def f(node, count):
      node.post_creation_verify()
      return count + 1
    count = self.visit_all_states(f, 0)
    assert count == nodes_created

  @staticmethod
  def __gather_transition_keys(encoding, state_set):
    keys = set(chain(*map(lambda state: state.key_iter(), state_set)))
    keys.discard(TransitionKey.epsilon())
    return TransitionKey.disjoint_keys(encoding, keys)

  def __to_dfa(self, nfa_state_set, dfa_nodes):
    nfa_state_set = Automaton.epsilon_closure(nfa_state_set)
    # nfa_state_set will be a state in the dfa.
    assert nfa_state_set
    name = ".".join(str(x.node_number()) for x in sorted(nfa_state_set))
    if name in dfa_nodes:
      return name
    dfa_nodes[name] = {
      'transitions': {},
      'terminal': self.__end in nfa_state_set,
      'action' : self.dominant_action(nfa_state_set)}
    # Gather the set of transition keys for which the dfa state will have
    # transitions (the disjoint set of all the transition keys from all the
    # states combined). For example, if a state in the state set has a
    # transition for key [a-c], and another state for [b-d], the dfa state will
    # have transitions with keys ([a], [b-c], [d]).
    for key in Nfa.__gather_transition_keys(self.encoding(), nfa_state_set):
      # Find out which states we can reach with "key", starting from any of the
      # states in "nfa_state_set". The corresponding dfa state will have a
      # transition with "key" to a state which corresponds to the set of the
      # states ("match_states") (more accurately, its epsilon closure).
      f = lambda state: state.transition_state_iter_for_key(key)
      match_states = set(chain(*map(f, nfa_state_set)))
      transition_state = self.__to_dfa(match_states, dfa_nodes)
      dfa_nodes[name]['transitions'][key] = transition_state
    return name

  def compute_dfa(self):
    dfa_nodes = {}
    start_name = self.__to_dfa(set([self.__start]), dfa_nodes)
    return (start_name, dfa_nodes)
