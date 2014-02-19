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

import ply.lex as lex
import ply.yacc as yacc
from term import Term
from regex_parser import RegexParser, ParserBuilder
from automaton import Action
from nfa_builder import NfaBuilder
from dfa import Dfa
from dfa_optimizer import DfaOptimizer
from dfa_minimizer import DfaMinimizer
from transition_key import TransitionKey, KeyEncoding
from backtracking_generator import BacktrackingGenerator

class RuleLexer:

  tokens = (
    'DEFAULT_ACTION',
    'EPSILON',
    'EOS',
    'CATCH_ALL',

    'IDENTIFIER',
    'STRING',
    'REGEX',
    'CHARACTER_CLASS_REGEX',

    'PLUS',
    'QUESTION_MARK',
    'EQUALS',
    'OR',
    'STAR',
    'LEFT_PARENTHESIS',
    'RIGHT_PARENTHESIS',
    'GRAPH_OPEN',
    'GRAPH_CLOSE',
    'SEMICOLON',
    'ACTION_OPEN',
    'ACTION_CLOSE',

    'COMMA',
  )

  t_ignore = " \t\n\r"

  def t_COMMENT(self, t):
    r'\#.*[\n\r]+'
    pass

  __special_identifiers = set(
    ['default_action', 'epsilon', 'catch_all', 'eos'])

  def t_IDENTIFIER(self, t):
    r'[a-zA-Z0-9_]+'
    if t.value in self.__special_identifiers:
      t.type = t.value.upper()
    return t

  t_STRING = r'"((\\("|\w|\\))|[^\\"])+"'
  t_REGEX = r'/(\\/|[^/])+/'
  t_CHARACTER_CLASS_REGEX = r'\[([^\]]|\\\])+\]'

  t_PLUS = r'\+'
  t_QUESTION_MARK = r'\?'
  t_STAR = r'\*'
  t_OR = r'\|'
  t_EQUALS = '='
  t_LEFT_PARENTHESIS = r'\('
  t_RIGHT_PARENTHESIS = r'\)'
  t_GRAPH_OPEN = '<<'
  t_GRAPH_CLOSE = '>>'
  t_SEMICOLON = ';'
  t_ACTION_OPEN = '<'
  t_ACTION_CLOSE = '>'
  t_COMMA = ','

  def t_ANY_error(self, t):
    raise Exception("Illegal character '%s'" % t.value[0])

class RuleParserState:

  def __init__(self, encoding):
    self.aliases = {}
    self.character_classes = {}
    self.current_state = None
    self.rules = {}
    self.transitions = set()
    self.encoding = encoding

class RuleParser:

  tokens = RuleLexer.tokens
  __rule_precedence_counter = 0
  __keyword_transitions = set(['continue'])

  def __init__(self):
    self.__state = None

  def p_statements(self, p):
    'statements : aliases rules'

  def p_aliases(self, p):
    '''aliases : alias_rule aliases
               | empty'''

  def p_alias_rule(self, p):
    'alias_rule : IDENTIFIER EQUALS composite_regex SEMICOLON'
    state = self.__state
    assert not p[1] in state.aliases, "cannot reassign alias %s" % p[1]
    term = p[3]
    state.aliases[p[1]] = term
    if term.name() == 'CLASS' or term.name() == 'NOT_CLASS':
      classes = state.character_classes
      assert not p[1] in classes, "cannot reassign alias %s" % p[1]
      encoding = state.encoding
      classes[p[1]] = TransitionKey.character_class(encoding, term, classes)

  def p_rules(self, p):
    '''rules : state_change transition_rules rules
             | empty'''

  def p_state_change(self, p):
    'state_change : GRAPH_OPEN IDENTIFIER GRAPH_CLOSE'
    state = self.__state
    state.current_state = p[2]
    assert state.current_state
    if not state.current_state in state.rules:
      state.rules[state.current_state] = {
        'default_action': Term.empty_term(),
        'uniques' : {},
        'trees' : []
      }

  def p_transition_rules(self, p):
    '''transition_rules : transition_rule transition_rules
                        | empty'''

  def p_transition_rule(self, p):
    '''transition_rule : composite_regex action
                       | DEFAULT_ACTION default_action
                       | EOS eos
                       | EPSILON epsilon
                       | CATCH_ALL action'''
    precedence = RuleParser.__rule_precedence_counter
    RuleParser.__rule_precedence_counter += 1
    action = p[2]
    (entry_action, match_action, transition) = action
    if transition and not transition.name() in self.__keyword_transitions:
      assert not transition.name() == 'default', "can't append default graph"
      self.__state.transitions.add(transition.name())
    rules = self.__state.rules[self.__state.current_state]
    # process default action
    if p[1] == 'default_action':
      assert self.__state.current_state == 'default'
      assert not rules['default_action']
      assert not entry_action
      rules['default_action'] = match_action
      return
    # process tree
    if p[1] == 'eos' or p[1] == 'catch_all':
      assert p[1] not in rules['uniques'], "cannot redefine %s in %s" % (
        p[1], self.__state.current_state)
      rules['uniques'][p[1]] = True
      tree = NfaBuilder.unique_key(p[1])
    elif p[1] == 'epsilon':
      tree = Term.empty_term()
    else:
      tree = p[1]  # regex case
    # install actions
    tree = NfaBuilder.add_action(tree, entry_action, match_action, precedence)
    # handle transitions
    if not transition:
      pass
    elif transition.name() == 'continue':
      tree = NfaBuilder.add_continue(tree, transition.args()[0])
    else:
      tree = NfaBuilder.join_subtree(tree, transition.name())
    # store tree
    rules['trees'].append(tree)

  def p_action(self, p):
    '''action : ACTION_OPEN maybe_identifier_action OR maybe_identifier_action OR maybe_transition ACTION_CLOSE'''
    p[0] = (p[2], p[4], p[6])

  def p_default_action(self, p):
    'default_action : ACTION_OPEN identifier_action ACTION_CLOSE'
    p[0] = (Term.empty_term(), p[2], Term.empty_term())

  def p_eos(self, p):
    'eos : ACTION_OPEN identifier_action ACTION_CLOSE'
    p[0] = (Term.empty_term(), p[2], Term.empty_term())

  def p_epsilon(self, p):
    'epsilon : ACTION_OPEN maybe_transition ACTION_CLOSE'
    assert p[2], 'cannot have an empty epsilon transition'
    p[0] = (Term.empty_term(), Term.empty_term(), p[2])

  def p_maybe_identifier_action(self, p):
    '''maybe_identifier_action : identifier_action
                         | empty'''
    p[0] = p[1] if p[1] else Term.empty_term()

  def p_maybe_transition(self, p):
    '''maybe_transition : IDENTIFIER
                        | IDENTIFIER LEFT_PARENTHESIS IDENTIFIER RIGHT_PARENTHESIS
                        | empty'''
    if len(p) == 5:
      assert p[1] == 'continue', 'only continue can take arguments'
      p[0] = Term(p[1], p[3])
    else:
      assert len(p) == 2
      p[0] = Term(p[1], '0') if p[1] else Term.empty_term()

  def p_identifier_action(self, p):
    '''identifier_action : IDENTIFIER
                         | IDENTIFIER LEFT_PARENTHESIS RIGHT_PARENTHESIS
                         | IDENTIFIER LEFT_PARENTHESIS action_params RIGHT_PARENTHESIS'''
    if len(p) == 2 or len(p) == 4:
      p[0] = Term(p[1])
    elif len(p) == 5:
        p[0] = Term(p[1], *p[3])
    else:
      raise Exception()

  def p_action_params(self, p):
    '''action_params : IDENTIFIER
                     | IDENTIFIER COMMA action_params'''
    if len(p) == 2:
      p[0] = (p[1],)
    elif len(p) == 4:
      p[0] = tuple(([p[1]] + list(p[3])))
    else:
      raise Exception()

  def p_composite_regex(self, p):
    '''composite_regex : regex_parts OR regex_parts
                       | regex_parts'''
    if len(p) == 2:
      p[0] = p[1]
    else:
      p[0] = NfaBuilder.or_terms([p[1], p[3]])

  def p_regex_parts(self, p):
    '''regex_parts : regex_part
                   | regex_part regex_parts'''
    p[0] = NfaBuilder.cat_terms(p[1:])

  def p_regex_part(self, p):
    '''regex_part : LEFT_PARENTHESIS composite_regex RIGHT_PARENTHESIS modifier
                  | regex_string_literal modifier
                  | regex_class modifier
                  | regex modifier
                  | regex_alias modifier'''
    modifier = p[len(p)-1]
    term = p[2] if len(p) == 5 else p[1]
    if modifier:
      p[0] = NfaBuilder.apply_modifier(modifier, term)
    else:
      p[0] = term

  def p_regex_string_literal(self, p):
    'regex_string_literal : STRING'
    string = p[1][1:-1]
    escape_char = lambda string, char: string.replace(char, "\\" + char)
    string = reduce(escape_char, "+?*|.[](){}", string).replace("\\\"", "\"")
    p[0] = RegexParser.parse(string)

  def p_regex(self, p):
    'regex : REGEX'
    string = p[1][1:-1].replace("\\/", "/")
    p[0] = RegexParser.parse(string)

  def p_regex_class(self, p):
    'regex_class : CHARACTER_CLASS_REGEX'
    p[0] = RegexParser.parse(p[1])

  def p_regex_alias(self, p):
    'regex_alias : IDENTIFIER'
    p[0] = self.__state.aliases[p[1]]

  def p_modifier(self, p):
    '''modifier : PLUS
                | QUESTION_MARK
                | STAR
                | empty'''
    p[0] = p[1]

  def p_empty(self, p):
    'empty :'

  def p_error(self, p):
    raise Exception("Syntax error in input '%s'" % str(p))

  @staticmethod
  def parse(string, parser_state):
    new_lexer = lambda: RuleLexer()
    new_parser = lambda: RuleParser()
    def preparse(parser):
      parser.__state = parser_state
    def postparse(parser):
      parser.__state = None
    return ParserBuilder.parse(
      string, "RuleParser", new_lexer, new_parser, preparse, postparse)

class RuleProcessor(object):

  def __init__(self, string, encoding_name):
    self.__automata = {}
    self.__default_action = None
    self.__encoding = KeyEncoding.get(encoding_name)
    self.__parser_state = RuleParserState(self.__encoding)
    RuleParser.parse(string, self.__parser_state)
    self.__process_parser_state()

  def encoding(self):
    return self.__encoding

  def alias_iter(self):
    return iter(self.__parser_state.aliases.items())

  def automata_iter(self):
    return iter(self.__automata.items())

  def default_automata(self):
    return self.__automata['default']

  def default_action(self):
    return self.__default_action

  class Automata(object):
    'a container for the resulting automata, which are lazily built'

    def __init__(self, rule_processor, character_classes, rule_map, name):
      self.__rule_processor = rule_processor
      self.__character_classes = character_classes
      self.__rule_map = rule_map
      self.__name = name
      self.__nfa = None
      self.__dfa = None
      self.__minimial_dfa = None

    def encoding(self):
      return self.__rule_processor.encoding()

    def name(self):
      return self.__name

    def rule_term(self):
      return self.__rule_map[self.__name]

    def nfa(self):
      if not self.__nfa:
        self.__nfa = NfaBuilder.nfa(
          self.encoding(), self.__character_classes,
          self.__rule_map, self.__name)
      return self.__nfa

    def dfa(self):
      if not self.__dfa:
        (start, dfa_nodes) = self.nfa().compute_dfa()
        dfa = Dfa(self.encoding(), start, dfa_nodes)
        if self.name() == 'default':
          dfa = BacktrackingGenerator.generate(
            dfa, self.__rule_processor.default_action())
        self.__dfa = dfa
      return self.__dfa

    def optimize_dfa(self, log = False):
      assert not self.__dfa
      assert not self.__minimial_dfa
      self.__dfa = DfaOptimizer.optimize(self.minimal_dfa(), log)
      self.__minimial_dfa = None

    def minimal_dfa(self):
      if not self.__minimial_dfa:
        self.__minimial_dfa = DfaMinimizer(self.dfa()).minimize()
      return self.__minimial_dfa

  def __process_parser_state(self):
    parser_state = self.__parser_state
    assert 'default' in parser_state.rules, "default lexer state required"
    # Check that we don't transition to a nonexistent state.
    assert parser_state.transitions <= set(parser_state.rules.keys())
    rule_map = {}
    # process all subgraphs
    for tree_name, v in parser_state.rules.items():
      assert v['trees'], "lexer state %s is empty" % tree_name
      rule_map[tree_name] = NfaBuilder.or_terms(v['trees'])
    # build the automata
    for name, tree in rule_map.items():
      self.__automata[name] = RuleProcessor.Automata(
        self, parser_state.character_classes, rule_map, name)
    # process default_action
    default_action = parser_state.rules['default']['default_action']
    self.__default_action = Action(default_action, 0)
