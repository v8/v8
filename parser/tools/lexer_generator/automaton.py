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

from types import IntType, TupleType, ListType
from itertools import chain
from term import Term
from transition_key import TransitionKey

class Action(object):

  __empty_action = None

  @staticmethod
  def empty_action():
    if Action.__empty_action == None:
      Action.__empty_action = Action(Term.empty_term(), -1)
    return Action.__empty_action

  @staticmethod
  def dominant_action(actions):
    dominant = Action.empty_action()
    for action in actions:
      if not action:
        continue
      if not dominant:
        dominant = action
        continue
      if action.precedence() == dominant.precedence():
        assert action.__term == dominant.__term
      elif action.precedence() < dominant.precedence():
        dominant = action
    return dominant

  def __init__(self, term, precedence):
    assert isinstance(term, Term)
    assert type(precedence) == IntType
    assert not term or precedence >= 0, 'action must have positive precedence'
    self.__term = term
    self.__precedence = precedence

  def name(self):
    return self.__term.name()

  def term(self):
    return self.__term

  def precedence(self):
    return self.__precedence

  def __nonzero__(self):
    'true <==> self == empty_action'
    return bool(self.__term)

  def __eq__(self, other):
    return isinstance(other, self.__class__) and self.__term == other.__term

  def __str__(self):
    return str(self.__term)

class AutomatonState(object):
  '''A base class for dfa and nfa states.  Immutable'''

  __node_number_counter = 0

  def __init__(self):
    self.__node_number = AutomatonState.__node_number_counter
    AutomatonState.__node_number_counter += 1

  def __hash__(self):
    return hash(self.__node_number)

  def __eq__(self, other):
    return (isinstance(other, self.__class__) and
            self.__node_number == other.__node_number)

  def node_number(self):
    return self.__node_number

  def __str__(self):
    return "%s(%d)" % (type(self), self.node_number())

  __pass = lambda x : True

  def key_iter(self, key_filter = __pass):
    return self.key_state_iter(
      key_filter = key_filter, yield_func = lambda x, y: x)

  def state_iter(self, key_filter = __pass, state_filter = __pass):
    return self.key_state_iter(
      key_filter = key_filter, state_filter = state_filter,
      yield_func = lambda x, y: y)

  def transition_state_iter_for_char(self, value):
    return self.key_state_iter(
      match_func = lambda k, v : k.matches_char(value),
      yield_func = lambda x, y: y)

  def transition_state_iter_for_key(self, value):
    return self.key_state_iter(
      match_func = lambda k, v : k.is_superset_of_key(value),
      yield_func = lambda x, y: y)

class Automaton(object):

  def __init__(self, encoding):
    self.__encoding = encoding

  def encoding(self):
    return self.__encoding

  @staticmethod
  def visit_states(edge, visitor, visit_state = None, state_iter = None):
    if not state_iter:
      state_iter  = lambda node: node.state_iter()
    visited = set()
    while edge:
      next_edge_iters = []
      def f(visit_state, node):
        next_edge_iters.append(state_iter(node))
        return visitor(node, visit_state)
      visit_state = reduce(f, edge, visit_state)
      next_edge = set(chain(*next_edge_iters))
      visited |= edge
      edge = next_edge - visited
    return visit_state

  def start_set(self):
    return set([self.start_state()])

  def visit_all_states(self, visitor, visit_state = None, state_iter = None):
    return self.visit_states(self.start_set(), visitor, visit_state, state_iter)

  @staticmethod
  def epsilon_closure(states):
    f = lambda state : state.epsilon_closure_iter()
    return set(chain(iter(states), *map(f, states)))

  @staticmethod
  def __omega_closure(states):
    f = lambda s : s.transition_state_iter_for_key(TransitionKey.omega())
    new_states = set(chain(*map(f, states)))
    return set(chain(iter(states), iter(Automaton.epsilon_closure(new_states))))

  @staticmethod
  def __transition_states_for_char(states, c):
    f = lambda s : s.transition_state_iter_for_char(c)
    states = set(chain(*map(f, Automaton.epsilon_closure(states))))
    return Automaton.__omega_closure(states)

  def matches(self, string):
    valid_states = self.start_set()
    for c in string:
      valid_states = Automaton.__transition_states_for_char(valid_states, c)
      if not valid_states:
        return False
    valid_states = self.__omega_closure(self.epsilon_closure(valid_states))
    return len(self.terminal_set().intersection(valid_states)) > 0

  @staticmethod
  def dominant_action(states):
    return Action.dominant_action(map(lambda s: s.action(), states))

  def lex(self, string, default_action):
    last_position = 0
    valid_states = self.start_set()
    for pos, c in enumerate(string):
      transitions = Automaton.__transition_states_for_char(valid_states, c)
      if transitions:
        valid_states = transitions
        continue
      # TODO(dcarney): action collection should walk omega transitions
      action = self.dominant_action(valid_states)
      if not action:
        assert default_action
        action = default_action
      yield (action, last_position, pos)
      last_position = pos
      # lex next token
      valid_states = Automaton.__transition_states_for_char(self.start_set(), c)
      assert valid_states
    valid_states = self.__omega_closure(self.epsilon_closure(valid_states))
    action = self.dominant_action(valid_states)
    if not action:
      assert default_action
      action = default_action
    yield (action, last_position, len(string))
