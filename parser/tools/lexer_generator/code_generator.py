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

import os
import sys
import jinja2
from dfa import Dfa
from transition_keys import TransitionKey

class CodeGenerator:

  def __init__(self,
               rule_processor,
               minimize_default = True,
               inline = True,
               debug_print = False,
               log = False):
    if minimize_default:
      dfa = rule_processor.default_automata().minimal_dfa()
    else:
      dfa = rule_processor.default_automata().dfa()
    self.__dfa = dfa
    self.__default_action = rule_processor.default_action
    self.__debug_print = debug_print
    self.__start_node_number = self.__dfa.start_state().node_number()
    self.__log = log
    self.__inline = inline

  def __state_cmp(self, left, right):
    if left['original_node_number'] == self.__start_node_number:
      return -1
    if right['original_node_number'] == self.__start_node_number:
      return 1
    c = cmp(len(left['disjoint_keys']), len(right['disjoint_keys']))
    if c:
      return c
    c = cmp(left['disjoint_keys'], right['disjoint_keys'])
    if c:
      return c
    c = cmp(len(left['transitions']), len(right['transitions']))
    if c:
      return c
    c = cmp(left['depth'], right['depth'])
    if c:
      return c
    left_precendence = left['action'].precedence() if left['action'] else -1
    right_precendence = right['action'].precedence() if right['action'] else -1
    c = cmp(left_precendence, right_precendence)
    if c:
      return c
    # if left['original_node_number'] != right['original_node_number']:
    #   # TODO fix
    #   print "noncanonical node ordering %d %d" % (left['original_node_number'],
    #                                               right['original_node_number'])
    return cmp(left['original_node_number'], right['original_node_number'])

  @staticmethod
  def __range_cmp(left, right):
    if left[0] == 'LATIN_1':
      if right[0] == 'LATIN_1':
        return cmp(left[1], right[1])
      assert right[0] == 'CLASS'
      return -1
    assert left[0] == 'CLASS'
    if right[0] == 'LATIN_1':
      return 1
    # TODO store numeric values and cmp
    return cmp(left[1], right[1])

  @staticmethod
  def __transform_state(state):
    # action data
    action = state.action()
    entry_action = None if not action else action.entry_action()
    match_action = None if not action else action.match_action()
    # compute disjoint ranges
    keys = TransitionKey.disjoint_keys(list(state.key_iter()))
    def f(key):
      ranges = list(key.range_iter())
      assert len(ranges) == 1
      return ranges[0]
    keys = sorted(map(f, keys), CodeGenerator.__range_cmp)
    # generate ordered transitions
    transitions = map(lambda (k, v) : (k, v.node_number()),
                      state.transitions().items())
    def cmp(left, right):
      return TransitionKey.canonical_compare(left[0], right[0])
    transitions = sorted(transitions, cmp)
    # dictionary object representing state
    return {
      'node_number' : state.node_number(),
      'original_node_number' : state.node_number(),
      'transitions' : transitions,
      'disjoint_keys' : keys,
      'inline' : None,
      'depth' : None,
      'action' : action,
      'entry_action' : entry_action,
      'match_action' : match_action,
    }

  @staticmethod
  def __compute_depths(node_number, depth, id_map):
    state = id_map[node_number]
    if state['depth'] != None:
      return
    state['depth'] = depth
    for (k, transition_node) in state['transitions']:
      CodeGenerator.__compute_depths(transition_node, depth + 1, id_map)

  @staticmethod
  def __set_inline(count, state):
    assert state['inline'] == None
    inline = False
    if not state['transitions']:
      inline = True
    state['inline'] = inline
    return count + 1 if inline else count

  def __canonicalize_traversal(self):
    dfa_states = []
    self.__dfa.visit_all_states(lambda state, acc: dfa_states.append(state))
    dfa_states = map(CodeGenerator.__transform_state, dfa_states)
    id_map = {x['original_node_number'] : x for x in dfa_states}
    CodeGenerator.__compute_depths(self.__start_node_number, 1, id_map)
    dfa_states = sorted(dfa_states, cmp=self.__state_cmp)
    # remap all node numbers
    for i, state in enumerate(dfa_states):
      state['node_number'] = i
    def f((key, original_node_number)):
      return (key, id_map[original_node_number]['node_number'])
    for state in dfa_states:
      state['transitions'] = map(f, state['transitions'])
    assert id_map[self.__start_node_number]['node_number'] == 0
    assert len(dfa_states) == self.__dfa.node_count()
    # set nodes to inline
    if self.__inline:
      inlined = reduce(CodeGenerator.__set_inline, dfa_states, 0)
      if self.__log:
        print "inlined %s" % inlined
    elif self.__log:
      print "no inlining"
    self.__dfa_states = dfa_states

  def process(self):

    self.__canonicalize_traversal()

    dfa_states = self.__dfa_states

    default_action = self.__default_action
    assert(default_action and default_action.match_action())
    default_action = default_action.match_action()

    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    template_env = jinja2.Environment(
      loader = jinja2.PackageLoader('lexer_generator', '.'),
      undefined = jinja2.StrictUndefined)
    template = template_env.get_template('code_generator.jinja')

    return template.render(
      start_node_number = 0,
      debug_print = self.__debug_print,
      default_action = default_action,
      dfa_states = dfa_states)
