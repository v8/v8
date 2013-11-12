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

  def __init__(self, node_number):
    super(NfaState, self).__init__(node_number)
    self.__transitions = {}
    self.__unclosed = set()
    self.__epsilon_closure = None
    self.__action = None

  def epsilon_closure(self):
    return self.__epsilon_closure

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

  def next_states(self, key_filter):
    assert self.is_closed()
    f = lambda acc, (k, v) : acc if not key_filter(k) else acc | set(v)
    return reduce(f, self.__transitions.items(), set())

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
    f = lambda acc, (k, vs): acc | vs if match_func(k, value) else acc
    return reduce(f, self.__transitions.items(), set())

  def char_matches(self, value):
    return self.__matches(lambda k, v : k.matches_char(v), value)

  def key_matches(self, value):
    return self.__matches(lambda k, v : k.matches_key(v), value)

  def replace_catch_all(self):
    catch_all = TransitionKey.unique('catch_all')
    if not catch_all in self.__transitions:
      return
    f = lambda acc, state: acc | state.__epsilon_closure
    reachable_states = reduce(f, self.__transitions[catch_all], set())
    f = lambda acc, state: acc | set(state.__transitions.keys())
    keys = reduce(f, reachable_states, set())
    keys.discard(TransitionKey.epsilon())
    keys.discard(catch_all)
    inverse_key = TransitionKey.inverse_key(keys)
    if inverse_key:
      self.__transitions[inverse_key] = self.__transitions[catch_all]
    del self.__transitions[catch_all]

  @staticmethod
  def gather_transition_keys(state_set):
    f = lambda acc, state: acc | set(state.__transitions.keys())
    keys = reduce(f, state_set, set())
    keys.discard(TransitionKey.epsilon())
    return TransitionKey.disjoint_keys(keys)

class Nfa(Automaton):

  def __init__(self, start, end, nodes_created):
    super(Nfa, self).__init__()
    self.__start = start
    self.__end = end
    self.__epsilon_closure_computed = False
    self.__verify(nodes_created)

  def __visit_all_edges(self, visitor, state):
    edge = set([self.__start])
    next_edge = lambda node: node.next_states(lambda x : True)
    return self.visit_edges(edge, next_edge, visitor, state)

  def __verify(self, nodes_created):
    def f(node, node_list):
      assert node.is_closed()
      node_list.append(node)
      return node_list
    node_list = self.__visit_all_edges(f, [])
    assert len(node_list) == nodes_created

  def __compute_epsilon_closures(self):
    if self.__epsilon_closure_computed:
      return
    self.__epsilon_closure_computed = True
    def outer(node, state):
      def inner(node, closure):
        closure.add(node)
        return closure
      is_epsilon = lambda k: k == TransitionKey.epsilon()
      next_edge = lambda node : node.next_states(is_epsilon)
      edge = next_edge(node)
      closure = self.visit_edges(edge, next_edge, inner, set())
      node.set_epsilon_closure(closure)
    self.__visit_all_edges(outer, None)

  @staticmethod
  def __close(states):
    f = lambda acc, node: acc | node.epsilon_closure()
    return reduce(f, states, set(states))

  def matches(self, string):
    self.__compute_epsilon_closures()
    valid_states = Nfa.__close(set([self.__start]))
    for c in string:
      f = lambda acc, state: acc | state.char_matches(c)
      transitions = reduce(f, valid_states, set())
      if not transitions:
        return False
      valid_states = Nfa.__close(transitions)
    return self.__end in valid_states

  @staticmethod
  def __to_dfa(nfa_state_set, dfa_nodes, end_node):
    nfa_state_set = Nfa.__close(nfa_state_set)
    assert nfa_state_set
    name = ".".join(str(x.node_number()) for x in sorted(nfa_state_set))
    if name in dfa_nodes:
      return name
    def gather_actions(states):
      actions = set()
      for state in states:
        if state.action():
          actions.add(state.action())
      actions = list(actions)
      actions.sort()
      return actions
    dfa_nodes[name] = {
      'transitions': {},
      'terminal': end_node in nfa_state_set,
      'actions' : gather_actions(nfa_state_set)}
    for key in NfaState.gather_transition_keys(nfa_state_set):
      match_states = set()
      f = lambda acc, state: acc | state.key_matches(key)
      for state in reduce(f, nfa_state_set, set()):
        match_states.add(state)
      transition_state = Nfa.__to_dfa(match_states, dfa_nodes, end_node)
      dfa_nodes[name]['transitions'][key] = transition_state
    return name

  def compute_dfa(self):
    self.__compute_epsilon_closures()
    self.__visit_all_edges(lambda node, state: node.replace_catch_all(), None)
    dfa_nodes = {}
    start_name = self.__to_dfa(set([self.__start]), dfa_nodes, self.__end)
    return (start_name, dfa_nodes)

  def to_dot(self):
    iterator = lambda visitor, state: self.__visit_all_edges(visitor, state)
    state_iterator = lambda x : x
    return self.generate_dot(self.__start, set([self.__end]), iterator, state_iterator)
