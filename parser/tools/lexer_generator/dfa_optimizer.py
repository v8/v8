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

# --- Optimization: Replacing tokens with gotos ---
#
# This optimization reduces the code size for grammars which have constructs
# similar to keywords and identifiers. Consider the following grammar:
#
# "baz" <|token(BAZ)|>
# "bazz" <|token(BAZZ)|>
#
# /[a-z]/ <|token(IDENTIFIER)|Identifier>
#
# <<Identifier>>
# /[a-z]/ <|token(IDENTIFIER)|continue>
#
# In the corresponding dfa, we have a set of nodes for recognizing the keywords,
# and one node for the "Identifier" state. From every node in the keyword part
# of the dfa, there is an edge to the Identifier state with the letters which
# cannot be used for advancing in the keyword part (e.g., we have seen "baz" and
# the next character is "s", so that's an identifier and not the keyword
# "baz" or "bazz").
#
# [ ] ---b---> [ ] ---a---> [ ] ---z---> [BAZ] ---z---> [BAZZ]
#               |            |             |            |
#             [b-z]        [a-y]         [a-y]        [a-z]
#               |            |             |            |
#               \---------------------------------------/
#                                  |
#                                  v
#                                 [ID] ----------\
#                                  ^            [a-z]
#                                  --------------/
#
# If we generate code from the dfa, these edges contribute a lot to the code
# size. To reduce the code size, we do the following transformation:
#
# For each state which is an end of a keyword, we, add a match action "store
# token and goto". The match action will be executed only if we cannot advance
# in the graph with the next character. For example, if we have seen "baz", we
# first check if the next character is "z" to get "bazz", and if the next
# character is not "z", we execute the match action.
#
# When we execute the "store token and goto" action, we record that we have seen
# the corresponding keyword (the token might still be an identifier, depending
# on what the next character is). Then we jump to the Identifier state of the
# graph. The Identifier state has an entry action "store_token(IDENTIFIER)" for
# [a-z]. When it is executed, it overwrites the stored token with IDENTIFIER.
#
# Example: if we have seen "baz" and the next character is not "z", we store the
# token BAZ and jump to the Identifier state. If the next character is "a", we
# are sure the token is not the keyword "baz", and overwrite the stored token
# with IDENTIFIER.
#
# The Identifier state has a match action "do stored token", which returns the
# stored token to the upper layer.
#
# Example: if we have seen "baz" and the next character is not "z", we store the
# token BAZ and jump to the Identifier state. If the next character is a space,
# we cannot advance in the dfa, and the match action is executed. The match
# action returns the stored token BAZ to the upper layer.
#
# For each state which is an intermediate state in a keyword, we add the same
# "goto", but we don't need to store a token.
#
# [ ] ---b---> [ ] ---a---> [ ] ---z---> [BAZ] ---z---> [BAZZ]
#               |            |             |            |
#            goto         goto     store BAZ, goto  store BAZZ, goto
#               |            |             |            |
#               \---------------------------------------/
#                                  |
#                                  v
#                                 [ID] ----------\
#                                  ^           [a-z], store IDENTIFIER
#                                  --------------/
#
# (Note: this graph illustrates the logic, but is not accurate wrt the entry
# actions and match actions of the states.)
#
# The code size decreases, because we remove the checks which correspond to
# transitions from keyword states to the identifier state ([b-z], [a-y] etc.),
# and replace them with a more general check ([a-z]) in one place. This works
# because the more specialized checks (e.g., checking for "z" when we have seen
# "baz") are done before we "goto" to the state which has the generic check.
#
# There is one complication though: When we consider adding a goto from a state
# s1 to state s2, we need to check all possible transitions from s1 and from s2,
# and see if they match. We cannot jump to a state which has different
# transitions than the state where we're jumping from.
#
# For example, the following partial dfa allows distinguishing identifiers which
# have only lower case letters from identifiers which have at least one upper
# case letter.
#
# [s1] ---a---> [ ]
#  |
#  |            /---[a-z]--\
#  |            v          |
#  ---[b-z]--> [s2] ------/
#  |             |
#  |           [A-Z]
#  |             |
#  |             v
#  \--[A-Z]---> [s3]
#
# We can add a goto from s1 to s2 (after checking for "a"), because the
# transitions match: with [b-z], both states transition to s2, and with [A-Z],
# both states transition to s3. Note that [a-z] is a superkey of [b-z], and the
# transition from s2 is defined for the superkey, but that is ok.

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
      if not action or not action.match_action():
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
      return Action(old_action.entry_action(), match_action,
                    old_action.precedence())
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
    start_name = name(self.__dfa.start_state())
    for state_id in store_states:
      old_action = states[state_id]['action']
      assert not old_action.entry_action()
      assert old_action.match_action()[0] == 'token', 'unimplemented'
      entry_action = ('store_token', old_action.match_action()[1])
      match_action = ('do_stored_token', state_id)
      precedence = old_action.precedence()
      states[state_id]['action'] = Action(
        entry_action, match_action, precedence)
      # The state might be only reachable via gotos; make sure it's connected in
      # the state graph by adding a bogus transition from the start state. This
      # transition doens't match any character.
      states[start_name]['transitions'][
          TransitionKey.unique('no_match')] = state_id

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
