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
    CONSTEXPR = 35,
    CONTINUE = 36,
    BREAK = 37,
    GOTO = 38,
    OTHERWISE = 39,
    TRY = 40,
    CATCH = 41,
    LABEL = 42,
    LABELS = 43,
    TAIL = 44,
    ISNT = 45,
    IS = 46,
    LET = 47,
    EXTERN = 48,
    ASSERT_TOKEN = 49,
    CHECK_TOKEN = 50,
    UNREACHABLE_TOKEN = 51,
    DEBUG_TOKEN = 52,
    ASSIGNMENT = 53,
    ASSIGNMENT_OPERATOR = 54,
    EQUAL = 55,
    PLUS = 56,
    MINUS = 57,
    MULTIPLY = 58,
    DIVIDE = 59,
    MODULO = 60,
    BIT_OR = 61,
    BIT_AND = 62,
    BIT_NOT = 63,
    MAX = 64,
    MIN = 65,
    NOT_EQUAL = 66,
    LESS_THAN = 67,
    LESS_THAN_EQUAL = 68,
    GREATER_THAN = 69,
    GREATER_THAN_EQUAL = 70,
    SHIFT_LEFT = 71,
    SHIFT_RIGHT = 72,
    SHIFT_RIGHT_ARITHMETIC = 73,
    VARARGS = 74,
    EQUALITY_OPERATOR = 75,
    INCREMENT = 76,
    DECREMENT = 77,
    NOT = 78,
    STRING_LITERAL = 79,
    IDENTIFIER = 80,
    WS = 81,
    BLOCK_COMMENT = 82,
    LINE_COMMENT = 83,
    DECIMAL_LITERAL = 84
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
