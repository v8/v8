// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_TORQUE_TORQUELEXER_H_
#define V8_TORQUE_TORQUELEXER_H_

// Generated from Torque.g4 by ANTLR 4.7.1

#pragma once

#include "./antlr4-runtime.h"

class TorqueLexer : public antlr4::Lexer {
 public:
  enum {
    T__0 = 1,
    T__1 = 2,
    T__2 = 3,
    T__3 = 4,
    T__4 = 5,
    T__5 = 6,
    T__6 = 7,
    T__7 = 8,
    T__8 = 9,
    T__9 = 10,
    T__10 = 11,
    T__11 = 12,
    T__12 = 13,
    T__13 = 14,
    T__14 = 15,
    T__15 = 16,
    T__16 = 17,
    T__17 = 18,
    T__18 = 19,
    T__19 = 20,
    T__20 = 21,
    MACRO = 22,
    BUILTIN = 23,
    RUNTIME = 24,
    MODULE = 25,
    JAVASCRIPT = 26,
    IMPLICIT = 27,
    DEFERRED = 28,
    IF = 29,
    CAST_KEYWORD = 30,
    CONVERT_KEYWORD = 31,
    FOR = 32,
    WHILE = 33,
    RETURN = 34,
    CONTINUE = 35,
    BREAK = 36,
    GOTO = 37,
    OTHERWISE = 38,
    TRY = 39,
    CATCH = 40,
    LABEL = 41,
    LABELS = 42,
    TAIL = 43,
    ISNT = 44,
    IS = 45,
    LET = 46,
    ASSIGNMENT = 47,
    ASSIGNMENT_OPERATOR = 48,
    EQUAL = 49,
    PLUS = 50,
    MINUS = 51,
    MULTIPLY = 52,
    DIVIDE = 53,
    MODULO = 54,
    BIT_OR = 55,
    BIT_AND = 56,
    BIT_NOT = 57,
    MAX = 58,
    MIN = 59,
    NOT_EQUAL = 60,
    LESS_THAN = 61,
    LESS_THAN_EQUAL = 62,
    GREATER_THAN = 63,
    GREATER_THAN_EQUAL = 64,
    SHIFT_LEFT = 65,
    SHIFT_RIGHT = 66,
    SHIFT_RIGHT_ARITHMETIC = 67,
    VARARGS = 68,
    EQUALITY_OPERATOR = 69,
    INCREMENT = 70,
    DECREMENT = 71,
    NOT = 72,
    STRING_LITERAL = 73,
    IDENTIFIER = 74,
    WS = 75,
    BLOCK_COMMENT = 76,
    LINE_COMMENT = 77,
    DECIMAL_LITERAL = 78
  };

  explicit TorqueLexer(antlr4::CharStream* input);
  ~TorqueLexer();

  std::string getGrammarFileName() const override;
  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;
  const std::vector<std::string>& getModeNames() const override;
  const std::vector<std::string>& getTokenNames()
      const override;  // deprecated, use vocabulary instead
  antlr4::dfa::Vocabulary& getVocabulary() const override;

  const std::vector<uint16_t> getSerializedATN() const override;
  const antlr4::atn::ATN& getATN() const override;

 private:
  static std::vector<antlr4::dfa::DFA> _decisionToDFA;
  static antlr4::atn::PredictionContextCache _sharedContextCache;
  static std::vector<std::string> _ruleNames;
  static std::vector<std::string> _tokenNames;
  static std::vector<std::string> _channelNames;
  static std::vector<std::string> _modeNames;

  static std::vector<std::string> _literalNames;
  static std::vector<std::string> _symbolicNames;
  static antlr4::dfa::Vocabulary _vocabulary;
  static antlr4::atn::ATN _atn;
  static std::vector<uint16_t> _serializedATN;

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

  struct Initializer {
    Initializer();
  };
  static Initializer _init;
};

#endif  // V8_TORQUE_TORQUELEXER_H_
