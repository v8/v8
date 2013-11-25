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
               switching = True,
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
    self.__switching = switching

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
    if left[0] == 'PRIMARY_RANGE':
      if right[0] == 'PRIMARY_RANGE':
        return cmp(left[1], right[1])
      assert right[0] == 'CLASS'
      return -1
    assert left[0] == 'CLASS'
    if right[0] == 'PRIMARY_RANGE':
      return 1
    # TODO store numeric values and cmp
    return cmp(left[1], right[1])

  @staticmethod
  def __transform_state(encoding, state):
    # action data
    action = state.action()
    entry_action = None if not action else action.entry_action()
    match_action = None if not action else action.match_action()
    # generate ordered transitions
    transitions = map(lambda (k, v) : (k, v.node_number()),
                      state.transitions().items())
    def cmp(left, right):
      return TransitionKey.canonical_compare(left[0], right[0])
    transitions = sorted(transitions, cmp)
    # map transition keys to disjoint ranges
    disjoint_keys = {'value' : []}
    def f((key, state)):
      ranges = list(key.range_iter(encoding))
      disjoint_keys['value'] += ranges
      return (ranges, state)
    transitions = map(f, transitions)
    disjoint_keys = sorted(disjoint_keys['value'], CodeGenerator.__range_cmp)
    # dictionary object representing state
    (class_keys, distinct_keys, ranges) = (0, 0, 0)
    for (t, r) in disjoint_keys:
      if t == 'CLASS':
        class_keys += 1
      elif t == 'PRIMARY_RANGE':
        distinct_keys += r[1] - r[0] + 1
        ranges += 1
      else:
        raise Exception()
    return {
      'node_number' : state.node_number(),
      'original_node_number' : state.node_number(),
      'transitions' : transitions,
      'switch_transitions' : [],
      'deferred_transitions' : [],
      'long_char_transitions' : [],
      'disjoint_keys' : disjoint_keys,
      'has_goto_after_entry' : False,
      'inline' : None,
      'depth' : None,
      'action' : action,
      'entry_action' : entry_action,
      'match_action' : match_action,
      'class_keys' : class_keys,
      'distinct_keys' : distinct_keys,
      'ranges' : ranges
    }

  @staticmethod
  def __compute_depths(node_number, depth, id_map):
    state = id_map[node_number]
    if state['depth'] != None:
      return
    state['depth'] = depth
    for (k, transition_node) in state['transitions']:
      CodeGenerator.__compute_depths(transition_node, depth + 1, id_map)

  def __set_inline(self, count, state):
    assert state['inline'] == None
    inline = False
    # inline terminal states
    if not state['transitions']:
      inline = True
    # inline next to terminal states with 1 or 2 transitions
    elif state['distinct_keys'] < 3 and state['class_keys'] == 0:
      inline = True
      # ensure state terminates in 1 step
      for key, state_id in state['transitions']:
        if self.__dfa_states[state_id]['transitions']:
          inline = False
          break
    state['inline'] = inline
    return count + 1 if inline else count

  def __split_transitions(self, split_count, state):
    '''Goes through the transitions for 'state' and decides which of them should
    use the if statement and which should use the switch statement.'''
    assert not state['switch_transitions']
    (distinct_keys, ranges) = (state['distinct_keys'], state['ranges'])
    no_switch = distinct_keys <= 7 or float(distinct_keys)/float(ranges) >= 7.0
    if_transitions = []
    switch_transitions = []
    deferred_transitions = []
    for (ranges, node_id) in state['transitions']:
      i = []
      s = []
      d = []
      for r in ranges:
        # all class checks will be deferred to after all other checks
        if r[0] == 'CLASS':
          d.append(r)
        elif no_switch:
          i.append(r)
        else:
          s.append(r[1])
      if i:
        if_transitions.append((i, node_id))
      if s:
        switch_transitions.append((s, node_id))
      if d:
        deferred_transitions.append((d, node_id))
    state['transitions'] = if_transitions
    state['switch_transitions'] = switch_transitions
    state['deferred_transitions'] = deferred_transitions
    return split_count + (0 if no_switch else 1)

  __call_map = {
    'non_primary_whitespace' : 'IsWhiteSpaceNotLineTerminator',
    'non_primary_letter' : 'IsLetter',
    'non_primary_identifier_part_not_letter' : 'IsIdentifierPartNotLetter',
    'non_primary_line_terminator' : 'IsLineTerminator',
  }

  def __rewrite_deferred_transitions(self, state):
    assert not state['long_char_transitions']
    transitions = state['deferred_transitions']
    if not transitions:
      return
    encoding = self.__dfa.encoding()
    bom = 'byte_order_mark'
    catch_all = 'non_primary_everything_else'
    all_classes = set(encoding.class_name_iter())
    fast_classes = set(['eos', 'zero'])
    call_classes = all_classes - fast_classes - set([bom, catch_all])
    def remap_transition(class_name):
      if class_name in call_classes:
        return ('LONG_CHAR_CLASS', 'call', self.__call_map[class_name])
      if class_name == bom:
        return ('LONG_CHAR_CLASS', class_name)
      raise Exception(class_name)
    fast_transitions = []
    long_class_transitions = []
    long_class_map = {}
    catchall_transition = None
    # loop through and remove catch_all_transitions
    for (classes, transition_node_id) in transitions:
      ft = []
      lct = []
      has_catch_all = False
      for (class_type, class_name) in classes:
        if class_name in fast_classes:
          ft.append((class_type, class_name))
        else:
          assert not class_name in long_class_map
          long_class_map[class_name] = transition_node_id
          if class_name == catch_all:
            assert not has_catch_all
            assert catchall_transition == None
            has_catch_all = True
          else:
            lct.append(remap_transition(class_name))
      if ft:
        fast_transitions.append((ft, transition_node_id))
      if has_catch_all:
        catchall_transition = (lct, transition_node_id)
      elif lct:
        long_class_transitions.append((lct, transition_node_id))
    # all transitions are fast
    if not long_class_map:
      return
    if catchall_transition:
      catchall_transitions = all_classes - fast_classes
      for class_name in long_class_map.iterkeys():
        catchall_transitions.remove(class_name)
      assert not catchall_transitions, "class inversion not unimplemented"
    # split deferred transitions
    state['deferred_transitions'] = fast_transitions
    if catchall_transition:
      catchall_transition = [
        ([('LONG_CHAR_CLASS', 'catch_all')], catchall_transition[1])]
    else:
      catchall_transition = []
    state['long_char_transitions'] = (long_class_transitions +
                                      catchall_transition) # must be last

  def __canonicalize_traversal(self):
    dfa_states = []
    self.__dfa.visit_all_states(lambda state, acc: dfa_states.append(state))
    encoding = self.__dfa.encoding()
    f = lambda state : CodeGenerator.__transform_state(encoding, state)
    dfa_states = map(f, dfa_states)
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
    # store states
    self.__dfa_states = dfa_states
    return id_map

  def __rewrite_gotos(self):
    goto_map = {}
    for state in self.__dfa_states:
      if (state['match_action'] and
          state['match_action'][0] == 'do_stored_token'):
        assert not state['match_action'][1] in goto_map
        goto_map[state['match_action'][1]] = state['node_number']
        state['has_goto_after_entry'] = True
    mapped_actions = set([
      'store_harmony_token_and_goto',
      'store_token_and_goto',
      'goto_start'])
    for state in self.__dfa_states:
      if not state['match_action']:
        continue
      if state['match_action'][0] in mapped_actions:
        value = state['match_action'][1]
        value = tuple(list(value[:-1]) + [goto_map[value[-1]]])
        state['match_action'] = (state['match_action'][0], value)

  def process(self):

    id_map = self.__canonicalize_traversal()
    self.__rewrite_gotos()

    dfa_states = self.__dfa_states
    # set nodes to inline
    if self.__inline:
      inlined = reduce(self.__set_inline, dfa_states, 0)
      if self.__log:
        print "%s states inlined" % inlined
    elif self.__log:
      print "no inlining"
    # split transitions
    switched = reduce(self.__split_transitions, dfa_states, 0)
    if self.__log:
      print "%s states use switch (instead of if)" % switched
    # rewrite deferred transitions
    for state in dfa_states:
      self.__rewrite_deferred_transitions(state)

    default_action = self.__default_action
    assert(default_action and default_action.match_action())
    default_action = default_action.match_action()

    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    template_env = jinja2.Environment(
      loader = jinja2.PackageLoader('lexer_generator', '.'),
      undefined = jinja2.StrictUndefined)
    template = template_env.get_template('code_generator.jinja')

    encoding = self.__dfa.encoding()
    char_types = {'latin1': 'uint8_t', 'utf16': 'uint16_t', 'utf8': 'int8_t'}
    char_type = char_types[encoding.name()]

    return template.render(
      start_node_number = 0,
      debug_print = self.__debug_print,
      default_action = default_action,
      dfa_states = dfa_states,
      encoding = encoding.name(),
      char_type = char_type,
      upper_bound = encoding.primary_range()[1])
