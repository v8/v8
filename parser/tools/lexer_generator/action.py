# Copyright 2014 the V8 project authors. All rights reserved.
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

from types import StringType, IntType

class Term(object):
  '''An immutable class representing a function and its arguments.
  f(a,b,c) would be represented as ('f', a, b, c) where
  a, b, and c are strings, integers or Terms.'''

  __empty_term = None

  @staticmethod
  def empty_term():
    if Term.__empty_term == None:
      Term.__empty_term = Term('')
    return Term.__empty_term

  def __init__(self, name, *args):
    assert type(name) == StringType
    if not name:
      assert not args, 'empty term must not have args'
    for v in args:
      if type(v) == IntType or type(v) == StringType:
        continue
      else:
        assert isinstance(v, Term)
    self.__tuple = tuple([name] + list(args))
    self.__str = None

  def name(self):
    return self.__tuple[0]

  def args(self):
    return self.__tuple[1:]

  def  __hash__(self):
    return hash(self.__tuple)

  def __nonzero__(self):
    'true <==> self == empty_term'
    return bool(self.__tuple[0])

  def __eq__(self, other):
    return (isinstance(other, self.__class__) and self.__tuple == other.__tuple)

  # TODO(dcarney): escape '(', ')' and ',' in strings
  def __str__(self):
    if self.__str == None:
      self.__str = '(%s)' % ','.join(map(str, self.__tuple))
    return self.__str

  def to_dot(self):
    node_ix = [0]
    node_template = 'node [label="%s"]; N_%d;'
    edge_template = 'N_%d -> N_%d'
    nodes = []
    edges = []

    def escape(v):  # TODO(dcarney): abstract into utilities
      v = str(v)
      v = v.replace('\r', '\\\\r').replace('\t', '\\\\t').replace('\n', '\\\\n')
      v = v.replace('\\', '\\\\').replace('\"', '\\\"')
      return v

    def process(term):
      if type(term) == StringType or type(term) == IntType:
        node_ix[0] += 1
        nodes.append(node_template % (escape(str(term)), node_ix[0]))
        return node_ix[0]
      elif isinstance(term, Term):
        child_ixs = map(process, term.args())
        node_ix[0] += 1
        nodes.append(node_template % (escape(term.name()), node_ix[0]))
        for child_ix in child_ixs:
          edges.append(edge_template % (node_ix[0], child_ix))
        return node_ix[0]
      raise Exception

    process(self)
    return 'digraph { %s %s }' % ('\n'.join(nodes), '\n'.join(edges))

class Action(object):

  __empty_action = None

  @staticmethod
  def empty_action():
    if Action.__empty_action == None:
      Action.__empty_action = Action(Term.empty_term(), Term.empty_term())
    return Action.__empty_action

  @staticmethod
  def dominant_action(state_set):
    action = Action.empty_action()
    for state in state_set:
      if not state.action():
        continue
      if not action:
        action = state.action()
        continue
      if state.action().precedence() == action.precedence():
        assert state.action() == action
      elif state.action().precedence() < action.precedence():
        action = state.action()
    return action

  def __init__(self, entry_action, match_action, precedence = -1):
    assert isinstance(match_action, Term)
    assert isinstance(entry_action, Term)
    assert type(precedence) == IntType
    self.__entry_action = entry_action
    self.__match_action = match_action
    self.__precedence = precedence

  def entry_action(self):
    return self.__entry_action

  def match_action(self):
    return self.__match_action

  def precedence(self):
    return self.__precedence

  def to_term(self):
    return Term(
      'action_serialization',
      self.__entry_action, self.__match_action, str(self.__precedence))

  @staticmethod
  def from_term(term):
    assert term.name() == 'action_serialization'
    return Action(term.args()[0], term.args()[1], int(term.args()[2]))

  def __nonzero__(self):
    return bool(self.__entry_action) or bool(self.__match_action)

  def __hash__(self):
    return hash((self.__entry_action, self.__match_action))

  def __eq__(self, other):
    return (isinstance(other, self.__class__) and
            self.__entry_action == other.__entry_action and
            self.__match_action == other.__match_action)

  def __str__(self):
    parts = map(lambda action : '' if not action else str(action),
                [self.__entry_action, self.__match_action])
    return "action< %s >" % " | ".join(parts)
