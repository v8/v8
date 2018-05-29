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
    UNSAFE_CAST_KEYWORD = 31,
    CONVERT_KEYWORD = 32,
    FOR = 33,
    WHILE = 34,
    RETURN = 35,
    CONSTEXPR = 36,
    CONTINUE = 37,
    BREAK = 38,
    GOTO = 39,
    OTHERWISE = 40,
    TRY = 41,
    CATCH = 42,
    LABEL = 43,
    LABELS = 44,
    TAIL = 45,
    ISNT = 46,
    IS = 47,
    LET = 48,
    EXTERN = 49,
    ASSERT_TOKEN = 50,
    CHECK_TOKEN = 51,
    UNREACHABLE_TOKEN = 52,
    DEBUG_TOKEN = 53,
    ASSIGNMENT = 54,
    ASSIGNMENT_OPERATOR = 55,
    EQUAL = 56,
    PLUS = 57,
    MINUS = 58,
    MULTIPLY = 59,
    DIVIDE = 60,
    MODULO = 61,
    BIT_OR = 62,
    BIT_AND = 63,
    BIT_NOT = 64,
    MAX = 65,
    MIN = 66,
    NOT_EQUAL = 67,
    LESS_THAN = 68,
    LESS_THAN_EQUAL = 69,
    GREATER_THAN = 70,
    GREATER_THAN_EQUAL = 71,
    SHIFT_LEFT = 72,
    SHIFT_RIGHT = 73,
    SHIFT_RIGHT_ARITHMETIC = 74,
    VARARGS = 75,
    EQUALITY_OPERATOR = 76,
    INCREMENT = 77,
    DECREMENT = 78,
    NOT = 79,
    STRING_LITERAL = 80,
    IDENTIFIER = 81,
    WS = 82,
    BLOCK_COMMENT = 83,
    LINE_COMMENT = 84,
    DECIMAL_LITERAL = 85
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
