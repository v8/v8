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
from automaton import Action
from dfa import Dfa

class DfaOptimizer(object):

  def __init__(self, dfa, log):
    self.__dfa = dfa
    self.__log = log

  @staticmethod
  def transistions_match(encoding, incoming_key, incoming_state, state):
    keys = set(state.key_iter())
    keys.add(incoming_key)
    disjoint_keys = TransitionKey.disjoint_keys(encoding, keys)
    for key in disjoint_keys:
      if not incoming_key.is_superset_of_key(key):
        continue
      transition_state = state.transition_state_for_key(key)
      if incoming_state.transition_state_for_key(key) != transition_state:
        return False
    return True

  def replace_tokens_with_gotos(self):
    '''replace states with no entry action and match action of type 'token(X)'
    with new states with entry action store_token(X) and match action
    goto(state_id) which has (far) fewer transitions'''

    dfa = self.__dfa
    encoding = dfa.encoding()

    incoming_transitions = {}
    def build_incoming_transitions(state, visitor_state):
      for key, transition_state in state.key_state_iter():
        if not transition_state in incoming_transitions:
          incoming_transitions[transition_state] = []
        incoming_transitions[transition_state].append((key,state))
    dfa.visit_all_states(build_incoming_transitions)

    def is_replacement_candidate(state):
      action = state.action()
      if not action or action.entry_action() or not action.match_action():
        return False
      if (action.match_action()[0] == 'token' or
          action.match_action()[0] == 'harmony_token'):
        return True
      return False

    replacements = {}
    for state, incoming in incoming_transitions.items():
      if len(incoming) < 10:
        continue
      if not is_replacement_candidate(state):
        continue
      # check to see if incoming edges are matched by an outgoing edge
      def match_filter((incoming_key, incoming_state)):
        return (incoming_state != state and  # drop self transitions
                is_replacement_candidate(incoming_state) and
                self.transistions_match(
                  encoding, incoming_key, incoming_state, state))
      for incoming_key_state in incoming:
        if not match_filter(incoming_key_state):
          continue
        (incoming_key, incoming_state) = incoming_key_state
        # set this transition as to be replaced
        if not incoming_state in replacements:
          replacements[incoming_state] = {}
        replacements[incoming_state][incoming_key] = state
        # now see if we can gather other transistions into the goto
        for key in incoming_state.key_iter():
          if key == incoming_key:
            continue
          if not self.transistions_match(
              encoding, key, incoming_state, state):
            continue
          # this transition can be removed
          replacements[incoming_state][key] = None
    if not replacements:
      return
    # perform replacement
    states = {}
    name = lambda state : str(state.node_number())
    counters = {
      'removals' : 0,
      'goto_start' : 0,
      'store_token_and_goto' : 0,
      'store_harmony_token_and_goto' : 0,
      }
    store_states = set([])
    # generate a store action to replace an existing action
    def replacement_action(old_action, transition_state):
      assert not old_action.entry_action()
      assert old_action.match_action()
      state_id = name(transition_state)
      if old_action.match_action()[0] == 'token':
        old_token = old_action.match_action()[1]
        if (transition_state.action().match_action()[0] == 'token' and
            transition_state.action().match_action()[1] == old_token):
          # no need to store token
          match_action = ('goto_start', (state_id,))
          counters['goto_start'] += 1
        else:
          counters['store_token_and_goto'] += 1
          match_action = ('store_token_and_goto', (old_token, state_id))
      elif old_action.match_action()[0] == 'harmony_token':
        match_action = ('store_harmony_token_and_goto',
                        (old_action.match_action()[1][0],
                         old_action.match_action()[1][1],
                         old_action.match_action()[1][2],
                         state_id))
        counters['store_harmony_token_and_goto'] += 1
      else:
        raise Exception(old_action.match_action())
      return Action(None, match_action, old_action.precedence())
    # map the old state to the new state, with fewer transitions and
    # goto actions
    def replace_state(state, acc):
      new_state = {
        'transitions' : {},
        'terminal' : state in self.__dfa.terminal_set(),
        'action' : state.action(),
      }
      states[name(state)] = new_state
      state_replacements = replacements[state] if state in replacements else {}
      for transition_key, transition_state in state.transitions().items():
        if not transition_key in state_replacements:
          new_state['transitions'][transition_key] = name(transition_state)
          continue
        replacement = state_replacements[transition_key]
        del state_replacements[transition_key]
        # just drop the transition
        if replacement == None:
          counters['removals'] += 1
          continue
        assert replacement == transition_state
        # do goto replacement
        new_state['action'] = replacement_action(state.action(), replacement)
        # will need to patch up transition_state
        store_states.add(name(transition_state))
      assert not state_replacements
    self.__dfa.visit_all_states(replace_state)
    # now patch up all states with stores
    for state_id in store_states:
      old_action = states[state_id]['action']
      assert not old_action.entry_action()
      assert old_action.match_action()[0] == 'token', 'unimplemented'
      entry_action = ('store_token', old_action.match_action()[1])
      match_action = ('do_stored_token', state_id)
      precedence = old_action.precedence()
      states[state_id]['action'] = Action(
        entry_action, match_action, precedence)
    start_name = name(self.__dfa.start_state())
    if self.__log:
      print 'goto_start inserted %s' % counters['goto_start']
      print 'store_token_and_goto inserted %s' % (
        counters['store_token_and_goto'])
      print 'store_harmony_token_and_goto %s' % (
        counters['store_harmony_token_and_goto'])
      print 'transitions removed %s' % counters['removals']
      print 'do_stored_token actions added %s' % len(store_states)
    self.__dfa = Dfa(self.__dfa.encoding(), start_name, states)

  @staticmethod
  def optimize(dfa, log):
    optimizer = DfaOptimizer(dfa, log)
    optimizer.replace_tokens_with_gotos()
    return optimizer.__dfa
