// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from Torque.g4 by ANTLR 4.7.1

#include "TorqueListener.h"
#include "TorqueVisitor.h"

#include "TorqueParser.h"

using namespace antlrcpp;
using namespace antlr4;

TorqueParser::TorqueParser(TokenStream* input) : Parser(input) {
  _interpreter = new atn::ParserATNSimulator(this, _atn, _decisionToDFA,
                                             _sharedContextCache);
}

TorqueParser::~TorqueParser() { delete _interpreter; }

std::string TorqueParser::getGrammarFileName() const { return "Torque.g4"; }

const std::vector<std::string>& TorqueParser::getRuleNames() const {
  return _ruleNames;
}

dfa::Vocabulary& TorqueParser::getVocabulary() const { return _vocabulary; }

//----------------- TypeContext
//------------------------------------------------------------------

TorqueParser::TypeContext::TypeContext(ParserRuleContext* parent,
                                       size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::TypeContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

size_t TorqueParser::TypeContext::getRuleIndex() const {
  return TorqueParser::RuleType;
}

void TorqueParser::TypeContext::enterRule(tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterType(this);
}

void TorqueParser::TypeContext::exitRule(tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitType(this);
}

antlrcpp::Any TorqueParser::TypeContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitType(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::TypeContext* TorqueParser::type() {
  TypeContext* _localctx =
      _tracker.createInstance<TypeContext>(_ctx, getState());
  enterRule(_localctx, 0, TorqueParser::RuleType);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(128);
    match(TorqueParser::IDENTIFIER);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeListContext
//------------------------------------------------------------------

TorqueParser::TypeListContext::TypeListContext(ParserRuleContext* parent,
                                               size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

std::vector<TorqueParser::TypeContext*> TorqueParser::TypeListContext::type() {
  return getRuleContexts<TorqueParser::TypeContext>();
}

TorqueParser::TypeContext* TorqueParser::TypeListContext::type(size_t i) {
  return getRuleContext<TorqueParser::TypeContext>(i);
}

size_t TorqueParser::TypeListContext::getRuleIndex() const {
  return TorqueParser::RuleTypeList;
}

void TorqueParser::TypeListContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterTypeList(this);
}

void TorqueParser::TypeListContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitTypeList(this);
}

antlrcpp::Any TorqueParser::TypeListContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitTypeList(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::TypeListContext* TorqueParser::typeList() {
  TypeListContext* _localctx =
      _tracker.createInstance<TypeListContext>(_ctx, getState());
  enterRule(_localctx, 2, TorqueParser::RuleTypeList);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(130);
    match(TorqueParser::T__0);
    setState(132);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::IDENTIFIER) {
      setState(131);
      type();
    }
    setState(138);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == TorqueParser::T__1) {
      setState(134);
      match(TorqueParser::T__1);
      setState(135);
      type();
      setState(140);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(141);
    match(TorqueParser::T__2);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeListMaybeVarArgsContext
//------------------------------------------------------------------

TorqueParser::TypeListMaybeVarArgsContext::TypeListMaybeVarArgsContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

std::vector<TorqueParser::TypeContext*>
TorqueParser::TypeListMaybeVarArgsContext::type() {
  return getRuleContexts<TorqueParser::TypeContext>();
}

TorqueParser::TypeContext* TorqueParser::TypeListMaybeVarArgsContext::type(
    size_t i) {
  return getRuleContext<TorqueParser::TypeContext>(i);
}

tree::TerminalNode* TorqueParser::TypeListMaybeVarArgsContext::VARARGS() {
  return getToken(TorqueParser::VARARGS, 0);
}

size_t TorqueParser::TypeListMaybeVarArgsContext::getRuleIndex() const {
  return TorqueParser::RuleTypeListMaybeVarArgs;
}

void TorqueParser::TypeListMaybeVarArgsContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->enterTypeListMaybeVarArgs(this);
}

void TorqueParser::TypeListMaybeVarArgsContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitTypeListMaybeVarArgs(this);
}

antlrcpp::Any TorqueParser::TypeListMaybeVarArgsContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitTypeListMaybeVarArgs(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::TypeListMaybeVarArgsContext*
TorqueParser::typeListMaybeVarArgs() {
  TypeListMaybeVarArgsContext* _localctx =
      _tracker.createInstance<TypeListMaybeVarArgsContext>(_ctx, getState());
  enterRule(_localctx, 4, TorqueParser::RuleTypeListMaybeVarArgs);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    size_t alt;
    setState(162);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
        _input, 5, _ctx)) {
      case 1: {
        enterOuterAlt(_localctx, 1);
        setState(143);
        match(TorqueParser::T__0);
        setState(145);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == TorqueParser::IDENTIFIER) {
          setState(144);
          type();
        }
        setState(151);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
            _input, 3, _ctx);
        while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
          if (alt == 1) {
            setState(147);
            match(TorqueParser::T__1);
            setState(148);
            type();
          }
          setState(153);
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
              _input, 3, _ctx);
        }
        setState(156);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == TorqueParser::T__1) {
          setState(154);
          match(TorqueParser::T__1);
          setState(155);
          match(TorqueParser::VARARGS);
        }
        setState(158);
        match(TorqueParser::T__2);
        break;
      }

      case 2: {
        enterOuterAlt(_localctx, 2);
        setState(159);
        match(TorqueParser::T__0);
        setState(160);
        match(TorqueParser::VARARGS);
        setState(161);
        match(TorqueParser::T__2);
        break;
      }
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LabelParameterContext
//------------------------------------------------------------------

TorqueParser::LabelParameterContext::LabelParameterContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::LabelParameterContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::TypeListContext* TorqueParser::LabelParameterContext::typeList() {
  return getRuleContext<TorqueParser::TypeListContext>(0);
}

size_t TorqueParser::LabelParameterContext::getRuleIndex() const {
  return TorqueParser::RuleLabelParameter;
}

void TorqueParser::LabelParameterContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterLabelParameter(this);
}

void TorqueParser::LabelParameterContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitLabelParameter(this);
}

antlrcpp::Any TorqueParser::LabelParameterContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitLabelParameter(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::LabelParameterContext* TorqueParser::labelParameter() {
  LabelParameterContext* _localctx =
      _tracker.createInstance<LabelParameterContext>(_ctx, getState());
  enterRule(_localctx, 6, TorqueParser::RuleLabelParameter);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(164);
    match(TorqueParser::IDENTIFIER);
    setState(166);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::T__0) {
      setState(165);
      typeList();
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- OptionalTypeContext
//------------------------------------------------------------------

TorqueParser::OptionalTypeContext::OptionalTypeContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::TypeContext* TorqueParser::OptionalTypeContext::type() {
  return getRuleContext<TorqueParser::TypeContext>(0);
}

size_t TorqueParser::OptionalTypeContext::getRuleIndex() const {
  return TorqueParser::RuleOptionalType;
}

void TorqueParser::OptionalTypeContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterOptionalType(this);
}

void TorqueParser::OptionalTypeContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitOptionalType(this);
}

antlrcpp::Any TorqueParser::OptionalTypeContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitOptionalType(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::OptionalTypeContext* TorqueParser::optionalType() {
  OptionalTypeContext* _localctx =
      _tracker.createInstance<OptionalTypeContext>(_ctx, getState());
  enterRule(_localctx, 8, TorqueParser::RuleOptionalType);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(170);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::T__3) {
      setState(168);
      match(TorqueParser::T__3);
      setState(169);
      type();
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- OptionalLabelListContext
//------------------------------------------------------------------

TorqueParser::OptionalLabelListContext::OptionalLabelListContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::OptionalLabelListContext::LABELS() {
  return getToken(TorqueParser::LABELS, 0);
}

std::vector<TorqueParser::LabelParameterContext*>
TorqueParser::OptionalLabelListContext::labelParameter() {
  return getRuleContexts<TorqueParser::LabelParameterContext>();
}

TorqueParser::LabelParameterContext*
TorqueParser::OptionalLabelListContext::labelParameter(size_t i) {
  return getRuleContext<TorqueParser::LabelParameterContext>(i);
}

size_t TorqueParser::OptionalLabelListContext::getRuleIndex() const {
  return TorqueParser::RuleOptionalLabelList;
}

void TorqueParser::OptionalLabelListContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterOptionalLabelList(this);
}

void TorqueParser::OptionalLabelListContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitOptionalLabelList(this);
}

antlrcpp::Any TorqueParser::OptionalLabelListContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitOptionalLabelList(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::OptionalLabelListContext* TorqueParser::optionalLabelList() {
  OptionalLabelListContext* _localctx =
      _tracker.createInstance<OptionalLabelListContext>(_ctx, getState());
  enterRule(_localctx, 10, TorqueParser::RuleOptionalLabelList);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(181);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::LABELS) {
      setState(172);
      match(TorqueParser::LABELS);
      setState(173);
      labelParameter();
      setState(178);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == TorqueParser::T__1) {
        setState(174);
        match(TorqueParser::T__1);
        setState(175);
        labelParameter();
        setState(180);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- OptionalOtherwiseContext
//------------------------------------------------------------------

TorqueParser::OptionalOtherwiseContext::OptionalOtherwiseContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::OptionalOtherwiseContext::OTHERWISE() {
  return getToken(TorqueParser::OTHERWISE, 0);
}

std::vector<tree::TerminalNode*>
TorqueParser::OptionalOtherwiseContext::IDENTIFIER() {
  return getTokens(TorqueParser::IDENTIFIER);
}

tree::TerminalNode* TorqueParser::OptionalOtherwiseContext::IDENTIFIER(
    size_t i) {
  return getToken(TorqueParser::IDENTIFIER, i);
}

size_t TorqueParser::OptionalOtherwiseContext::getRuleIndex() const {
  return TorqueParser::RuleOptionalOtherwise;
}

void TorqueParser::OptionalOtherwiseContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterOptionalOtherwise(this);
}

void TorqueParser::OptionalOtherwiseContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitOptionalOtherwise(this);
}

antlrcpp::Any TorqueParser::OptionalOtherwiseContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitOptionalOtherwise(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::OptionalOtherwiseContext* TorqueParser::optionalOtherwise() {
  OptionalOtherwiseContext* _localctx =
      _tracker.createInstance<OptionalOtherwiseContext>(_ctx, getState());
  enterRule(_localctx, 12, TorqueParser::RuleOptionalOtherwise);

  auto onExit = finally([=] { exitRule(); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(192);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
        _input, 11, _ctx)) {
      case 1: {
        setState(183);
        match(TorqueParser::OTHERWISE);
        setState(184);
        match(TorqueParser::IDENTIFIER);
        setState(189);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
            _input, 10, _ctx);
        while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
          if (alt == 1) {
            setState(185);
            match(TorqueParser::T__1);
            setState(186);
            match(TorqueParser::IDENTIFIER);
          }
          setState(191);
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
              _input, 10, _ctx);
        }
        break;
      }
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParameterContext
//------------------------------------------------------------------

TorqueParser::ParameterContext::ParameterContext(ParserRuleContext* parent,
                                                 size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ParameterContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::TypeContext* TorqueParser::ParameterContext::type() {
  return getRuleContext<TorqueParser::TypeContext>(0);
}

size_t TorqueParser::ParameterContext::getRuleIndex() const {
  return TorqueParser::RuleParameter;
}

void TorqueParser::ParameterContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterParameter(this);
}

void TorqueParser::ParameterContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitParameter(this);
}

antlrcpp::Any TorqueParser::ParameterContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitParameter(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ParameterContext* TorqueParser::parameter() {
  ParameterContext* _localctx =
      _tracker.createInstance<ParameterContext>(_ctx, getState());
  enterRule(_localctx, 14, TorqueParser::RuleParameter);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(194);
    match(TorqueParser::IDENTIFIER);
    setState(195);
    match(TorqueParser::T__3);
    setState(197);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::IDENTIFIER) {
      setState(196);
      type();
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParameterListContext
//------------------------------------------------------------------

TorqueParser::ParameterListContext::ParameterListContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

std::vector<TorqueParser::ParameterContext*>
TorqueParser::ParameterListContext::parameter() {
  return getRuleContexts<TorqueParser::ParameterContext>();
}

TorqueParser::ParameterContext* TorqueParser::ParameterListContext::parameter(
    size_t i) {
  return getRuleContext<TorqueParser::ParameterContext>(i);
}

tree::TerminalNode* TorqueParser::ParameterListContext::VARARGS() {
  return getToken(TorqueParser::VARARGS, 0);
}

tree::TerminalNode* TorqueParser::ParameterListContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

size_t TorqueParser::ParameterListContext::getRuleIndex() const {
  return TorqueParser::RuleParameterList;
}

void TorqueParser::ParameterListContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterParameterList(this);
}

void TorqueParser::ParameterListContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitParameterList(this);
}

antlrcpp::Any TorqueParser::ParameterListContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitParameterList(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ParameterListContext* TorqueParser::parameterList() {
  ParameterListContext* _localctx =
      _tracker.createInstance<ParameterListContext>(_ctx, getState());
  enterRule(_localctx, 16, TorqueParser::RuleParameterList);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    setState(220);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
        _input, 15, _ctx)) {
      case 1: {
        enterOuterAlt(_localctx, 1);
        setState(199);
        match(TorqueParser::T__0);
        setState(201);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == TorqueParser::IDENTIFIER) {
          setState(200);
          parameter();
        }
        setState(207);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == TorqueParser::T__1) {
          setState(203);
          match(TorqueParser::T__1);
          setState(204);
          parameter();
          setState(209);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        setState(210);
        match(TorqueParser::T__2);
        break;
      }

      case 2: {
        enterOuterAlt(_localctx, 2);
        setState(211);
        match(TorqueParser::T__0);
        setState(212);
        parameter();
        setState(213);
        match(TorqueParser::T__1);
        setState(214);
        parameter();
        setState(215);
        match(TorqueParser::T__1);
        setState(216);
        match(TorqueParser::VARARGS);
        setState(217);
        match(TorqueParser::IDENTIFIER);
        setState(218);
        match(TorqueParser::T__2);
        break;
      }
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LabelDeclarationContext
//------------------------------------------------------------------

TorqueParser::LabelDeclarationContext::LabelDeclarationContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::LabelDeclarationContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::ParameterListContext*
TorqueParser::LabelDeclarationContext::parameterList() {
  return getRuleContext<TorqueParser::ParameterListContext>(0);
}

size_t TorqueParser::LabelDeclarationContext::getRuleIndex() const {
  return TorqueParser::RuleLabelDeclaration;
}

void TorqueParser::LabelDeclarationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterLabelDeclaration(this);
}

void TorqueParser::LabelDeclarationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitLabelDeclaration(this);
}

antlrcpp::Any TorqueParser::LabelDeclarationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitLabelDeclaration(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::LabelDeclarationContext* TorqueParser::labelDeclaration() {
  LabelDeclarationContext* _localctx =
      _tracker.createInstance<LabelDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 18, TorqueParser::RuleLabelDeclaration);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(222);
    match(TorqueParser::IDENTIFIER);
    setState(224);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::T__0) {
      setState(223);
      parameterList();
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExpressionContext
//------------------------------------------------------------------

TorqueParser::ExpressionContext::ExpressionContext(ParserRuleContext* parent,
                                                   size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::ConditionalExpressionContext*
TorqueParser::ExpressionContext::conditionalExpression() {
  return getRuleContext<TorqueParser::ConditionalExpressionContext>(0);
}

size_t TorqueParser::ExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleExpression;
}

void TorqueParser::ExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterExpression(this);
}

void TorqueParser::ExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitExpression(this);
}

antlrcpp::Any TorqueParser::ExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ExpressionContext* TorqueParser::expression() {
  ExpressionContext* _localctx =
      _tracker.createInstance<ExpressionContext>(_ctx, getState());
  enterRule(_localctx, 20, TorqueParser::RuleExpression);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(226);
    conditionalExpression(0);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConditionalExpressionContext
//------------------------------------------------------------------

TorqueParser::ConditionalExpressionContext::ConditionalExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

std::vector<TorqueParser::LogicalORExpressionContext*>
TorqueParser::ConditionalExpressionContext::logicalORExpression() {
  return getRuleContexts<TorqueParser::LogicalORExpressionContext>();
}

TorqueParser::LogicalORExpressionContext*
TorqueParser::ConditionalExpressionContext::logicalORExpression(size_t i) {
  return getRuleContext<TorqueParser::LogicalORExpressionContext>(i);
}

TorqueParser::ConditionalExpressionContext*
TorqueParser::ConditionalExpressionContext::conditionalExpression() {
  return getRuleContext<TorqueParser::ConditionalExpressionContext>(0);
}

size_t TorqueParser::ConditionalExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleConditionalExpression;
}

void TorqueParser::ConditionalExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->enterConditionalExpression(this);
}

void TorqueParser::ConditionalExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->exitConditionalExpression(this);
}

antlrcpp::Any TorqueParser::ConditionalExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitConditionalExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ConditionalExpressionContext*
TorqueParser::conditionalExpression() {
  return conditionalExpression(0);
}

TorqueParser::ConditionalExpressionContext* TorqueParser::conditionalExpression(
    int precedence) {
  ParserRuleContext* parentContext = _ctx;
  size_t parentState = getState();
  TorqueParser::ConditionalExpressionContext* _localctx =
      _tracker.createInstance<ConditionalExpressionContext>(_ctx, parentState);
  TorqueParser::ConditionalExpressionContext* previousContext = _localctx;
  size_t startState = 22;
  enterRecursionRule(_localctx, 22, TorqueParser::RuleConditionalExpression,
                     precedence);

  auto onExit = finally([=] { unrollRecursionContexts(parentContext); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(229);
    logicalORExpression(0);
    _ctx->stop = _input->LT(-1);
    setState(239);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 17,
                                                                     _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty()) triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<ConditionalExpressionContext>(
            parentContext, parentState);
        pushNewRecursionContext(_localctx, startState,
                                RuleConditionalExpression);
        setState(231);

        if (!(precpred(_ctx, 1)))
          throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(232);
        match(TorqueParser::T__4);
        setState(233);
        logicalORExpression(0);
        setState(234);
        match(TorqueParser::T__3);
        setState(235);
        logicalORExpression(0);
      }
      setState(241);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 17, _ctx);
    }
  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- LogicalORExpressionContext
//------------------------------------------------------------------

TorqueParser::LogicalORExpressionContext::LogicalORExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::LogicalANDExpressionContext*
TorqueParser::LogicalORExpressionContext::logicalANDExpression() {
  return getRuleContext<TorqueParser::LogicalANDExpressionContext>(0);
}

TorqueParser::LogicalORExpressionContext*
TorqueParser::LogicalORExpressionContext::logicalORExpression() {
  return getRuleContext<TorqueParser::LogicalORExpressionContext>(0);
}

size_t TorqueParser::LogicalORExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleLogicalORExpression;
}

void TorqueParser::LogicalORExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterLogicalORExpression(this);
}

void TorqueParser::LogicalORExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitLogicalORExpression(this);
}

antlrcpp::Any TorqueParser::LogicalORExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitLogicalORExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::LogicalORExpressionContext* TorqueParser::logicalORExpression() {
  return logicalORExpression(0);
}

TorqueParser::LogicalORExpressionContext* TorqueParser::logicalORExpression(
    int precedence) {
  ParserRuleContext* parentContext = _ctx;
  size_t parentState = getState();
  TorqueParser::LogicalORExpressionContext* _localctx =
      _tracker.createInstance<LogicalORExpressionContext>(_ctx, parentState);
  TorqueParser::LogicalORExpressionContext* previousContext = _localctx;
  size_t startState = 24;
  enterRecursionRule(_localctx, 24, TorqueParser::RuleLogicalORExpression,
                     precedence);

  auto onExit = finally([=] { unrollRecursionContexts(parentContext); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(243);
    logicalANDExpression(0);
    _ctx->stop = _input->LT(-1);
    setState(250);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 18,
                                                                     _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty()) triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<LogicalORExpressionContext>(
            parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleLogicalORExpression);
        setState(245);

        if (!(precpred(_ctx, 1)))
          throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(246);
        match(TorqueParser::T__5);
        setState(247);
        logicalANDExpression(0);
      }
      setState(252);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 18, _ctx);
    }
  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- LogicalANDExpressionContext
//------------------------------------------------------------------

TorqueParser::LogicalANDExpressionContext::LogicalANDExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::BitwiseExpressionContext*
TorqueParser::LogicalANDExpressionContext::bitwiseExpression() {
  return getRuleContext<TorqueParser::BitwiseExpressionContext>(0);
}

TorqueParser::LogicalANDExpressionContext*
TorqueParser::LogicalANDExpressionContext::logicalANDExpression() {
  return getRuleContext<TorqueParser::LogicalANDExpressionContext>(0);
}

size_t TorqueParser::LogicalANDExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleLogicalANDExpression;
}

void TorqueParser::LogicalANDExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->enterLogicalANDExpression(this);
}

void TorqueParser::LogicalANDExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitLogicalANDExpression(this);
}

antlrcpp::Any TorqueParser::LogicalANDExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitLogicalANDExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::LogicalANDExpressionContext*
TorqueParser::logicalANDExpression() {
  return logicalANDExpression(0);
}

TorqueParser::LogicalANDExpressionContext* TorqueParser::logicalANDExpression(
    int precedence) {
  ParserRuleContext* parentContext = _ctx;
  size_t parentState = getState();
  TorqueParser::LogicalANDExpressionContext* _localctx =
      _tracker.createInstance<LogicalANDExpressionContext>(_ctx, parentState);
  TorqueParser::LogicalANDExpressionContext* previousContext = _localctx;
  size_t startState = 26;
  enterRecursionRule(_localctx, 26, TorqueParser::RuleLogicalANDExpression,
                     precedence);

  auto onExit = finally([=] { unrollRecursionContexts(parentContext); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(254);
    bitwiseExpression(0);
    _ctx->stop = _input->LT(-1);
    setState(261);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 19,
                                                                     _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty()) triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<LogicalANDExpressionContext>(
            parentContext, parentState);
        pushNewRecursionContext(_localctx, startState,
                                RuleLogicalANDExpression);
        setState(256);

        if (!(precpred(_ctx, 1)))
          throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(257);
        match(TorqueParser::T__6);
        setState(258);
        bitwiseExpression(0);
      }
      setState(263);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 19, _ctx);
    }
  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- BitwiseExpressionContext
//------------------------------------------------------------------

TorqueParser::BitwiseExpressionContext::BitwiseExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::EqualityExpressionContext*
TorqueParser::BitwiseExpressionContext::equalityExpression() {
  return getRuleContext<TorqueParser::EqualityExpressionContext>(0);
}

TorqueParser::BitwiseExpressionContext*
TorqueParser::BitwiseExpressionContext::bitwiseExpression() {
  return getRuleContext<TorqueParser::BitwiseExpressionContext>(0);
}

tree::TerminalNode* TorqueParser::BitwiseExpressionContext::BIT_AND() {
  return getToken(TorqueParser::BIT_AND, 0);
}

tree::TerminalNode* TorqueParser::BitwiseExpressionContext::BIT_OR() {
  return getToken(TorqueParser::BIT_OR, 0);
}

size_t TorqueParser::BitwiseExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleBitwiseExpression;
}

void TorqueParser::BitwiseExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterBitwiseExpression(this);
}

void TorqueParser::BitwiseExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitBitwiseExpression(this);
}

antlrcpp::Any TorqueParser::BitwiseExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitBitwiseExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::BitwiseExpressionContext* TorqueParser::bitwiseExpression() {
  return bitwiseExpression(0);
}

TorqueParser::BitwiseExpressionContext* TorqueParser::bitwiseExpression(
    int precedence) {
  ParserRuleContext* parentContext = _ctx;
  size_t parentState = getState();
  TorqueParser::BitwiseExpressionContext* _localctx =
      _tracker.createInstance<BitwiseExpressionContext>(_ctx, parentState);
  TorqueParser::BitwiseExpressionContext* previousContext = _localctx;
  size_t startState = 28;
  enterRecursionRule(_localctx, 28, TorqueParser::RuleBitwiseExpression,
                     precedence);

  size_t _la = 0;

  auto onExit = finally([=] { unrollRecursionContexts(parentContext); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(265);
    equalityExpression(0);
    _ctx->stop = _input->LT(-1);
    setState(272);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 20,
                                                                     _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty()) triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<BitwiseExpressionContext>(
            parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleBitwiseExpression);
        setState(267);

        if (!(precpred(_ctx, 1)))
          throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(268);
        dynamic_cast<BitwiseExpressionContext*>(_localctx)->op = _input->LT(1);
        _la = _input->LA(1);
        if (!(_la == TorqueParser::BIT_OR

              || _la == TorqueParser::BIT_AND)) {
          dynamic_cast<BitwiseExpressionContext*>(_localctx)->op =
              _errHandler->recoverInline(this);
        } else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(269);
        equalityExpression(0);
      }
      setState(274);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 20, _ctx);
    }
  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- EqualityExpressionContext
//------------------------------------------------------------------

TorqueParser::EqualityExpressionContext::EqualityExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::RelationalExpressionContext*
TorqueParser::EqualityExpressionContext::relationalExpression() {
  return getRuleContext<TorqueParser::RelationalExpressionContext>(0);
}

TorqueParser::EqualityExpressionContext*
TorqueParser::EqualityExpressionContext::equalityExpression() {
  return getRuleContext<TorqueParser::EqualityExpressionContext>(0);
}

tree::TerminalNode* TorqueParser::EqualityExpressionContext::EQUAL() {
  return getToken(TorqueParser::EQUAL, 0);
}

tree::TerminalNode* TorqueParser::EqualityExpressionContext::NOT_EQUAL() {
  return getToken(TorqueParser::NOT_EQUAL, 0);
}

size_t TorqueParser::EqualityExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleEqualityExpression;
}

void TorqueParser::EqualityExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterEqualityExpression(this);
}

void TorqueParser::EqualityExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitEqualityExpression(this);
}

antlrcpp::Any TorqueParser::EqualityExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitEqualityExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::EqualityExpressionContext* TorqueParser::equalityExpression() {
  return equalityExpression(0);
}

TorqueParser::EqualityExpressionContext* TorqueParser::equalityExpression(
    int precedence) {
  ParserRuleContext* parentContext = _ctx;
  size_t parentState = getState();
  TorqueParser::EqualityExpressionContext* _localctx =
      _tracker.createInstance<EqualityExpressionContext>(_ctx, parentState);
  TorqueParser::EqualityExpressionContext* previousContext = _localctx;
  size_t startState = 30;
  enterRecursionRule(_localctx, 30, TorqueParser::RuleEqualityExpression,
                     precedence);

  size_t _la = 0;

  auto onExit = finally([=] { unrollRecursionContexts(parentContext); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(276);
    relationalExpression(0);
    _ctx->stop = _input->LT(-1);
    setState(283);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 21,
                                                                     _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty()) triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EqualityExpressionContext>(
            parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEqualityExpression);
        setState(278);

        if (!(precpred(_ctx, 1)))
          throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(279);
        dynamic_cast<EqualityExpressionContext*>(_localctx)->op = _input->LT(1);
        _la = _input->LA(1);
        if (!(_la == TorqueParser::EQUAL

              || _la == TorqueParser::NOT_EQUAL)) {
          dynamic_cast<EqualityExpressionContext*>(_localctx)->op =
              _errHandler->recoverInline(this);
        } else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(280);
        relationalExpression(0);
      }
      setState(285);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 21, _ctx);
    }
  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- RelationalExpressionContext
//------------------------------------------------------------------

TorqueParser::RelationalExpressionContext::RelationalExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::ShiftExpressionContext*
TorqueParser::RelationalExpressionContext::shiftExpression() {
  return getRuleContext<TorqueParser::ShiftExpressionContext>(0);
}

TorqueParser::RelationalExpressionContext*
TorqueParser::RelationalExpressionContext::relationalExpression() {
  return getRuleContext<TorqueParser::RelationalExpressionContext>(0);
}

tree::TerminalNode* TorqueParser::RelationalExpressionContext::LESS_THAN() {
  return getToken(TorqueParser::LESS_THAN, 0);
}

tree::TerminalNode*
TorqueParser::RelationalExpressionContext::LESS_THAN_EQUAL() {
  return getToken(TorqueParser::LESS_THAN_EQUAL, 0);
}

tree::TerminalNode* TorqueParser::RelationalExpressionContext::GREATER_THAN() {
  return getToken(TorqueParser::GREATER_THAN, 0);
}

tree::TerminalNode*
TorqueParser::RelationalExpressionContext::GREATER_THAN_EQUAL() {
  return getToken(TorqueParser::GREATER_THAN_EQUAL, 0);
}

size_t TorqueParser::RelationalExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleRelationalExpression;
}

void TorqueParser::RelationalExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->enterRelationalExpression(this);
}

void TorqueParser::RelationalExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitRelationalExpression(this);
}

antlrcpp::Any TorqueParser::RelationalExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitRelationalExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::RelationalExpressionContext*
TorqueParser::relationalExpression() {
  return relationalExpression(0);
}

TorqueParser::RelationalExpressionContext* TorqueParser::relationalExpression(
    int precedence) {
  ParserRuleContext* parentContext = _ctx;
  size_t parentState = getState();
  TorqueParser::RelationalExpressionContext* _localctx =
      _tracker.createInstance<RelationalExpressionContext>(_ctx, parentState);
  TorqueParser::RelationalExpressionContext* previousContext = _localctx;
  size_t startState = 32;
  enterRecursionRule(_localctx, 32, TorqueParser::RuleRelationalExpression,
                     precedence);

  size_t _la = 0;

  auto onExit = finally([=] { unrollRecursionContexts(parentContext); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(287);
    shiftExpression(0);
    _ctx->stop = _input->LT(-1);
    setState(294);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 22,
                                                                     _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty()) triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<RelationalExpressionContext>(
            parentContext, parentState);
        pushNewRecursionContext(_localctx, startState,
                                RuleRelationalExpression);
        setState(289);

        if (!(precpred(_ctx, 1)))
          throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(290);
        dynamic_cast<RelationalExpressionContext*>(_localctx)->op =
            _input->LT(1);
        _la = _input->LA(1);
        if (!(((((_la - 61) & ~0x3fULL) == 0) &&
               ((1ULL << (_la - 61)) &
                ((1ULL << (TorqueParser::LESS_THAN - 61)) |
                 (1ULL << (TorqueParser::LESS_THAN_EQUAL - 61)) |
                 (1ULL << (TorqueParser::GREATER_THAN - 61)) |
                 (1ULL << (TorqueParser::GREATER_THAN_EQUAL - 61)))) != 0))) {
          dynamic_cast<RelationalExpressionContext*>(_localctx)->op =
              _errHandler->recoverInline(this);
        } else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(291);
        shiftExpression(0);
      }
      setState(296);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 22, _ctx);
    }
  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- ShiftExpressionContext
//------------------------------------------------------------------

TorqueParser::ShiftExpressionContext::ShiftExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::AdditiveExpressionContext*
TorqueParser::ShiftExpressionContext::additiveExpression() {
  return getRuleContext<TorqueParser::AdditiveExpressionContext>(0);
}

TorqueParser::ShiftExpressionContext*
TorqueParser::ShiftExpressionContext::shiftExpression() {
  return getRuleContext<TorqueParser::ShiftExpressionContext>(0);
}

tree::TerminalNode* TorqueParser::ShiftExpressionContext::SHIFT_RIGHT() {
  return getToken(TorqueParser::SHIFT_RIGHT, 0);
}

tree::TerminalNode* TorqueParser::ShiftExpressionContext::SHIFT_LEFT() {
  return getToken(TorqueParser::SHIFT_LEFT, 0);
}

tree::TerminalNode*
TorqueParser::ShiftExpressionContext::SHIFT_RIGHT_ARITHMETIC() {
  return getToken(TorqueParser::SHIFT_RIGHT_ARITHMETIC, 0);
}

size_t TorqueParser::ShiftExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleShiftExpression;
}

void TorqueParser::ShiftExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterShiftExpression(this);
}

void TorqueParser::ShiftExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitShiftExpression(this);
}

antlrcpp::Any TorqueParser::ShiftExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitShiftExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ShiftExpressionContext* TorqueParser::shiftExpression() {
  return shiftExpression(0);
}

TorqueParser::ShiftExpressionContext* TorqueParser::shiftExpression(
    int precedence) {
  ParserRuleContext* parentContext = _ctx;
  size_t parentState = getState();
  TorqueParser::ShiftExpressionContext* _localctx =
      _tracker.createInstance<ShiftExpressionContext>(_ctx, parentState);
  TorqueParser::ShiftExpressionContext* previousContext = _localctx;
  size_t startState = 34;
  enterRecursionRule(_localctx, 34, TorqueParser::RuleShiftExpression,
                     precedence);

  size_t _la = 0;

  auto onExit = finally([=] { unrollRecursionContexts(parentContext); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(298);
    additiveExpression(0);
    _ctx->stop = _input->LT(-1);
    setState(305);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23,
                                                                     _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty()) triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<ShiftExpressionContext>(
            parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleShiftExpression);
        setState(300);

        if (!(precpred(_ctx, 1)))
          throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(301);
        dynamic_cast<ShiftExpressionContext*>(_localctx)->op = _input->LT(1);
        _la = _input->LA(1);
        if (!(((((_la - 65) & ~0x3fULL) == 0) &&
               ((1ULL << (_la - 65)) &
                ((1ULL << (TorqueParser::SHIFT_LEFT - 65)) |
                 (1ULL << (TorqueParser::SHIFT_RIGHT - 65)) |
                 (1ULL << (TorqueParser::SHIFT_RIGHT_ARITHMETIC - 65)))) !=
                   0))) {
          dynamic_cast<ShiftExpressionContext*>(_localctx)->op =
              _errHandler->recoverInline(this);
        } else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(302);
        additiveExpression(0);
      }
      setState(307);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 23, _ctx);
    }
  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- AdditiveExpressionContext
//------------------------------------------------------------------

TorqueParser::AdditiveExpressionContext::AdditiveExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::MultiplicativeExpressionContext*
TorqueParser::AdditiveExpressionContext::multiplicativeExpression() {
  return getRuleContext<TorqueParser::MultiplicativeExpressionContext>(0);
}

TorqueParser::AdditiveExpressionContext*
TorqueParser::AdditiveExpressionContext::additiveExpression() {
  return getRuleContext<TorqueParser::AdditiveExpressionContext>(0);
}

tree::TerminalNode* TorqueParser::AdditiveExpressionContext::PLUS() {
  return getToken(TorqueParser::PLUS, 0);
}

tree::TerminalNode* TorqueParser::AdditiveExpressionContext::MINUS() {
  return getToken(TorqueParser::MINUS, 0);
}

size_t TorqueParser::AdditiveExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleAdditiveExpression;
}

void TorqueParser::AdditiveExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterAdditiveExpression(this);
}

void TorqueParser::AdditiveExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitAdditiveExpression(this);
}

antlrcpp::Any TorqueParser::AdditiveExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitAdditiveExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::AdditiveExpressionContext* TorqueParser::additiveExpression() {
  return additiveExpression(0);
}

TorqueParser::AdditiveExpressionContext* TorqueParser::additiveExpression(
    int precedence) {
  ParserRuleContext* parentContext = _ctx;
  size_t parentState = getState();
  TorqueParser::AdditiveExpressionContext* _localctx =
      _tracker.createInstance<AdditiveExpressionContext>(_ctx, parentState);
  TorqueParser::AdditiveExpressionContext* previousContext = _localctx;
  size_t startState = 36;
  enterRecursionRule(_localctx, 36, TorqueParser::RuleAdditiveExpression,
                     precedence);

  size_t _la = 0;

  auto onExit = finally([=] { unrollRecursionContexts(parentContext); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(309);
    multiplicativeExpression(0);
    _ctx->stop = _input->LT(-1);
    setState(316);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 24,
                                                                     _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty()) triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<AdditiveExpressionContext>(
            parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleAdditiveExpression);
        setState(311);

        if (!(precpred(_ctx, 1)))
          throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(312);
        dynamic_cast<AdditiveExpressionContext*>(_localctx)->op = _input->LT(1);
        _la = _input->LA(1);
        if (!(_la == TorqueParser::PLUS

              || _la == TorqueParser::MINUS)) {
          dynamic_cast<AdditiveExpressionContext*>(_localctx)->op =
              _errHandler->recoverInline(this);
        } else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(313);
        multiplicativeExpression(0);
      }
      setState(318);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 24, _ctx);
    }
  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- MultiplicativeExpressionContext
//------------------------------------------------------------------

TorqueParser::MultiplicativeExpressionContext::MultiplicativeExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::UnaryExpressionContext*
TorqueParser::MultiplicativeExpressionContext::unaryExpression() {
  return getRuleContext<TorqueParser::UnaryExpressionContext>(0);
}

TorqueParser::MultiplicativeExpressionContext*
TorqueParser::MultiplicativeExpressionContext::multiplicativeExpression() {
  return getRuleContext<TorqueParser::MultiplicativeExpressionContext>(0);
}

tree::TerminalNode* TorqueParser::MultiplicativeExpressionContext::MULTIPLY() {
  return getToken(TorqueParser::MULTIPLY, 0);
}

tree::TerminalNode* TorqueParser::MultiplicativeExpressionContext::DIVIDE() {
  return getToken(TorqueParser::DIVIDE, 0);
}

tree::TerminalNode* TorqueParser::MultiplicativeExpressionContext::MODULO() {
  return getToken(TorqueParser::MODULO, 0);
}

size_t TorqueParser::MultiplicativeExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleMultiplicativeExpression;
}

void TorqueParser::MultiplicativeExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->enterMultiplicativeExpression(this);
}

void TorqueParser::MultiplicativeExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->exitMultiplicativeExpression(this);
}

antlrcpp::Any TorqueParser::MultiplicativeExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitMultiplicativeExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::MultiplicativeExpressionContext*
TorqueParser::multiplicativeExpression() {
  return multiplicativeExpression(0);
}

TorqueParser::MultiplicativeExpressionContext*
TorqueParser::multiplicativeExpression(int precedence) {
  ParserRuleContext* parentContext = _ctx;
  size_t parentState = getState();
  TorqueParser::MultiplicativeExpressionContext* _localctx =
      _tracker.createInstance<MultiplicativeExpressionContext>(_ctx,
                                                               parentState);
  TorqueParser::MultiplicativeExpressionContext* previousContext = _localctx;
  size_t startState = 38;
  enterRecursionRule(_localctx, 38, TorqueParser::RuleMultiplicativeExpression,
                     precedence);

  size_t _la = 0;

  auto onExit = finally([=] { unrollRecursionContexts(parentContext); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(320);
    unaryExpression();
    _ctx->stop = _input->LT(-1);
    setState(327);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25,
                                                                     _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty()) triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<MultiplicativeExpressionContext>(
            parentContext, parentState);
        pushNewRecursionContext(_localctx, startState,
                                RuleMultiplicativeExpression);
        setState(322);

        if (!(precpred(_ctx, 1)))
          throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(323);
        dynamic_cast<MultiplicativeExpressionContext*>(_localctx)->op =
            _input->LT(1);
        _la = _input->LA(1);
        if (!((((_la & ~0x3fULL) == 0) &&
               ((1ULL << _la) & ((1ULL << TorqueParser::MULTIPLY) |
                                 (1ULL << TorqueParser::DIVIDE) |
                                 (1ULL << TorqueParser::MODULO))) != 0))) {
          dynamic_cast<MultiplicativeExpressionContext*>(_localctx)->op =
              _errHandler->recoverInline(this);
        } else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(324);
        unaryExpression();
      }
      setState(329);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 25, _ctx);
    }
  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- UnaryExpressionContext
//------------------------------------------------------------------

TorqueParser::UnaryExpressionContext::UnaryExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::AssignmentExpressionContext*
TorqueParser::UnaryExpressionContext::assignmentExpression() {
  return getRuleContext<TorqueParser::AssignmentExpressionContext>(0);
}

TorqueParser::UnaryExpressionContext*
TorqueParser::UnaryExpressionContext::unaryExpression() {
  return getRuleContext<TorqueParser::UnaryExpressionContext>(0);
}

tree::TerminalNode* TorqueParser::UnaryExpressionContext::PLUS() {
  return getToken(TorqueParser::PLUS, 0);
}

tree::TerminalNode* TorqueParser::UnaryExpressionContext::MINUS() {
  return getToken(TorqueParser::MINUS, 0);
}

tree::TerminalNode* TorqueParser::UnaryExpressionContext::BIT_NOT() {
  return getToken(TorqueParser::BIT_NOT, 0);
}

tree::TerminalNode* TorqueParser::UnaryExpressionContext::NOT() {
  return getToken(TorqueParser::NOT, 0);
}

size_t TorqueParser::UnaryExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleUnaryExpression;
}

void TorqueParser::UnaryExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterUnaryExpression(this);
}

void TorqueParser::UnaryExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitUnaryExpression(this);
}

antlrcpp::Any TorqueParser::UnaryExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitUnaryExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::UnaryExpressionContext* TorqueParser::unaryExpression() {
  UnaryExpressionContext* _localctx =
      _tracker.createInstance<UnaryExpressionContext>(_ctx, getState());
  enterRule(_localctx, 40, TorqueParser::RuleUnaryExpression);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    setState(333);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case TorqueParser::T__0:
      case TorqueParser::CAST_KEYWORD:
      case TorqueParser::CONVERT_KEYWORD:
      case TorqueParser::MAX:
      case TorqueParser::MIN:
      case TorqueParser::INCREMENT:
      case TorqueParser::DECREMENT:
      case TorqueParser::STRING_LITERAL:
      case TorqueParser::IDENTIFIER:
      case TorqueParser::DECIMAL_LITERAL: {
        enterOuterAlt(_localctx, 1);
        setState(330);
        assignmentExpression();
        break;
      }

      case TorqueParser::PLUS:
      case TorqueParser::MINUS:
      case TorqueParser::BIT_NOT:
      case TorqueParser::NOT: {
        enterOuterAlt(_localctx, 2);
        setState(331);
        dynamic_cast<UnaryExpressionContext*>(_localctx)->op = _input->LT(1);
        _la = _input->LA(1);
        if (!(((((_la - 50) & ~0x3fULL) == 0) &&
               ((1ULL << (_la - 50)) & ((1ULL << (TorqueParser::PLUS - 50)) |
                                        (1ULL << (TorqueParser::MINUS - 50)) |
                                        (1ULL << (TorqueParser::BIT_NOT - 50)) |
                                        (1ULL << (TorqueParser::NOT - 50)))) !=
                   0))) {
          dynamic_cast<UnaryExpressionContext*>(_localctx)->op =
              _errHandler->recoverInline(this);
        } else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(332);
        unaryExpression();
        break;
      }

      default:
        throw NoViableAltException(this);
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LocationExpressionContext
//------------------------------------------------------------------

TorqueParser::LocationExpressionContext::LocationExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::LocationExpressionContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::LocationExpressionContext*
TorqueParser::LocationExpressionContext::locationExpression() {
  return getRuleContext<TorqueParser::LocationExpressionContext>(0);
}

TorqueParser::ExpressionContext*
TorqueParser::LocationExpressionContext::expression() {
  return getRuleContext<TorqueParser::ExpressionContext>(0);
}

size_t TorqueParser::LocationExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleLocationExpression;
}

void TorqueParser::LocationExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterLocationExpression(this);
}

void TorqueParser::LocationExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitLocationExpression(this);
}

antlrcpp::Any TorqueParser::LocationExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitLocationExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::LocationExpressionContext* TorqueParser::locationExpression() {
  return locationExpression(0);
}

TorqueParser::LocationExpressionContext* TorqueParser::locationExpression(
    int precedence) {
  ParserRuleContext* parentContext = _ctx;
  size_t parentState = getState();
  TorqueParser::LocationExpressionContext* _localctx =
      _tracker.createInstance<LocationExpressionContext>(_ctx, parentState);
  TorqueParser::LocationExpressionContext* previousContext = _localctx;
  size_t startState = 42;
  enterRecursionRule(_localctx, 42, TorqueParser::RuleLocationExpression,
                     precedence);

  auto onExit = finally([=] { unrollRecursionContexts(parentContext); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(336);
    match(TorqueParser::IDENTIFIER);
    _ctx->stop = _input->LT(-1);
    setState(348);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28,
                                                                     _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty()) triggerExitRuleEvent();
        previousContext = _localctx;
        setState(346);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
            _input, 27, _ctx)) {
          case 1: {
            _localctx = _tracker.createInstance<LocationExpressionContext>(
                parentContext, parentState);
            pushNewRecursionContext(_localctx, startState,
                                    RuleLocationExpression);
            setState(338);

            if (!(precpred(_ctx, 2)))
              throw FailedPredicateException(this, "precpred(_ctx, 2)");
            setState(339);
            match(TorqueParser::T__7);
            setState(340);
            match(TorqueParser::IDENTIFIER);
            break;
          }

          case 2: {
            _localctx = _tracker.createInstance<LocationExpressionContext>(
                parentContext, parentState);
            pushNewRecursionContext(_localctx, startState,
                                    RuleLocationExpression);
            setState(341);

            if (!(precpred(_ctx, 1)))
              throw FailedPredicateException(this, "precpred(_ctx, 1)");
            setState(342);
            match(TorqueParser::T__8);
            setState(343);
            expression();
            setState(344);
            match(TorqueParser::T__9);
            break;
          }
        }
      }
      setState(350);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 28, _ctx);
    }
  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- IncrementDecrementContext
//------------------------------------------------------------------

TorqueParser::IncrementDecrementContext::IncrementDecrementContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::IncrementDecrementContext::INCREMENT() {
  return getToken(TorqueParser::INCREMENT, 0);
}

TorqueParser::LocationExpressionContext*
TorqueParser::IncrementDecrementContext::locationExpression() {
  return getRuleContext<TorqueParser::LocationExpressionContext>(0);
}

tree::TerminalNode* TorqueParser::IncrementDecrementContext::DECREMENT() {
  return getToken(TorqueParser::DECREMENT, 0);
}

size_t TorqueParser::IncrementDecrementContext::getRuleIndex() const {
  return TorqueParser::RuleIncrementDecrement;
}

void TorqueParser::IncrementDecrementContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterIncrementDecrement(this);
}

void TorqueParser::IncrementDecrementContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitIncrementDecrement(this);
}

antlrcpp::Any TorqueParser::IncrementDecrementContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitIncrementDecrement(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::IncrementDecrementContext* TorqueParser::incrementDecrement() {
  IncrementDecrementContext* _localctx =
      _tracker.createInstance<IncrementDecrementContext>(_ctx, getState());
  enterRule(_localctx, 44, TorqueParser::RuleIncrementDecrement);

  auto onExit = finally([=] { exitRule(); });
  try {
    setState(361);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
        _input, 29, _ctx)) {
      case 1: {
        enterOuterAlt(_localctx, 1);
        setState(351);
        match(TorqueParser::INCREMENT);
        setState(352);
        locationExpression(0);
        break;
      }

      case 2: {
        enterOuterAlt(_localctx, 2);
        setState(353);
        match(TorqueParser::DECREMENT);
        setState(354);
        locationExpression(0);
        break;
      }

      case 3: {
        enterOuterAlt(_localctx, 3);
        setState(355);
        locationExpression(0);
        setState(356);
        dynamic_cast<IncrementDecrementContext*>(_localctx)->op =
            match(TorqueParser::INCREMENT);
        break;
      }

      case 4: {
        enterOuterAlt(_localctx, 4);
        setState(358);
        locationExpression(0);
        setState(359);
        dynamic_cast<IncrementDecrementContext*>(_localctx)->op =
            match(TorqueParser::DECREMENT);
        break;
      }
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AssignmentContext
//------------------------------------------------------------------

TorqueParser::AssignmentContext::AssignmentContext(ParserRuleContext* parent,
                                                   size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::IncrementDecrementContext*
TorqueParser::AssignmentContext::incrementDecrement() {
  return getRuleContext<TorqueParser::IncrementDecrementContext>(0);
}

TorqueParser::LocationExpressionContext*
TorqueParser::AssignmentContext::locationExpression() {
  return getRuleContext<TorqueParser::LocationExpressionContext>(0);
}

TorqueParser::ExpressionContext* TorqueParser::AssignmentContext::expression() {
  return getRuleContext<TorqueParser::ExpressionContext>(0);
}

tree::TerminalNode* TorqueParser::AssignmentContext::ASSIGNMENT() {
  return getToken(TorqueParser::ASSIGNMENT, 0);
}

tree::TerminalNode* TorqueParser::AssignmentContext::ASSIGNMENT_OPERATOR() {
  return getToken(TorqueParser::ASSIGNMENT_OPERATOR, 0);
}

size_t TorqueParser::AssignmentContext::getRuleIndex() const {
  return TorqueParser::RuleAssignment;
}

void TorqueParser::AssignmentContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterAssignment(this);
}

void TorqueParser::AssignmentContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitAssignment(this);
}

antlrcpp::Any TorqueParser::AssignmentContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitAssignment(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::AssignmentContext* TorqueParser::assignment() {
  AssignmentContext* _localctx =
      _tracker.createInstance<AssignmentContext>(_ctx, getState());
  enterRule(_localctx, 46, TorqueParser::RuleAssignment);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    setState(369);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
        _input, 31, _ctx)) {
      case 1: {
        enterOuterAlt(_localctx, 1);
        setState(363);
        incrementDecrement();
        break;
      }

      case 2: {
        enterOuterAlt(_localctx, 2);
        setState(364);
        locationExpression(0);
        setState(367);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
            _input, 30, _ctx)) {
          case 1: {
            setState(365);
            _la = _input->LA(1);
            if (!(_la == TorqueParser::ASSIGNMENT

                  || _la == TorqueParser::ASSIGNMENT_OPERATOR)) {
              _errHandler->recoverInline(this);
            } else {
              _errHandler->reportMatch(this);
              consume();
            }
            setState(366);
            expression();
            break;
          }
        }
        break;
      }
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AssignmentExpressionContext
//------------------------------------------------------------------

TorqueParser::AssignmentExpressionContext::AssignmentExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::PrimaryExpressionContext*
TorqueParser::AssignmentExpressionContext::primaryExpression() {
  return getRuleContext<TorqueParser::PrimaryExpressionContext>(0);
}

TorqueParser::AssignmentContext*
TorqueParser::AssignmentExpressionContext::assignment() {
  return getRuleContext<TorqueParser::AssignmentContext>(0);
}

size_t TorqueParser::AssignmentExpressionContext::getRuleIndex() const {
  return TorqueParser::RuleAssignmentExpression;
}

void TorqueParser::AssignmentExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->enterAssignmentExpression(this);
}

void TorqueParser::AssignmentExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitAssignmentExpression(this);
}

antlrcpp::Any TorqueParser::AssignmentExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitAssignmentExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::AssignmentExpressionContext*
TorqueParser::assignmentExpression() {
  AssignmentExpressionContext* _localctx =
      _tracker.createInstance<AssignmentExpressionContext>(_ctx, getState());
  enterRule(_localctx, 48, TorqueParser::RuleAssignmentExpression);

  auto onExit = finally([=] { exitRule(); });
  try {
    setState(373);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
        _input, 32, _ctx)) {
      case 1: {
        enterOuterAlt(_localctx, 1);
        setState(371);
        primaryExpression();
        break;
      }

      case 2: {
        enterOuterAlt(_localctx, 2);
        setState(372);
        assignment();
        break;
      }
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimaryExpressionContext
//------------------------------------------------------------------

TorqueParser::PrimaryExpressionContext::PrimaryExpressionContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::HelperCallContext*
TorqueParser::PrimaryExpressionContext::helperCall() {
  return getRuleContext<TorqueParser::HelperCallContext>(0);
}

tree::TerminalNode* TorqueParser::PrimaryExpressionContext::DECIMAL_LITERAL() {
  return getToken(TorqueParser::DECIMAL_LITERAL, 0);
}

tree::TerminalNode* TorqueParser::PrimaryExpressionContext::STRING_LITERAL() {
  return getToken(TorqueParser::STRING_LITERAL, 0);
}

tree::TerminalNode* TorqueParser::PrimaryExpressionContext::CAST_KEYWORD() {
  return getToken(TorqueParser::CAST_KEYWORD, 0);
}

TorqueParser::TypeContext* TorqueParser::PrimaryExpressionContext::type() {
  return getRuleContext<TorqueParser::TypeContext>(0);
}

TorqueParser::ExpressionContext*
TorqueParser::PrimaryExpressionContext::expression() {
  return getRuleContext<TorqueParser::ExpressionContext>(0);
}

tree::TerminalNode* TorqueParser::PrimaryExpressionContext::OTHERWISE() {
  return getToken(TorqueParser::OTHERWISE, 0);
}

tree::TerminalNode* TorqueParser::PrimaryExpressionContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

tree::TerminalNode* TorqueParser::PrimaryExpressionContext::CONVERT_KEYWORD() {
  return getToken(TorqueParser::CONVERT_KEYWORD, 0);
}

size_t TorqueParser::PrimaryExpressionContext::getRuleIndex() const {
  return TorqueParser::RulePrimaryExpression;
}

void TorqueParser::PrimaryExpressionContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterPrimaryExpression(this);
}

void TorqueParser::PrimaryExpressionContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitPrimaryExpression(this);
}

antlrcpp::Any TorqueParser::PrimaryExpressionContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitPrimaryExpression(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::PrimaryExpressionContext* TorqueParser::primaryExpression() {
  PrimaryExpressionContext* _localctx =
      _tracker.createInstance<PrimaryExpressionContext>(_ctx, getState());
  enterRule(_localctx, 50, TorqueParser::RulePrimaryExpression);

  auto onExit = finally([=] { exitRule(); });
  try {
    setState(400);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case TorqueParser::MAX:
      case TorqueParser::MIN:
      case TorqueParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(375);
        helperCall();
        break;
      }

      case TorqueParser::DECIMAL_LITERAL: {
        enterOuterAlt(_localctx, 2);
        setState(376);
        match(TorqueParser::DECIMAL_LITERAL);
        break;
      }

      case TorqueParser::STRING_LITERAL: {
        enterOuterAlt(_localctx, 3);
        setState(377);
        match(TorqueParser::STRING_LITERAL);
        break;
      }

      case TorqueParser::CAST_KEYWORD: {
        enterOuterAlt(_localctx, 4);
        setState(378);
        match(TorqueParser::CAST_KEYWORD);
        setState(379);
        match(TorqueParser::LESS_THAN);
        setState(380);
        type();
        setState(381);
        match(TorqueParser::GREATER_THAN);
        setState(382);
        match(TorqueParser::T__0);
        setState(383);
        expression();
        setState(384);
        match(TorqueParser::T__2);
        setState(385);
        match(TorqueParser::OTHERWISE);
        setState(386);
        match(TorqueParser::IDENTIFIER);
        break;
      }

      case TorqueParser::CONVERT_KEYWORD: {
        enterOuterAlt(_localctx, 5);
        setState(388);
        match(TorqueParser::CONVERT_KEYWORD);
        setState(389);
        match(TorqueParser::LESS_THAN);
        setState(390);
        type();
        setState(391);
        match(TorqueParser::GREATER_THAN);
        setState(392);
        match(TorqueParser::T__0);
        setState(393);
        expression();
        setState(394);
        match(TorqueParser::T__2);
        break;
      }

      case TorqueParser::T__0: {
        enterOuterAlt(_localctx, 6);
        setState(396);
        match(TorqueParser::T__0);
        setState(397);
        expression();
        setState(398);
        match(TorqueParser::T__2);
        break;
      }

      default:
        throw NoViableAltException(this);
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ForInitializationContext
//------------------------------------------------------------------

TorqueParser::ForInitializationContext::ForInitializationContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::VariableDeclarationWithInitializationContext* TorqueParser::
    ForInitializationContext::variableDeclarationWithInitialization() {
  return getRuleContext<
      TorqueParser::VariableDeclarationWithInitializationContext>(0);
}

size_t TorqueParser::ForInitializationContext::getRuleIndex() const {
  return TorqueParser::RuleForInitialization;
}

void TorqueParser::ForInitializationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterForInitialization(this);
}

void TorqueParser::ForInitializationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitForInitialization(this);
}

antlrcpp::Any TorqueParser::ForInitializationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitForInitialization(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ForInitializationContext* TorqueParser::forInitialization() {
  ForInitializationContext* _localctx =
      _tracker.createInstance<ForInitializationContext>(_ctx, getState());
  enterRule(_localctx, 52, TorqueParser::RuleForInitialization);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(403);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::LET) {
      setState(402);
      variableDeclarationWithInitialization();
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ForLoopContext
//------------------------------------------------------------------

TorqueParser::ForLoopContext::ForLoopContext(ParserRuleContext* parent,
                                             size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ForLoopContext::FOR() {
  return getToken(TorqueParser::FOR, 0);
}

TorqueParser::ForInitializationContext*
TorqueParser::ForLoopContext::forInitialization() {
  return getRuleContext<TorqueParser::ForInitializationContext>(0);
}

TorqueParser::ExpressionContext* TorqueParser::ForLoopContext::expression() {
  return getRuleContext<TorqueParser::ExpressionContext>(0);
}

TorqueParser::AssignmentContext* TorqueParser::ForLoopContext::assignment() {
  return getRuleContext<TorqueParser::AssignmentContext>(0);
}

TorqueParser::StatementBlockContext*
TorqueParser::ForLoopContext::statementBlock() {
  return getRuleContext<TorqueParser::StatementBlockContext>(0);
}

size_t TorqueParser::ForLoopContext::getRuleIndex() const {
  return TorqueParser::RuleForLoop;
}

void TorqueParser::ForLoopContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterForLoop(this);
}

void TorqueParser::ForLoopContext::exitRule(tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitForLoop(this);
}

antlrcpp::Any TorqueParser::ForLoopContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitForLoop(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ForLoopContext* TorqueParser::forLoop() {
  ForLoopContext* _localctx =
      _tracker.createInstance<ForLoopContext>(_ctx, getState());
  enterRule(_localctx, 54, TorqueParser::RuleForLoop);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(405);
    match(TorqueParser::FOR);
    setState(406);
    match(TorqueParser::T__0);
    setState(407);
    forInitialization();
    setState(408);
    match(TorqueParser::T__10);
    setState(409);
    expression();
    setState(410);
    match(TorqueParser::T__10);
    setState(411);
    assignment();
    setState(412);
    match(TorqueParser::T__2);
    setState(413);
    statementBlock();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RangeSpecifierContext
//------------------------------------------------------------------

TorqueParser::RangeSpecifierContext::RangeSpecifierContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

std::vector<TorqueParser::ExpressionContext*>
TorqueParser::RangeSpecifierContext::expression() {
  return getRuleContexts<TorqueParser::ExpressionContext>();
}

TorqueParser::ExpressionContext*
TorqueParser::RangeSpecifierContext::expression(size_t i) {
  return getRuleContext<TorqueParser::ExpressionContext>(i);
}

size_t TorqueParser::RangeSpecifierContext::getRuleIndex() const {
  return TorqueParser::RuleRangeSpecifier;
}

void TorqueParser::RangeSpecifierContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterRangeSpecifier(this);
}

void TorqueParser::RangeSpecifierContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitRangeSpecifier(this);
}

antlrcpp::Any TorqueParser::RangeSpecifierContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitRangeSpecifier(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::RangeSpecifierContext* TorqueParser::rangeSpecifier() {
  RangeSpecifierContext* _localctx =
      _tracker.createInstance<RangeSpecifierContext>(_ctx, getState());
  enterRule(_localctx, 56, TorqueParser::RuleRangeSpecifier);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(415);
    match(TorqueParser::T__8);
    setState(417);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~0x3fULL) == 0) &&
         ((1ULL << _la) &
          ((1ULL << TorqueParser::T__0) | (1ULL << TorqueParser::CAST_KEYWORD) |
           (1ULL << TorqueParser::CONVERT_KEYWORD) |
           (1ULL << TorqueParser::PLUS) | (1ULL << TorqueParser::MINUS) |
           (1ULL << TorqueParser::BIT_NOT) | (1ULL << TorqueParser::MAX) |
           (1ULL << TorqueParser::MIN))) != 0) ||
        ((((_la - 70) & ~0x3fULL) == 0) &&
         ((1ULL << (_la - 70)) &
          ((1ULL << (TorqueParser::INCREMENT - 70)) |
           (1ULL << (TorqueParser::DECREMENT - 70)) |
           (1ULL << (TorqueParser::NOT - 70)) |
           (1ULL << (TorqueParser::STRING_LITERAL - 70)) |
           (1ULL << (TorqueParser::IDENTIFIER - 70)) |
           (1ULL << (TorqueParser::DECIMAL_LITERAL - 70)))) != 0)) {
      setState(416);
      dynamic_cast<RangeSpecifierContext*>(_localctx)->begin = expression();
    }
    setState(419);
    match(TorqueParser::T__3);
    setState(421);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~0x3fULL) == 0) &&
         ((1ULL << _la) &
          ((1ULL << TorqueParser::T__0) | (1ULL << TorqueParser::CAST_KEYWORD) |
           (1ULL << TorqueParser::CONVERT_KEYWORD) |
           (1ULL << TorqueParser::PLUS) | (1ULL << TorqueParser::MINUS) |
           (1ULL << TorqueParser::BIT_NOT) | (1ULL << TorqueParser::MAX) |
           (1ULL << TorqueParser::MIN))) != 0) ||
        ((((_la - 70) & ~0x3fULL) == 0) &&
         ((1ULL << (_la - 70)) &
          ((1ULL << (TorqueParser::INCREMENT - 70)) |
           (1ULL << (TorqueParser::DECREMENT - 70)) |
           (1ULL << (TorqueParser::NOT - 70)) |
           (1ULL << (TorqueParser::STRING_LITERAL - 70)) |
           (1ULL << (TorqueParser::IDENTIFIER - 70)) |
           (1ULL << (TorqueParser::DECIMAL_LITERAL - 70)))) != 0)) {
      setState(420);
      dynamic_cast<RangeSpecifierContext*>(_localctx)->end = expression();
    }
    setState(423);
    match(TorqueParser::T__9);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ForOfRangeContext
//------------------------------------------------------------------

TorqueParser::ForOfRangeContext::ForOfRangeContext(ParserRuleContext* parent,
                                                   size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::RangeSpecifierContext*
TorqueParser::ForOfRangeContext::rangeSpecifier() {
  return getRuleContext<TorqueParser::RangeSpecifierContext>(0);
}

size_t TorqueParser::ForOfRangeContext::getRuleIndex() const {
  return TorqueParser::RuleForOfRange;
}

void TorqueParser::ForOfRangeContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterForOfRange(this);
}

void TorqueParser::ForOfRangeContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitForOfRange(this);
}

antlrcpp::Any TorqueParser::ForOfRangeContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitForOfRange(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ForOfRangeContext* TorqueParser::forOfRange() {
  ForOfRangeContext* _localctx =
      _tracker.createInstance<ForOfRangeContext>(_ctx, getState());
  enterRule(_localctx, 58, TorqueParser::RuleForOfRange);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(426);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::T__8) {
      setState(425);
      rangeSpecifier();
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ForOfLoopContext
//------------------------------------------------------------------

TorqueParser::ForOfLoopContext::ForOfLoopContext(ParserRuleContext* parent,
                                                 size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ForOfLoopContext::FOR() {
  return getToken(TorqueParser::FOR, 0);
}

TorqueParser::VariableDeclarationContext*
TorqueParser::ForOfLoopContext::variableDeclaration() {
  return getRuleContext<TorqueParser::VariableDeclarationContext>(0);
}

TorqueParser::ExpressionContext* TorqueParser::ForOfLoopContext::expression() {
  return getRuleContext<TorqueParser::ExpressionContext>(0);
}

TorqueParser::ForOfRangeContext* TorqueParser::ForOfLoopContext::forOfRange() {
  return getRuleContext<TorqueParser::ForOfRangeContext>(0);
}

TorqueParser::StatementBlockContext*
TorqueParser::ForOfLoopContext::statementBlock() {
  return getRuleContext<TorqueParser::StatementBlockContext>(0);
}

size_t TorqueParser::ForOfLoopContext::getRuleIndex() const {
  return TorqueParser::RuleForOfLoop;
}

void TorqueParser::ForOfLoopContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterForOfLoop(this);
}

void TorqueParser::ForOfLoopContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitForOfLoop(this);
}

antlrcpp::Any TorqueParser::ForOfLoopContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitForOfLoop(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ForOfLoopContext* TorqueParser::forOfLoop() {
  ForOfLoopContext* _localctx =
      _tracker.createInstance<ForOfLoopContext>(_ctx, getState());
  enterRule(_localctx, 60, TorqueParser::RuleForOfLoop);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(428);
    match(TorqueParser::FOR);
    setState(429);
    match(TorqueParser::T__0);
    setState(430);
    variableDeclaration();
    setState(431);
    match(TorqueParser::T__11);
    setState(432);
    expression();
    setState(433);
    forOfRange();
    setState(434);
    match(TorqueParser::T__2);
    setState(435);
    statementBlock();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArgumentContext
//------------------------------------------------------------------

TorqueParser::ArgumentContext::ArgumentContext(ParserRuleContext* parent,
                                               size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::ExpressionContext* TorqueParser::ArgumentContext::expression() {
  return getRuleContext<TorqueParser::ExpressionContext>(0);
}

size_t TorqueParser::ArgumentContext::getRuleIndex() const {
  return TorqueParser::RuleArgument;
}

void TorqueParser::ArgumentContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterArgument(this);
}

void TorqueParser::ArgumentContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitArgument(this);
}

antlrcpp::Any TorqueParser::ArgumentContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitArgument(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ArgumentContext* TorqueParser::argument() {
  ArgumentContext* _localctx =
      _tracker.createInstance<ArgumentContext>(_ctx, getState());
  enterRule(_localctx, 62, TorqueParser::RuleArgument);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(437);
    expression();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArgumentListContext
//------------------------------------------------------------------

TorqueParser::ArgumentListContext::ArgumentListContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

std::vector<TorqueParser::ArgumentContext*>
TorqueParser::ArgumentListContext::argument() {
  return getRuleContexts<TorqueParser::ArgumentContext>();
}

TorqueParser::ArgumentContext* TorqueParser::ArgumentListContext::argument(
    size_t i) {
  return getRuleContext<TorqueParser::ArgumentContext>(i);
}

size_t TorqueParser::ArgumentListContext::getRuleIndex() const {
  return TorqueParser::RuleArgumentList;
}

void TorqueParser::ArgumentListContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterArgumentList(this);
}

void TorqueParser::ArgumentListContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitArgumentList(this);
}

antlrcpp::Any TorqueParser::ArgumentListContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitArgumentList(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ArgumentListContext* TorqueParser::argumentList() {
  ArgumentListContext* _localctx =
      _tracker.createInstance<ArgumentListContext>(_ctx, getState());
  enterRule(_localctx, 64, TorqueParser::RuleArgumentList);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(439);
    match(TorqueParser::T__0);
    setState(441);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~0x3fULL) == 0) &&
         ((1ULL << _la) &
          ((1ULL << TorqueParser::T__0) | (1ULL << TorqueParser::CAST_KEYWORD) |
           (1ULL << TorqueParser::CONVERT_KEYWORD) |
           (1ULL << TorqueParser::PLUS) | (1ULL << TorqueParser::MINUS) |
           (1ULL << TorqueParser::BIT_NOT) | (1ULL << TorqueParser::MAX) |
           (1ULL << TorqueParser::MIN))) != 0) ||
        ((((_la - 70) & ~0x3fULL) == 0) &&
         ((1ULL << (_la - 70)) &
          ((1ULL << (TorqueParser::INCREMENT - 70)) |
           (1ULL << (TorqueParser::DECREMENT - 70)) |
           (1ULL << (TorqueParser::NOT - 70)) |
           (1ULL << (TorqueParser::STRING_LITERAL - 70)) |
           (1ULL << (TorqueParser::IDENTIFIER - 70)) |
           (1ULL << (TorqueParser::DECIMAL_LITERAL - 70)))) != 0)) {
      setState(440);
      argument();
    }
    setState(447);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == TorqueParser::T__1) {
      setState(443);
      match(TorqueParser::T__1);
      setState(444);
      argument();
      setState(449);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(450);
    match(TorqueParser::T__2);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- HelperCallContext
//------------------------------------------------------------------

TorqueParser::HelperCallContext::HelperCallContext(ParserRuleContext* parent,
                                                   size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::ArgumentListContext*
TorqueParser::HelperCallContext::argumentList() {
  return getRuleContext<TorqueParser::ArgumentListContext>(0);
}

TorqueParser::OptionalOtherwiseContext*
TorqueParser::HelperCallContext::optionalOtherwise() {
  return getRuleContext<TorqueParser::OptionalOtherwiseContext>(0);
}

tree::TerminalNode* TorqueParser::HelperCallContext::MIN() {
  return getToken(TorqueParser::MIN, 0);
}

tree::TerminalNode* TorqueParser::HelperCallContext::MAX() {
  return getToken(TorqueParser::MAX, 0);
}

tree::TerminalNode* TorqueParser::HelperCallContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

size_t TorqueParser::HelperCallContext::getRuleIndex() const {
  return TorqueParser::RuleHelperCall;
}

void TorqueParser::HelperCallContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterHelperCall(this);
}

void TorqueParser::HelperCallContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitHelperCall(this);
}

antlrcpp::Any TorqueParser::HelperCallContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitHelperCall(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::HelperCallContext* TorqueParser::helperCall() {
  HelperCallContext* _localctx =
      _tracker.createInstance<HelperCallContext>(_ctx, getState());
  enterRule(_localctx, 66, TorqueParser::RuleHelperCall);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(452);
    _la = _input->LA(1);
    if (!(((((_la - 58) & ~0x3fULL) == 0) &&
           ((1ULL << (_la - 58)) &
            ((1ULL << (TorqueParser::MAX - 58)) |
             (1ULL << (TorqueParser::MIN - 58)) |
             (1ULL << (TorqueParser::IDENTIFIER - 58)))) != 0))) {
      _errHandler->recoverInline(this);
    } else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(453);
    argumentList();
    setState(454);
    optionalOtherwise();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LabelReferenceContext
//------------------------------------------------------------------

TorqueParser::LabelReferenceContext::LabelReferenceContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::LabelReferenceContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

size_t TorqueParser::LabelReferenceContext::getRuleIndex() const {
  return TorqueParser::RuleLabelReference;
}

void TorqueParser::LabelReferenceContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterLabelReference(this);
}

void TorqueParser::LabelReferenceContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitLabelReference(this);
}

antlrcpp::Any TorqueParser::LabelReferenceContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitLabelReference(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::LabelReferenceContext* TorqueParser::labelReference() {
  LabelReferenceContext* _localctx =
      _tracker.createInstance<LabelReferenceContext>(_ctx, getState());
  enterRule(_localctx, 68, TorqueParser::RuleLabelReference);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(456);
    match(TorqueParser::IDENTIFIER);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VariableDeclarationContext
//------------------------------------------------------------------

TorqueParser::VariableDeclarationContext::VariableDeclarationContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::VariableDeclarationContext::LET() {
  return getToken(TorqueParser::LET, 0);
}

tree::TerminalNode* TorqueParser::VariableDeclarationContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::TypeContext* TorqueParser::VariableDeclarationContext::type() {
  return getRuleContext<TorqueParser::TypeContext>(0);
}

size_t TorqueParser::VariableDeclarationContext::getRuleIndex() const {
  return TorqueParser::RuleVariableDeclaration;
}

void TorqueParser::VariableDeclarationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterVariableDeclaration(this);
}

void TorqueParser::VariableDeclarationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitVariableDeclaration(this);
}

antlrcpp::Any TorqueParser::VariableDeclarationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitVariableDeclaration(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::VariableDeclarationContext* TorqueParser::variableDeclaration() {
  VariableDeclarationContext* _localctx =
      _tracker.createInstance<VariableDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 70, TorqueParser::RuleVariableDeclaration);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(458);
    match(TorqueParser::LET);
    setState(459);
    match(TorqueParser::IDENTIFIER);
    setState(460);
    match(TorqueParser::T__3);
    setState(461);
    type();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VariableDeclarationWithInitializationContext
//------------------------------------------------------------------

TorqueParser::VariableDeclarationWithInitializationContext::
    VariableDeclarationWithInitializationContext(ParserRuleContext* parent,
                                                 size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::VariableDeclarationContext* TorqueParser::
    VariableDeclarationWithInitializationContext::variableDeclaration() {
  return getRuleContext<TorqueParser::VariableDeclarationContext>(0);
}

TorqueParser::ExpressionContext*
TorqueParser::VariableDeclarationWithInitializationContext::expression() {
  return getRuleContext<TorqueParser::ExpressionContext>(0);
}

size_t
TorqueParser::VariableDeclarationWithInitializationContext::getRuleIndex()
    const {
  return TorqueParser::RuleVariableDeclarationWithInitialization;
}

void TorqueParser::VariableDeclarationWithInitializationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->enterVariableDeclarationWithInitialization(this);
}

void TorqueParser::VariableDeclarationWithInitializationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->exitVariableDeclarationWithInitialization(this);
}

antlrcpp::Any
TorqueParser::VariableDeclarationWithInitializationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitVariableDeclarationWithInitialization(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::VariableDeclarationWithInitializationContext*
TorqueParser::variableDeclarationWithInitialization() {
  VariableDeclarationWithInitializationContext* _localctx =
      _tracker.createInstance<VariableDeclarationWithInitializationContext>(
          _ctx, getState());
  enterRule(_localctx, 72,
            TorqueParser::RuleVariableDeclarationWithInitialization);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(463);
    variableDeclaration();
    setState(466);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::ASSIGNMENT) {
      setState(464);
      match(TorqueParser::ASSIGNMENT);
      setState(465);
      expression();
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- HelperCallStatementContext
//------------------------------------------------------------------

TorqueParser::HelperCallStatementContext::HelperCallStatementContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::HelperCallContext*
TorqueParser::HelperCallStatementContext::helperCall() {
  return getRuleContext<TorqueParser::HelperCallContext>(0);
}

tree::TerminalNode* TorqueParser::HelperCallStatementContext::TAIL() {
  return getToken(TorqueParser::TAIL, 0);
}

size_t TorqueParser::HelperCallStatementContext::getRuleIndex() const {
  return TorqueParser::RuleHelperCallStatement;
}

void TorqueParser::HelperCallStatementContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterHelperCallStatement(this);
}

void TorqueParser::HelperCallStatementContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitHelperCallStatement(this);
}

antlrcpp::Any TorqueParser::HelperCallStatementContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitHelperCallStatement(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::HelperCallStatementContext* TorqueParser::helperCallStatement() {
  HelperCallStatementContext* _localctx =
      _tracker.createInstance<HelperCallStatementContext>(_ctx, getState());
  enterRule(_localctx, 74, TorqueParser::RuleHelperCallStatement);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(469);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::TAIL) {
      setState(468);
      match(TorqueParser::TAIL);
    }
    setState(471);
    helperCall();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExpressionStatementContext
//------------------------------------------------------------------

TorqueParser::ExpressionStatementContext::ExpressionStatementContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::AssignmentContext*
TorqueParser::ExpressionStatementContext::assignment() {
  return getRuleContext<TorqueParser::AssignmentContext>(0);
}

size_t TorqueParser::ExpressionStatementContext::getRuleIndex() const {
  return TorqueParser::RuleExpressionStatement;
}

void TorqueParser::ExpressionStatementContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterExpressionStatement(this);
}

void TorqueParser::ExpressionStatementContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitExpressionStatement(this);
}

antlrcpp::Any TorqueParser::ExpressionStatementContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitExpressionStatement(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ExpressionStatementContext* TorqueParser::expressionStatement() {
  ExpressionStatementContext* _localctx =
      _tracker.createInstance<ExpressionStatementContext>(_ctx, getState());
  enterRule(_localctx, 76, TorqueParser::RuleExpressionStatement);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(473);
    assignment();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IfStatementContext
//------------------------------------------------------------------

TorqueParser::IfStatementContext::IfStatementContext(ParserRuleContext* parent,
                                                     size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::IfStatementContext::IF() {
  return getToken(TorqueParser::IF, 0);
}

TorqueParser::ExpressionContext*
TorqueParser::IfStatementContext::expression() {
  return getRuleContext<TorqueParser::ExpressionContext>(0);
}

std::vector<TorqueParser::StatementBlockContext*>
TorqueParser::IfStatementContext::statementBlock() {
  return getRuleContexts<TorqueParser::StatementBlockContext>();
}

TorqueParser::StatementBlockContext*
TorqueParser::IfStatementContext::statementBlock(size_t i) {
  return getRuleContext<TorqueParser::StatementBlockContext>(i);
}

size_t TorqueParser::IfStatementContext::getRuleIndex() const {
  return TorqueParser::RuleIfStatement;
}

void TorqueParser::IfStatementContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterIfStatement(this);
}

void TorqueParser::IfStatementContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitIfStatement(this);
}

antlrcpp::Any TorqueParser::IfStatementContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitIfStatement(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::IfStatementContext* TorqueParser::ifStatement() {
  IfStatementContext* _localctx =
      _tracker.createInstance<IfStatementContext>(_ctx, getState());
  enterRule(_localctx, 78, TorqueParser::RuleIfStatement);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(475);
    match(TorqueParser::IF);
    setState(476);
    match(TorqueParser::T__0);
    setState(477);
    expression();
    setState(478);
    match(TorqueParser::T__2);
    setState(479);
    statementBlock();
    setState(482);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
        _input, 42, _ctx)) {
      case 1: {
        setState(480);
        match(TorqueParser::T__12);
        setState(481);
        statementBlock();
        break;
      }
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- WhileLoopContext
//------------------------------------------------------------------

TorqueParser::WhileLoopContext::WhileLoopContext(ParserRuleContext* parent,
                                                 size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::WhileLoopContext::WHILE() {
  return getToken(TorqueParser::WHILE, 0);
}

TorqueParser::ExpressionContext* TorqueParser::WhileLoopContext::expression() {
  return getRuleContext<TorqueParser::ExpressionContext>(0);
}

TorqueParser::StatementBlockContext*
TorqueParser::WhileLoopContext::statementBlock() {
  return getRuleContext<TorqueParser::StatementBlockContext>(0);
}

size_t TorqueParser::WhileLoopContext::getRuleIndex() const {
  return TorqueParser::RuleWhileLoop;
}

void TorqueParser::WhileLoopContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterWhileLoop(this);
}

void TorqueParser::WhileLoopContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitWhileLoop(this);
}

antlrcpp::Any TorqueParser::WhileLoopContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitWhileLoop(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::WhileLoopContext* TorqueParser::whileLoop() {
  WhileLoopContext* _localctx =
      _tracker.createInstance<WhileLoopContext>(_ctx, getState());
  enterRule(_localctx, 80, TorqueParser::RuleWhileLoop);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(484);
    match(TorqueParser::WHILE);
    setState(485);
    match(TorqueParser::T__0);
    setState(486);
    expression();
    setState(487);
    match(TorqueParser::T__2);
    setState(488);
    statementBlock();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ReturnStatementContext
//------------------------------------------------------------------

TorqueParser::ReturnStatementContext::ReturnStatementContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ReturnStatementContext::RETURN() {
  return getToken(TorqueParser::RETURN, 0);
}

TorqueParser::ExpressionContext*
TorqueParser::ReturnStatementContext::expression() {
  return getRuleContext<TorqueParser::ExpressionContext>(0);
}

size_t TorqueParser::ReturnStatementContext::getRuleIndex() const {
  return TorqueParser::RuleReturnStatement;
}

void TorqueParser::ReturnStatementContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterReturnStatement(this);
}

void TorqueParser::ReturnStatementContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitReturnStatement(this);
}

antlrcpp::Any TorqueParser::ReturnStatementContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitReturnStatement(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ReturnStatementContext* TorqueParser::returnStatement() {
  ReturnStatementContext* _localctx =
      _tracker.createInstance<ReturnStatementContext>(_ctx, getState());
  enterRule(_localctx, 82, TorqueParser::RuleReturnStatement);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(490);
    match(TorqueParser::RETURN);
    setState(492);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~0x3fULL) == 0) &&
         ((1ULL << _la) &
          ((1ULL << TorqueParser::T__0) | (1ULL << TorqueParser::CAST_KEYWORD) |
           (1ULL << TorqueParser::CONVERT_KEYWORD) |
           (1ULL << TorqueParser::PLUS) | (1ULL << TorqueParser::MINUS) |
           (1ULL << TorqueParser::BIT_NOT) | (1ULL << TorqueParser::MAX) |
           (1ULL << TorqueParser::MIN))) != 0) ||
        ((((_la - 70) & ~0x3fULL) == 0) &&
         ((1ULL << (_la - 70)) &
          ((1ULL << (TorqueParser::INCREMENT - 70)) |
           (1ULL << (TorqueParser::DECREMENT - 70)) |
           (1ULL << (TorqueParser::NOT - 70)) |
           (1ULL << (TorqueParser::STRING_LITERAL - 70)) |
           (1ULL << (TorqueParser::IDENTIFIER - 70)) |
           (1ULL << (TorqueParser::DECIMAL_LITERAL - 70)))) != 0)) {
      setState(491);
      expression();
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BreakStatementContext
//------------------------------------------------------------------

TorqueParser::BreakStatementContext::BreakStatementContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::BreakStatementContext::BREAK() {
  return getToken(TorqueParser::BREAK, 0);
}

size_t TorqueParser::BreakStatementContext::getRuleIndex() const {
  return TorqueParser::RuleBreakStatement;
}

void TorqueParser::BreakStatementContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterBreakStatement(this);
}

void TorqueParser::BreakStatementContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitBreakStatement(this);
}

antlrcpp::Any TorqueParser::BreakStatementContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitBreakStatement(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::BreakStatementContext* TorqueParser::breakStatement() {
  BreakStatementContext* _localctx =
      _tracker.createInstance<BreakStatementContext>(_ctx, getState());
  enterRule(_localctx, 84, TorqueParser::RuleBreakStatement);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(494);
    match(TorqueParser::BREAK);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ContinueStatementContext
//------------------------------------------------------------------

TorqueParser::ContinueStatementContext::ContinueStatementContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ContinueStatementContext::CONTINUE() {
  return getToken(TorqueParser::CONTINUE, 0);
}

size_t TorqueParser::ContinueStatementContext::getRuleIndex() const {
  return TorqueParser::RuleContinueStatement;
}

void TorqueParser::ContinueStatementContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterContinueStatement(this);
}

void TorqueParser::ContinueStatementContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitContinueStatement(this);
}

antlrcpp::Any TorqueParser::ContinueStatementContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitContinueStatement(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ContinueStatementContext* TorqueParser::continueStatement() {
  ContinueStatementContext* _localctx =
      _tracker.createInstance<ContinueStatementContext>(_ctx, getState());
  enterRule(_localctx, 86, TorqueParser::RuleContinueStatement);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(496);
    match(TorqueParser::CONTINUE);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- GotoStatementContext
//------------------------------------------------------------------

TorqueParser::GotoStatementContext::GotoStatementContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::GotoStatementContext::GOTO() {
  return getToken(TorqueParser::GOTO, 0);
}

TorqueParser::LabelReferenceContext*
TorqueParser::GotoStatementContext::labelReference() {
  return getRuleContext<TorqueParser::LabelReferenceContext>(0);
}

TorqueParser::ArgumentListContext*
TorqueParser::GotoStatementContext::argumentList() {
  return getRuleContext<TorqueParser::ArgumentListContext>(0);
}

size_t TorqueParser::GotoStatementContext::getRuleIndex() const {
  return TorqueParser::RuleGotoStatement;
}

void TorqueParser::GotoStatementContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterGotoStatement(this);
}

void TorqueParser::GotoStatementContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitGotoStatement(this);
}

antlrcpp::Any TorqueParser::GotoStatementContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitGotoStatement(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::GotoStatementContext* TorqueParser::gotoStatement() {
  GotoStatementContext* _localctx =
      _tracker.createInstance<GotoStatementContext>(_ctx, getState());
  enterRule(_localctx, 88, TorqueParser::RuleGotoStatement);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(498);
    match(TorqueParser::GOTO);
    setState(499);
    labelReference();
    setState(501);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::T__0) {
      setState(500);
      argumentList();
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- HandlerWithStatementContext
//------------------------------------------------------------------

TorqueParser::HandlerWithStatementContext::HandlerWithStatementContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::StatementBlockContext*
TorqueParser::HandlerWithStatementContext::statementBlock() {
  return getRuleContext<TorqueParser::StatementBlockContext>(0);
}

tree::TerminalNode* TorqueParser::HandlerWithStatementContext::CATCH() {
  return getToken(TorqueParser::CATCH, 0);
}

tree::TerminalNode* TorqueParser::HandlerWithStatementContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

tree::TerminalNode* TorqueParser::HandlerWithStatementContext::LABEL() {
  return getToken(TorqueParser::LABEL, 0);
}

TorqueParser::LabelDeclarationContext*
TorqueParser::HandlerWithStatementContext::labelDeclaration() {
  return getRuleContext<TorqueParser::LabelDeclarationContext>(0);
}

size_t TorqueParser::HandlerWithStatementContext::getRuleIndex() const {
  return TorqueParser::RuleHandlerWithStatement;
}

void TorqueParser::HandlerWithStatementContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->enterHandlerWithStatement(this);
}

void TorqueParser::HandlerWithStatementContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitHandlerWithStatement(this);
}

antlrcpp::Any TorqueParser::HandlerWithStatementContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitHandlerWithStatement(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::HandlerWithStatementContext*
TorqueParser::handlerWithStatement() {
  HandlerWithStatementContext* _localctx =
      _tracker.createInstance<HandlerWithStatementContext>(_ctx, getState());
  enterRule(_localctx, 90, TorqueParser::RuleHandlerWithStatement);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(507);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case TorqueParser::CATCH: {
        setState(503);
        match(TorqueParser::CATCH);
        setState(504);
        match(TorqueParser::IDENTIFIER);
        break;
      }

      case TorqueParser::LABEL: {
        setState(505);
        match(TorqueParser::LABEL);
        setState(506);
        labelDeclaration();
        break;
      }

      default:
        throw NoViableAltException(this);
    }
    setState(509);
    statementBlock();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TryCatchContext
//------------------------------------------------------------------

TorqueParser::TryCatchContext::TryCatchContext(ParserRuleContext* parent,
                                               size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::TryCatchContext::TRY() {
  return getToken(TorqueParser::TRY, 0);
}

TorqueParser::StatementBlockContext*
TorqueParser::TryCatchContext::statementBlock() {
  return getRuleContext<TorqueParser::StatementBlockContext>(0);
}

std::vector<TorqueParser::HandlerWithStatementContext*>
TorqueParser::TryCatchContext::handlerWithStatement() {
  return getRuleContexts<TorqueParser::HandlerWithStatementContext>();
}

TorqueParser::HandlerWithStatementContext*
TorqueParser::TryCatchContext::handlerWithStatement(size_t i) {
  return getRuleContext<TorqueParser::HandlerWithStatementContext>(i);
}

size_t TorqueParser::TryCatchContext::getRuleIndex() const {
  return TorqueParser::RuleTryCatch;
}

void TorqueParser::TryCatchContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterTryCatch(this);
}

void TorqueParser::TryCatchContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitTryCatch(this);
}

antlrcpp::Any TorqueParser::TryCatchContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitTryCatch(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::TryCatchContext* TorqueParser::tryCatch() {
  TryCatchContext* _localctx =
      _tracker.createInstance<TryCatchContext>(_ctx, getState());
  enterRule(_localctx, 92, TorqueParser::RuleTryCatch);

  auto onExit = finally([=] { exitRule(); });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(511);
    match(TorqueParser::TRY);
    setState(512);
    statementBlock();
    setState(514);
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
          setState(513);
          handlerWithStatement();
          break;
        }

        default:
          throw NoViableAltException(this);
      }
      setState(516);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
          _input, 46, _ctx);
    } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StatementContext
//------------------------------------------------------------------

TorqueParser::StatementContext::StatementContext(ParserRuleContext* parent,
                                                 size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::VariableDeclarationWithInitializationContext*
TorqueParser::StatementContext::variableDeclarationWithInitialization() {
  return getRuleContext<
      TorqueParser::VariableDeclarationWithInitializationContext>(0);
}

TorqueParser::HelperCallStatementContext*
TorqueParser::StatementContext::helperCallStatement() {
  return getRuleContext<TorqueParser::HelperCallStatementContext>(0);
}

TorqueParser::ExpressionStatementContext*
TorqueParser::StatementContext::expressionStatement() {
  return getRuleContext<TorqueParser::ExpressionStatementContext>(0);
}

TorqueParser::ReturnStatementContext*
TorqueParser::StatementContext::returnStatement() {
  return getRuleContext<TorqueParser::ReturnStatementContext>(0);
}

TorqueParser::BreakStatementContext*
TorqueParser::StatementContext::breakStatement() {
  return getRuleContext<TorqueParser::BreakStatementContext>(0);
}

TorqueParser::ContinueStatementContext*
TorqueParser::StatementContext::continueStatement() {
  return getRuleContext<TorqueParser::ContinueStatementContext>(0);
}

TorqueParser::GotoStatementContext*
TorqueParser::StatementContext::gotoStatement() {
  return getRuleContext<TorqueParser::GotoStatementContext>(0);
}

TorqueParser::IfStatementContext*
TorqueParser::StatementContext::ifStatement() {
  return getRuleContext<TorqueParser::IfStatementContext>(0);
}

TorqueParser::WhileLoopContext* TorqueParser::StatementContext::whileLoop() {
  return getRuleContext<TorqueParser::WhileLoopContext>(0);
}

TorqueParser::ForOfLoopContext* TorqueParser::StatementContext::forOfLoop() {
  return getRuleContext<TorqueParser::ForOfLoopContext>(0);
}

TorqueParser::ForLoopContext* TorqueParser::StatementContext::forLoop() {
  return getRuleContext<TorqueParser::ForLoopContext>(0);
}

TorqueParser::TryCatchContext* TorqueParser::StatementContext::tryCatch() {
  return getRuleContext<TorqueParser::TryCatchContext>(0);
}

size_t TorqueParser::StatementContext::getRuleIndex() const {
  return TorqueParser::RuleStatement;
}

void TorqueParser::StatementContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterStatement(this);
}

void TorqueParser::StatementContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitStatement(this);
}

antlrcpp::Any TorqueParser::StatementContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitStatement(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::StatementContext* TorqueParser::statement() {
  StatementContext* _localctx =
      _tracker.createInstance<StatementContext>(_ctx, getState());
  enterRule(_localctx, 94, TorqueParser::RuleStatement);

  auto onExit = finally([=] { exitRule(); });
  try {
    setState(544);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
        _input, 47, _ctx)) {
      case 1: {
        enterOuterAlt(_localctx, 1);
        setState(518);
        variableDeclarationWithInitialization();
        setState(519);
        match(TorqueParser::T__10);
        break;
      }

      case 2: {
        enterOuterAlt(_localctx, 2);
        setState(521);
        helperCallStatement();
        setState(522);
        match(TorqueParser::T__10);
        break;
      }

      case 3: {
        enterOuterAlt(_localctx, 3);
        setState(524);
        expressionStatement();
        setState(525);
        match(TorqueParser::T__10);
        break;
      }

      case 4: {
        enterOuterAlt(_localctx, 4);
        setState(527);
        returnStatement();
        setState(528);
        match(TorqueParser::T__10);
        break;
      }

      case 5: {
        enterOuterAlt(_localctx, 5);
        setState(530);
        breakStatement();
        setState(531);
        match(TorqueParser::T__10);
        break;
      }

      case 6: {
        enterOuterAlt(_localctx, 6);
        setState(533);
        continueStatement();
        setState(534);
        match(TorqueParser::T__10);
        break;
      }

      case 7: {
        enterOuterAlt(_localctx, 7);
        setState(536);
        gotoStatement();
        setState(537);
        match(TorqueParser::T__10);
        break;
      }

      case 8: {
        enterOuterAlt(_localctx, 8);
        setState(539);
        ifStatement();
        break;
      }

      case 9: {
        enterOuterAlt(_localctx, 9);
        setState(540);
        whileLoop();
        break;
      }

      case 10: {
        enterOuterAlt(_localctx, 10);
        setState(541);
        forOfLoop();
        break;
      }

      case 11: {
        enterOuterAlt(_localctx, 11);
        setState(542);
        forLoop();
        break;
      }

      case 12: {
        enterOuterAlt(_localctx, 12);
        setState(543);
        tryCatch();
        break;
      }
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StatementListContext
//------------------------------------------------------------------

TorqueParser::StatementListContext::StatementListContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

std::vector<TorqueParser::StatementContext*>
TorqueParser::StatementListContext::statement() {
  return getRuleContexts<TorqueParser::StatementContext>();
}

TorqueParser::StatementContext* TorqueParser::StatementListContext::statement(
    size_t i) {
  return getRuleContext<TorqueParser::StatementContext>(i);
}

size_t TorqueParser::StatementListContext::getRuleIndex() const {
  return TorqueParser::RuleStatementList;
}

void TorqueParser::StatementListContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterStatementList(this);
}

void TorqueParser::StatementListContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitStatementList(this);
}

antlrcpp::Any TorqueParser::StatementListContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitStatementList(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::StatementListContext* TorqueParser::statementList() {
  StatementListContext* _localctx =
      _tracker.createInstance<StatementListContext>(_ctx, getState());
  enterRule(_localctx, 96, TorqueParser::RuleStatementList);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(549);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (((((_la - 29) & ~0x3fULL) == 0) &&
            ((1ULL << (_la - 29)) &
             ((1ULL << (TorqueParser::IF - 29)) |
              (1ULL << (TorqueParser::FOR - 29)) |
              (1ULL << (TorqueParser::WHILE - 29)) |
              (1ULL << (TorqueParser::RETURN - 29)) |
              (1ULL << (TorqueParser::CONTINUE - 29)) |
              (1ULL << (TorqueParser::BREAK - 29)) |
              (1ULL << (TorqueParser::GOTO - 29)) |
              (1ULL << (TorqueParser::TRY - 29)) |
              (1ULL << (TorqueParser::TAIL - 29)) |
              (1ULL << (TorqueParser::LET - 29)) |
              (1ULL << (TorqueParser::MAX - 29)) |
              (1ULL << (TorqueParser::MIN - 29)) |
              (1ULL << (TorqueParser::INCREMENT - 29)) |
              (1ULL << (TorqueParser::DECREMENT - 29)) |
              (1ULL << (TorqueParser::IDENTIFIER - 29)))) != 0)) {
      setState(546);
      statement();
      setState(551);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StatementScopeContext
//------------------------------------------------------------------

TorqueParser::StatementScopeContext::StatementScopeContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::StatementListContext*
TorqueParser::StatementScopeContext::statementList() {
  return getRuleContext<TorqueParser::StatementListContext>(0);
}

tree::TerminalNode* TorqueParser::StatementScopeContext::DEFERRED() {
  return getToken(TorqueParser::DEFERRED, 0);
}

size_t TorqueParser::StatementScopeContext::getRuleIndex() const {
  return TorqueParser::RuleStatementScope;
}

void TorqueParser::StatementScopeContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterStatementScope(this);
}

void TorqueParser::StatementScopeContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitStatementScope(this);
}

antlrcpp::Any TorqueParser::StatementScopeContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitStatementScope(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::StatementScopeContext* TorqueParser::statementScope() {
  StatementScopeContext* _localctx =
      _tracker.createInstance<StatementScopeContext>(_ctx, getState());
  enterRule(_localctx, 98, TorqueParser::RuleStatementScope);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(553);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::DEFERRED) {
      setState(552);
      match(TorqueParser::DEFERRED);
    }
    setState(555);
    match(TorqueParser::T__13);
    setState(556);
    statementList();
    setState(557);
    match(TorqueParser::T__14);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StatementBlockContext
//------------------------------------------------------------------

TorqueParser::StatementBlockContext::StatementBlockContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::StatementContext*
TorqueParser::StatementBlockContext::statement() {
  return getRuleContext<TorqueParser::StatementContext>(0);
}

TorqueParser::StatementScopeContext*
TorqueParser::StatementBlockContext::statementScope() {
  return getRuleContext<TorqueParser::StatementScopeContext>(0);
}

size_t TorqueParser::StatementBlockContext::getRuleIndex() const {
  return TorqueParser::RuleStatementBlock;
}

void TorqueParser::StatementBlockContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterStatementBlock(this);
}

void TorqueParser::StatementBlockContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitStatementBlock(this);
}

antlrcpp::Any TorqueParser::StatementBlockContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitStatementBlock(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::StatementBlockContext* TorqueParser::statementBlock() {
  StatementBlockContext* _localctx =
      _tracker.createInstance<StatementBlockContext>(_ctx, getState());
  enterRule(_localctx, 100, TorqueParser::RuleStatementBlock);

  auto onExit = finally([=] { exitRule(); });
  try {
    setState(561);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case TorqueParser::IF:
      case TorqueParser::FOR:
      case TorqueParser::WHILE:
      case TorqueParser::RETURN:
      case TorqueParser::CONTINUE:
      case TorqueParser::BREAK:
      case TorqueParser::GOTO:
      case TorqueParser::TRY:
      case TorqueParser::TAIL:
      case TorqueParser::LET:
      case TorqueParser::MAX:
      case TorqueParser::MIN:
      case TorqueParser::INCREMENT:
      case TorqueParser::DECREMENT:
      case TorqueParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(559);
        statement();
        break;
      }

      case TorqueParser::T__13:
      case TorqueParser::DEFERRED: {
        enterOuterAlt(_localctx, 2);
        setState(560);
        statementScope();
        break;
      }

      default:
        throw NoViableAltException(this);
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- HelperBodyContext
//------------------------------------------------------------------

TorqueParser::HelperBodyContext::HelperBodyContext(ParserRuleContext* parent,
                                                   size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::StatementScopeContext*
TorqueParser::HelperBodyContext::statementScope() {
  return getRuleContext<TorqueParser::StatementScopeContext>(0);
}

size_t TorqueParser::HelperBodyContext::getRuleIndex() const {
  return TorqueParser::RuleHelperBody;
}

void TorqueParser::HelperBodyContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterHelperBody(this);
}

void TorqueParser::HelperBodyContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitHelperBody(this);
}

antlrcpp::Any TorqueParser::HelperBodyContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitHelperBody(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::HelperBodyContext* TorqueParser::helperBody() {
  HelperBodyContext* _localctx =
      _tracker.createInstance<HelperBodyContext>(_ctx, getState());
  enterRule(_localctx, 102, TorqueParser::RuleHelperBody);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(563);
    statementScope();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- GeneratesDeclarationContext
//------------------------------------------------------------------

TorqueParser::GeneratesDeclarationContext::GeneratesDeclarationContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode*
TorqueParser::GeneratesDeclarationContext::STRING_LITERAL() {
  return getToken(TorqueParser::STRING_LITERAL, 0);
}

size_t TorqueParser::GeneratesDeclarationContext::getRuleIndex() const {
  return TorqueParser::RuleGeneratesDeclaration;
}

void TorqueParser::GeneratesDeclarationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr)
    parserListener->enterGeneratesDeclaration(this);
}

void TorqueParser::GeneratesDeclarationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitGeneratesDeclaration(this);
}

antlrcpp::Any TorqueParser::GeneratesDeclarationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitGeneratesDeclaration(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::GeneratesDeclarationContext*
TorqueParser::generatesDeclaration() {
  GeneratesDeclarationContext* _localctx =
      _tracker.createInstance<GeneratesDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 104, TorqueParser::RuleGeneratesDeclaration);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(565);
    match(TorqueParser::T__15);
    setState(566);
    match(TorqueParser::STRING_LITERAL);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExtendsDeclarationContext
//------------------------------------------------------------------

TorqueParser::ExtendsDeclarationContext::ExtendsDeclarationContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ExtendsDeclarationContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

size_t TorqueParser::ExtendsDeclarationContext::getRuleIndex() const {
  return TorqueParser::RuleExtendsDeclaration;
}

void TorqueParser::ExtendsDeclarationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterExtendsDeclaration(this);
}

void TorqueParser::ExtendsDeclarationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitExtendsDeclaration(this);
}

antlrcpp::Any TorqueParser::ExtendsDeclarationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitExtendsDeclaration(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ExtendsDeclarationContext* TorqueParser::extendsDeclaration() {
  ExtendsDeclarationContext* _localctx =
      _tracker.createInstance<ExtendsDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 106, TorqueParser::RuleExtendsDeclaration);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(568);
    match(TorqueParser::T__16);
    setState(569);
    match(TorqueParser::IDENTIFIER);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeDeclarationContext
//------------------------------------------------------------------

TorqueParser::TypeDeclarationContext::TypeDeclarationContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::TypeDeclarationContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::ExtendsDeclarationContext*
TorqueParser::TypeDeclarationContext::extendsDeclaration() {
  return getRuleContext<TorqueParser::ExtendsDeclarationContext>(0);
}

TorqueParser::GeneratesDeclarationContext*
TorqueParser::TypeDeclarationContext::generatesDeclaration() {
  return getRuleContext<TorqueParser::GeneratesDeclarationContext>(0);
}

size_t TorqueParser::TypeDeclarationContext::getRuleIndex() const {
  return TorqueParser::RuleTypeDeclaration;
}

void TorqueParser::TypeDeclarationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterTypeDeclaration(this);
}

void TorqueParser::TypeDeclarationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitTypeDeclaration(this);
}

antlrcpp::Any TorqueParser::TypeDeclarationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitTypeDeclaration(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::TypeDeclarationContext* TorqueParser::typeDeclaration() {
  TypeDeclarationContext* _localctx =
      _tracker.createInstance<TypeDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 108, TorqueParser::RuleTypeDeclaration);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(571);
    match(TorqueParser::T__17);
    setState(572);
    match(TorqueParser::IDENTIFIER);
    setState(574);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::T__16) {
      setState(573);
      extendsDeclaration();
    }
    setState(577);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::T__15) {
      setState(576);
      generatesDeclaration();
    }
    setState(579);
    match(TorqueParser::T__10);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExternalBuiltinContext
//------------------------------------------------------------------

TorqueParser::ExternalBuiltinContext::ExternalBuiltinContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ExternalBuiltinContext::BUILTIN() {
  return getToken(TorqueParser::BUILTIN, 0);
}

tree::TerminalNode* TorqueParser::ExternalBuiltinContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::TypeListContext*
TorqueParser::ExternalBuiltinContext::typeList() {
  return getRuleContext<TorqueParser::TypeListContext>(0);
}

TorqueParser::OptionalTypeContext*
TorqueParser::ExternalBuiltinContext::optionalType() {
  return getRuleContext<TorqueParser::OptionalTypeContext>(0);
}

tree::TerminalNode* TorqueParser::ExternalBuiltinContext::JAVASCRIPT() {
  return getToken(TorqueParser::JAVASCRIPT, 0);
}

size_t TorqueParser::ExternalBuiltinContext::getRuleIndex() const {
  return TorqueParser::RuleExternalBuiltin;
}

void TorqueParser::ExternalBuiltinContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterExternalBuiltin(this);
}

void TorqueParser::ExternalBuiltinContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitExternalBuiltin(this);
}

antlrcpp::Any TorqueParser::ExternalBuiltinContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitExternalBuiltin(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ExternalBuiltinContext* TorqueParser::externalBuiltin() {
  ExternalBuiltinContext* _localctx =
      _tracker.createInstance<ExternalBuiltinContext>(_ctx, getState());
  enterRule(_localctx, 110, TorqueParser::RuleExternalBuiltin);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(581);
    match(TorqueParser::T__18);
    setState(583);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::JAVASCRIPT) {
      setState(582);
      match(TorqueParser::JAVASCRIPT);
    }
    setState(585);
    match(TorqueParser::BUILTIN);
    setState(586);
    match(TorqueParser::IDENTIFIER);
    setState(587);
    typeList();
    setState(588);
    optionalType();
    setState(589);
    match(TorqueParser::T__10);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExternalMacroContext
//------------------------------------------------------------------

TorqueParser::ExternalMacroContext::ExternalMacroContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ExternalMacroContext::MACRO() {
  return getToken(TorqueParser::MACRO, 0);
}

tree::TerminalNode* TorqueParser::ExternalMacroContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::TypeListMaybeVarArgsContext*
TorqueParser::ExternalMacroContext::typeListMaybeVarArgs() {
  return getRuleContext<TorqueParser::TypeListMaybeVarArgsContext>(0);
}

TorqueParser::OptionalTypeContext*
TorqueParser::ExternalMacroContext::optionalType() {
  return getRuleContext<TorqueParser::OptionalTypeContext>(0);
}

TorqueParser::OptionalLabelListContext*
TorqueParser::ExternalMacroContext::optionalLabelList() {
  return getRuleContext<TorqueParser::OptionalLabelListContext>(0);
}

tree::TerminalNode* TorqueParser::ExternalMacroContext::STRING_LITERAL() {
  return getToken(TorqueParser::STRING_LITERAL, 0);
}

tree::TerminalNode* TorqueParser::ExternalMacroContext::IMPLICIT() {
  return getToken(TorqueParser::IMPLICIT, 0);
}

size_t TorqueParser::ExternalMacroContext::getRuleIndex() const {
  return TorqueParser::RuleExternalMacro;
}

void TorqueParser::ExternalMacroContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterExternalMacro(this);
}

void TorqueParser::ExternalMacroContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitExternalMacro(this);
}

antlrcpp::Any TorqueParser::ExternalMacroContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitExternalMacro(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ExternalMacroContext* TorqueParser::externalMacro() {
  ExternalMacroContext* _localctx =
      _tracker.createInstance<ExternalMacroContext>(_ctx, getState());
  enterRule(_localctx, 112, TorqueParser::RuleExternalMacro);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(591);
    match(TorqueParser::T__18);
    setState(597);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::T__19

        || _la == TorqueParser::IMPLICIT) {
      setState(593);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == TorqueParser::IMPLICIT) {
        setState(592);
        match(TorqueParser::IMPLICIT);
      }
      setState(595);
      match(TorqueParser::T__19);
      setState(596);
      match(TorqueParser::STRING_LITERAL);
    }
    setState(599);
    match(TorqueParser::MACRO);
    setState(600);
    match(TorqueParser::IDENTIFIER);
    setState(601);
    typeListMaybeVarArgs();
    setState(602);
    optionalType();
    setState(603);
    optionalLabelList();
    setState(604);
    match(TorqueParser::T__10);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExternalRuntimeContext
//------------------------------------------------------------------

TorqueParser::ExternalRuntimeContext::ExternalRuntimeContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ExternalRuntimeContext::RUNTIME() {
  return getToken(TorqueParser::RUNTIME, 0);
}

tree::TerminalNode* TorqueParser::ExternalRuntimeContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::TypeListMaybeVarArgsContext*
TorqueParser::ExternalRuntimeContext::typeListMaybeVarArgs() {
  return getRuleContext<TorqueParser::TypeListMaybeVarArgsContext>(0);
}

TorqueParser::OptionalTypeContext*
TorqueParser::ExternalRuntimeContext::optionalType() {
  return getRuleContext<TorqueParser::OptionalTypeContext>(0);
}

size_t TorqueParser::ExternalRuntimeContext::getRuleIndex() const {
  return TorqueParser::RuleExternalRuntime;
}

void TorqueParser::ExternalRuntimeContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterExternalRuntime(this);
}

void TorqueParser::ExternalRuntimeContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitExternalRuntime(this);
}

antlrcpp::Any TorqueParser::ExternalRuntimeContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitExternalRuntime(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ExternalRuntimeContext* TorqueParser::externalRuntime() {
  ExternalRuntimeContext* _localctx =
      _tracker.createInstance<ExternalRuntimeContext>(_ctx, getState());
  enterRule(_localctx, 114, TorqueParser::RuleExternalRuntime);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(606);
    match(TorqueParser::T__18);
    setState(607);
    match(TorqueParser::RUNTIME);
    setState(608);
    match(TorqueParser::IDENTIFIER);
    setState(609);
    typeListMaybeVarArgs();
    setState(610);
    optionalType();
    setState(611);
    match(TorqueParser::T__10);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BuiltinDeclarationContext
//------------------------------------------------------------------

TorqueParser::BuiltinDeclarationContext::BuiltinDeclarationContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::BuiltinDeclarationContext::BUILTIN() {
  return getToken(TorqueParser::BUILTIN, 0);
}

tree::TerminalNode* TorqueParser::BuiltinDeclarationContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::ParameterListContext*
TorqueParser::BuiltinDeclarationContext::parameterList() {
  return getRuleContext<TorqueParser::ParameterListContext>(0);
}

TorqueParser::OptionalTypeContext*
TorqueParser::BuiltinDeclarationContext::optionalType() {
  return getRuleContext<TorqueParser::OptionalTypeContext>(0);
}

TorqueParser::HelperBodyContext*
TorqueParser::BuiltinDeclarationContext::helperBody() {
  return getRuleContext<TorqueParser::HelperBodyContext>(0);
}

tree::TerminalNode* TorqueParser::BuiltinDeclarationContext::JAVASCRIPT() {
  return getToken(TorqueParser::JAVASCRIPT, 0);
}

size_t TorqueParser::BuiltinDeclarationContext::getRuleIndex() const {
  return TorqueParser::RuleBuiltinDeclaration;
}

void TorqueParser::BuiltinDeclarationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterBuiltinDeclaration(this);
}

void TorqueParser::BuiltinDeclarationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitBuiltinDeclaration(this);
}

antlrcpp::Any TorqueParser::BuiltinDeclarationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitBuiltinDeclaration(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::BuiltinDeclarationContext* TorqueParser::builtinDeclaration() {
  BuiltinDeclarationContext* _localctx =
      _tracker.createInstance<BuiltinDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 116, TorqueParser::RuleBuiltinDeclaration);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(614);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == TorqueParser::JAVASCRIPT) {
      setState(613);
      match(TorqueParser::JAVASCRIPT);
    }
    setState(616);
    match(TorqueParser::BUILTIN);
    setState(617);
    match(TorqueParser::IDENTIFIER);
    setState(618);
    parameterList();
    setState(619);
    optionalType();
    setState(620);
    helperBody();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MacroDeclarationContext
//------------------------------------------------------------------

TorqueParser::MacroDeclarationContext::MacroDeclarationContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::MacroDeclarationContext::MACRO() {
  return getToken(TorqueParser::MACRO, 0);
}

tree::TerminalNode* TorqueParser::MacroDeclarationContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::ParameterListContext*
TorqueParser::MacroDeclarationContext::parameterList() {
  return getRuleContext<TorqueParser::ParameterListContext>(0);
}

TorqueParser::OptionalTypeContext*
TorqueParser::MacroDeclarationContext::optionalType() {
  return getRuleContext<TorqueParser::OptionalTypeContext>(0);
}

TorqueParser::OptionalLabelListContext*
TorqueParser::MacroDeclarationContext::optionalLabelList() {
  return getRuleContext<TorqueParser::OptionalLabelListContext>(0);
}

TorqueParser::HelperBodyContext*
TorqueParser::MacroDeclarationContext::helperBody() {
  return getRuleContext<TorqueParser::HelperBodyContext>(0);
}

size_t TorqueParser::MacroDeclarationContext::getRuleIndex() const {
  return TorqueParser::RuleMacroDeclaration;
}

void TorqueParser::MacroDeclarationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterMacroDeclaration(this);
}

void TorqueParser::MacroDeclarationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitMacroDeclaration(this);
}

antlrcpp::Any TorqueParser::MacroDeclarationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitMacroDeclaration(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::MacroDeclarationContext* TorqueParser::macroDeclaration() {
  MacroDeclarationContext* _localctx =
      _tracker.createInstance<MacroDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 118, TorqueParser::RuleMacroDeclaration);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(622);
    match(TorqueParser::MACRO);
    setState(623);
    match(TorqueParser::IDENTIFIER);
    setState(624);
    parameterList();
    setState(625);
    optionalType();
    setState(626);
    optionalLabelList();
    setState(627);
    helperBody();

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConstDeclarationContext
//------------------------------------------------------------------

TorqueParser::ConstDeclarationContext::ConstDeclarationContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ConstDeclarationContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

TorqueParser::TypeContext* TorqueParser::ConstDeclarationContext::type() {
  return getRuleContext<TorqueParser::TypeContext>(0);
}

tree::TerminalNode* TorqueParser::ConstDeclarationContext::STRING_LITERAL() {
  return getToken(TorqueParser::STRING_LITERAL, 0);
}

size_t TorqueParser::ConstDeclarationContext::getRuleIndex() const {
  return TorqueParser::RuleConstDeclaration;
}

void TorqueParser::ConstDeclarationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterConstDeclaration(this);
}

void TorqueParser::ConstDeclarationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitConstDeclaration(this);
}

antlrcpp::Any TorqueParser::ConstDeclarationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitConstDeclaration(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ConstDeclarationContext* TorqueParser::constDeclaration() {
  ConstDeclarationContext* _localctx =
      _tracker.createInstance<ConstDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 120, TorqueParser::RuleConstDeclaration);

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(629);
    match(TorqueParser::T__20);
    setState(630);
    match(TorqueParser::IDENTIFIER);
    setState(631);
    match(TorqueParser::T__3);
    setState(632);
    type();
    setState(633);
    match(TorqueParser::ASSIGNMENT);
    setState(634);
    match(TorqueParser::STRING_LITERAL);
    setState(635);
    match(TorqueParser::T__10);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DeclarationContext
//------------------------------------------------------------------

TorqueParser::DeclarationContext::DeclarationContext(ParserRuleContext* parent,
                                                     size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

TorqueParser::TypeDeclarationContext*
TorqueParser::DeclarationContext::typeDeclaration() {
  return getRuleContext<TorqueParser::TypeDeclarationContext>(0);
}

TorqueParser::BuiltinDeclarationContext*
TorqueParser::DeclarationContext::builtinDeclaration() {
  return getRuleContext<TorqueParser::BuiltinDeclarationContext>(0);
}

TorqueParser::MacroDeclarationContext*
TorqueParser::DeclarationContext::macroDeclaration() {
  return getRuleContext<TorqueParser::MacroDeclarationContext>(0);
}

TorqueParser::ExternalMacroContext*
TorqueParser::DeclarationContext::externalMacro() {
  return getRuleContext<TorqueParser::ExternalMacroContext>(0);
}

TorqueParser::ExternalBuiltinContext*
TorqueParser::DeclarationContext::externalBuiltin() {
  return getRuleContext<TorqueParser::ExternalBuiltinContext>(0);
}

TorqueParser::ExternalRuntimeContext*
TorqueParser::DeclarationContext::externalRuntime() {
  return getRuleContext<TorqueParser::ExternalRuntimeContext>(0);
}

TorqueParser::ConstDeclarationContext*
TorqueParser::DeclarationContext::constDeclaration() {
  return getRuleContext<TorqueParser::ConstDeclarationContext>(0);
}

size_t TorqueParser::DeclarationContext::getRuleIndex() const {
  return TorqueParser::RuleDeclaration;
}

void TorqueParser::DeclarationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterDeclaration(this);
}

void TorqueParser::DeclarationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitDeclaration(this);
}

antlrcpp::Any TorqueParser::DeclarationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitDeclaration(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::DeclarationContext* TorqueParser::declaration() {
  DeclarationContext* _localctx =
      _tracker.createInstance<DeclarationContext>(_ctx, getState());
  enterRule(_localctx, 122, TorqueParser::RuleDeclaration);

  auto onExit = finally([=] { exitRule(); });
  try {
    setState(644);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(
        _input, 57, _ctx)) {
      case 1: {
        enterOuterAlt(_localctx, 1);
        setState(637);
        typeDeclaration();
        break;
      }

      case 2: {
        enterOuterAlt(_localctx, 2);
        setState(638);
        builtinDeclaration();
        break;
      }

      case 3: {
        enterOuterAlt(_localctx, 3);
        setState(639);
        macroDeclaration();
        break;
      }

      case 4: {
        enterOuterAlt(_localctx, 4);
        setState(640);
        externalMacro();
        break;
      }

      case 5: {
        enterOuterAlt(_localctx, 5);
        setState(641);
        externalBuiltin();
        break;
      }

      case 6: {
        enterOuterAlt(_localctx, 6);
        setState(642);
        externalRuntime();
        break;
      }

      case 7: {
        enterOuterAlt(_localctx, 7);
        setState(643);
        constDeclaration();
        break;
      }
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ModuleDeclarationContext
//------------------------------------------------------------------

TorqueParser::ModuleDeclarationContext::ModuleDeclarationContext(
    ParserRuleContext* parent, size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

tree::TerminalNode* TorqueParser::ModuleDeclarationContext::MODULE() {
  return getToken(TorqueParser::MODULE, 0);
}

tree::TerminalNode* TorqueParser::ModuleDeclarationContext::IDENTIFIER() {
  return getToken(TorqueParser::IDENTIFIER, 0);
}

std::vector<TorqueParser::DeclarationContext*>
TorqueParser::ModuleDeclarationContext::declaration() {
  return getRuleContexts<TorqueParser::DeclarationContext>();
}

TorqueParser::DeclarationContext*
TorqueParser::ModuleDeclarationContext::declaration(size_t i) {
  return getRuleContext<TorqueParser::DeclarationContext>(i);
}

size_t TorqueParser::ModuleDeclarationContext::getRuleIndex() const {
  return TorqueParser::RuleModuleDeclaration;
}

void TorqueParser::ModuleDeclarationContext::enterRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterModuleDeclaration(this);
}

void TorqueParser::ModuleDeclarationContext::exitRule(
    tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitModuleDeclaration(this);
}

antlrcpp::Any TorqueParser::ModuleDeclarationContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitModuleDeclaration(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::ModuleDeclarationContext* TorqueParser::moduleDeclaration() {
  ModuleDeclarationContext* _localctx =
      _tracker.createInstance<ModuleDeclarationContext>(_ctx, getState());
  enterRule(_localctx, 124, TorqueParser::RuleModuleDeclaration);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(646);
    match(TorqueParser::MODULE);
    setState(647);
    match(TorqueParser::IDENTIFIER);
    setState(648);
    match(TorqueParser::T__13);
    setState(652);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~0x3fULL) == 0) &&
            ((1ULL << _la) &
             ((1ULL << TorqueParser::T__17) | (1ULL << TorqueParser::T__18) |
              (1ULL << TorqueParser::T__20) | (1ULL << TorqueParser::MACRO) |
              (1ULL << TorqueParser::BUILTIN) |
              (1ULL << TorqueParser::JAVASCRIPT))) != 0)) {
      setState(649);
      declaration();
      setState(654);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(655);
    match(TorqueParser::T__14);

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FileContext
//------------------------------------------------------------------

TorqueParser::FileContext::FileContext(ParserRuleContext* parent,
                                       size_t invokingState)
    : ParserRuleContext(parent, invokingState) {}

std::vector<TorqueParser::ModuleDeclarationContext*>
TorqueParser::FileContext::moduleDeclaration() {
  return getRuleContexts<TorqueParser::ModuleDeclarationContext>();
}

TorqueParser::ModuleDeclarationContext*
TorqueParser::FileContext::moduleDeclaration(size_t i) {
  return getRuleContext<TorqueParser::ModuleDeclarationContext>(i);
}

std::vector<TorqueParser::DeclarationContext*>
TorqueParser::FileContext::declaration() {
  return getRuleContexts<TorqueParser::DeclarationContext>();
}

TorqueParser::DeclarationContext* TorqueParser::FileContext::declaration(
    size_t i) {
  return getRuleContext<TorqueParser::DeclarationContext>(i);
}

size_t TorqueParser::FileContext::getRuleIndex() const {
  return TorqueParser::RuleFile;
}

void TorqueParser::FileContext::enterRule(tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->enterFile(this);
}

void TorqueParser::FileContext::exitRule(tree::ParseTreeListener* listener) {
  auto parserListener = dynamic_cast<TorqueListener*>(listener);
  if (parserListener != nullptr) parserListener->exitFile(this);
}

antlrcpp::Any TorqueParser::FileContext::accept(
    tree::ParseTreeVisitor* visitor) {
  if (auto parserVisitor = dynamic_cast<TorqueVisitor*>(visitor))
    return parserVisitor->visitFile(this);
  else
    return visitor->visitChildren(this);
}

TorqueParser::FileContext* TorqueParser::file() {
  FileContext* _localctx =
      _tracker.createInstance<FileContext>(_ctx, getState());
  enterRule(_localctx, 126, TorqueParser::RuleFile);
  size_t _la = 0;

  auto onExit = finally([=] { exitRule(); });
  try {
    enterOuterAlt(_localctx, 1);
    setState(661);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~0x3fULL) == 0) &&
            ((1ULL << _la) &
             ((1ULL << TorqueParser::T__17) | (1ULL << TorqueParser::T__18) |
              (1ULL << TorqueParser::T__20) | (1ULL << TorqueParser::MACRO) |
              (1ULL << TorqueParser::BUILTIN) | (1ULL << TorqueParser::MODULE) |
              (1ULL << TorqueParser::JAVASCRIPT))) != 0)) {
      setState(659);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case TorqueParser::MODULE: {
          setState(657);
          moduleDeclaration();
          break;
        }

        case TorqueParser::T__17:
        case TorqueParser::T__18:
        case TorqueParser::T__20:
        case TorqueParser::MACRO:
        case TorqueParser::BUILTIN:
        case TorqueParser::JAVASCRIPT: {
          setState(658);
          declaration();
          break;
        }

        default:
          throw NoViableAltException(this);
      }
      setState(663);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }

  } catch (RecognitionException& e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

bool TorqueParser::sempred(RuleContext* context, size_t ruleIndex,
                           size_t predicateIndex) {
  switch (ruleIndex) {
    case 11:
      return conditionalExpressionSempred(
          dynamic_cast<ConditionalExpressionContext*>(context), predicateIndex);
    case 12:
      return logicalORExpressionSempred(
          dynamic_cast<LogicalORExpressionContext*>(context), predicateIndex);
    case 13:
      return logicalANDExpressionSempred(
          dynamic_cast<LogicalANDExpressionContext*>(context), predicateIndex);
    case 14:
      return bitwiseExpressionSempred(
          dynamic_cast<BitwiseExpressionContext*>(context), predicateIndex);
    case 15:
      return equalityExpressionSempred(
          dynamic_cast<EqualityExpressionContext*>(context), predicateIndex);
    case 16:
      return relationalExpressionSempred(
          dynamic_cast<RelationalExpressionContext*>(context), predicateIndex);
    case 17:
      return shiftExpressionSempred(
          dynamic_cast<ShiftExpressionContext*>(context), predicateIndex);
    case 18:
      return additiveExpressionSempred(
          dynamic_cast<AdditiveExpressionContext*>(context), predicateIndex);
    case 19:
      return multiplicativeExpressionSempred(
          dynamic_cast<MultiplicativeExpressionContext*>(context),
          predicateIndex);
    case 21:
      return locationExpressionSempred(
          dynamic_cast<LocationExpressionContext*>(context), predicateIndex);

    default:
      break;
  }
  return true;
}

bool TorqueParser::conditionalExpressionSempred(
    ConditionalExpressionContext* _localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0:
      return precpred(_ctx, 1);

    default:
      break;
  }
  return true;
}

bool TorqueParser::logicalORExpressionSempred(
    LogicalORExpressionContext* _localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 1:
      return precpred(_ctx, 1);

    default:
      break;
  }
  return true;
}

bool TorqueParser::logicalANDExpressionSempred(
    LogicalANDExpressionContext* _localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 2:
      return precpred(_ctx, 1);

    default:
      break;
  }
  return true;
}

bool TorqueParser::bitwiseExpressionSempred(BitwiseExpressionContext* _localctx,
                                            size_t predicateIndex) {
  switch (predicateIndex) {
    case 3:
      return precpred(_ctx, 1);

    default:
      break;
  }
  return true;
}

bool TorqueParser::equalityExpressionSempred(
    EqualityExpressionContext* _localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 4:
      return precpred(_ctx, 1);

    default:
      break;
  }
  return true;
}

bool TorqueParser::relationalExpressionSempred(
    RelationalExpressionContext* _localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 5:
      return precpred(_ctx, 1);

    default:
      break;
  }
  return true;
}

bool TorqueParser::shiftExpressionSempred(ShiftExpressionContext* _localctx,
                                          size_t predicateIndex) {
  switch (predicateIndex) {
    case 6:
      return precpred(_ctx, 1);

    default:
      break;
  }
  return true;
}

bool TorqueParser::additiveExpressionSempred(
    AdditiveExpressionContext* _localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 7:
      return precpred(_ctx, 1);

    default:
      break;
  }
  return true;
}

bool TorqueParser::multiplicativeExpressionSempred(
    MultiplicativeExpressionContext* _localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 8:
      return precpred(_ctx, 1);

    default:
      break;
  }
  return true;
}

bool TorqueParser::locationExpressionSempred(
    LocationExpressionContext* _localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 9:
      return precpred(_ctx, 2);
    case 10:
      return precpred(_ctx, 1);

    default:
      break;
  }
  return true;
}

// Static vars and initialization.
std::vector<dfa::DFA> TorqueParser::_decisionToDFA;
atn::PredictionContextCache TorqueParser::_sharedContextCache;

// We own the ATN which in turn owns the ATN states.
atn::ATN TorqueParser::_atn;
std::vector<uint16_t> TorqueParser::_serializedATN;

std::vector<std::string> TorqueParser::_ruleNames = {
    "type",
    "typeList",
    "typeListMaybeVarArgs",
    "labelParameter",
    "optionalType",
    "optionalLabelList",
    "optionalOtherwise",
    "parameter",
    "parameterList",
    "labelDeclaration",
    "expression",
    "conditionalExpression",
    "logicalORExpression",
    "logicalANDExpression",
    "bitwiseExpression",
    "equalityExpression",
    "relationalExpression",
    "shiftExpression",
    "additiveExpression",
    "multiplicativeExpression",
    "unaryExpression",
    "locationExpression",
    "incrementDecrement",
    "assignment",
    "assignmentExpression",
    "primaryExpression",
    "forInitialization",
    "forLoop",
    "rangeSpecifier",
    "forOfRange",
    "forOfLoop",
    "argument",
    "argumentList",
    "helperCall",
    "labelReference",
    "variableDeclaration",
    "variableDeclarationWithInitialization",
    "helperCallStatement",
    "expressionStatement",
    "ifStatement",
    "whileLoop",
    "returnStatement",
    "breakStatement",
    "continueStatement",
    "gotoStatement",
    "handlerWithStatement",
    "tryCatch",
    "statement",
    "statementList",
    "statementScope",
    "statementBlock",
    "helperBody",
    "generatesDeclaration",
    "extendsDeclaration",
    "typeDeclaration",
    "externalBuiltin",
    "externalMacro",
    "externalRuntime",
    "builtinDeclaration",
    "macroDeclaration",
    "constDeclaration",
    "declaration",
    "moduleDeclaration",
    "file"};

std::vector<std::string> TorqueParser::_literalNames = {
    "",           "'('",         "','",
    "')'",        "':'",         "'?'",
    "'||'",       "'&&'",        "'.'",
    "'['",        "']'",         "';'",
    "'of'",       "'else'",      "'{'",
    "'}'",        "'generates'", "'extends'",
    "'type'",     "'extern'",    "'operator'",
    "'const'",    "'macro'",     "'builtin'",
    "'runtime'",  "'module'",    "'javascript'",
    "'implicit'", "'deferred'",  "'if'",
    "'cast'",     "'convert'",   "'for'",
    "'while'",    "'return'",    "'continue'",
    "'break'",    "'goto'",      "'otherwise'",
    "'try'",      "'catch'",     "'label'",
    "'labels'",   "'tail'",      "'isnt'",
    "'is'",       "'let'",       "'='",
    "",           "'=='",        "'+'",
    "'-'",        "'*'",         "'/'",
    "'%'",        "'|'",         "'&'",
    "'~'",        "'max'",       "'min'",
    "'!='",       "'<'",         "'<='",
    "'>'",        "'>='",        "'<<'",
    "'>>'",       "'>>>'",       "'...'",
    "",           "'++'",        "'--'",
    "'!'"};

std::vector<std::string> TorqueParser::_symbolicNames = {
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "MACRO",
    "BUILTIN",
    "RUNTIME",
    "MODULE",
    "JAVASCRIPT",
    "IMPLICIT",
    "DEFERRED",
    "IF",
    "CAST_KEYWORD",
    "CONVERT_KEYWORD",
    "FOR",
    "WHILE",
    "RETURN",
    "CONTINUE",
    "BREAK",
    "GOTO",
    "OTHERWISE",
    "TRY",
    "CATCH",
    "LABEL",
    "LABELS",
    "TAIL",
    "ISNT",
    "IS",
    "LET",
    "ASSIGNMENT",
    "ASSIGNMENT_OPERATOR",
    "EQUAL",
    "PLUS",
    "MINUS",
    "MULTIPLY",
    "DIVIDE",
    "MODULO",
    "BIT_OR",
    "BIT_AND",
    "BIT_NOT",
    "MAX",
    "MIN",
    "NOT_EQUAL",
    "LESS_THAN",
    "LESS_THAN_EQUAL",
    "GREATER_THAN",
    "GREATER_THAN_EQUAL",
    "SHIFT_LEFT",
    "SHIFT_RIGHT",
    "SHIFT_RIGHT_ARITHMETIC",
    "VARARGS",
    "EQUALITY_OPERATOR",
    "INCREMENT",
    "DECREMENT",
    "NOT",
    "STRING_LITERAL",
    "IDENTIFIER",
    "WS",
    "BLOCK_COMMENT",
    "LINE_COMMENT",
    "DECIMAL_LITERAL"};

dfa::Vocabulary TorqueParser::_vocabulary(_literalNames, _symbolicNames);

std::vector<std::string> TorqueParser::_tokenNames;

TorqueParser::Initializer::Initializer() {
  for (size_t i = 0; i < _symbolicNames.size(); ++i) {
    std::string name = _vocabulary.getLiteralName(i);
    if (name.empty()) {
      name = _vocabulary.getSymbolicName(i);
    }

    if (name.empty()) {
      _tokenNames.push_back("<INVALID>");
    } else {
      _tokenNames.push_back(name);
    }
  }

  _serializedATN = {
      0x3,   0x608b, 0xa72a, 0x8133, 0xb9ed, 0x417c, 0x3be7, 0x7786, 0x5964,
      0x3,   0x50,   0x29b,  0x4,    0x2,    0x9,    0x2,    0x4,    0x3,
      0x9,   0x3,    0x4,    0x4,    0x9,    0x4,    0x4,    0x5,    0x9,
      0x5,   0x4,    0x6,    0x9,    0x6,    0x4,    0x7,    0x9,    0x7,
      0x4,   0x8,    0x9,    0x8,    0x4,    0x9,    0x9,    0x9,    0x4,
      0xa,   0x9,    0xa,    0x4,    0xb,    0x9,    0xb,    0x4,    0xc,
      0x9,   0xc,    0x4,    0xd,    0x9,    0xd,    0x4,    0xe,    0x9,
      0xe,   0x4,    0xf,    0x9,    0xf,    0x4,    0x10,   0x9,    0x10,
      0x4,   0x11,   0x9,    0x11,   0x4,    0x12,   0x9,    0x12,   0x4,
      0x13,  0x9,    0x13,   0x4,    0x14,   0x9,    0x14,   0x4,    0x15,
      0x9,   0x15,   0x4,    0x16,   0x9,    0x16,   0x4,    0x17,   0x9,
      0x17,  0x4,    0x18,   0x9,    0x18,   0x4,    0x19,   0x9,    0x19,
      0x4,   0x1a,   0x9,    0x1a,   0x4,    0x1b,   0x9,    0x1b,   0x4,
      0x1c,  0x9,    0x1c,   0x4,    0x1d,   0x9,    0x1d,   0x4,    0x1e,
      0x9,   0x1e,   0x4,    0x1f,   0x9,    0x1f,   0x4,    0x20,   0x9,
      0x20,  0x4,    0x21,   0x9,    0x21,   0x4,    0x22,   0x9,    0x22,
      0x4,   0x23,   0x9,    0x23,   0x4,    0x24,   0x9,    0x24,   0x4,
      0x25,  0x9,    0x25,   0x4,    0x26,   0x9,    0x26,   0x4,    0x27,
      0x9,   0x27,   0x4,    0x28,   0x9,    0x28,   0x4,    0x29,   0x9,
      0x29,  0x4,    0x2a,   0x9,    0x2a,   0x4,    0x2b,   0x9,    0x2b,
      0x4,   0x2c,   0x9,    0x2c,   0x4,    0x2d,   0x9,    0x2d,   0x4,
      0x2e,  0x9,    0x2e,   0x4,    0x2f,   0x9,    0x2f,   0x4,    0x30,
      0x9,   0x30,   0x4,    0x31,   0x9,    0x31,   0x4,    0x32,   0x9,
      0x32,  0x4,    0x33,   0x9,    0x33,   0x4,    0x34,   0x9,    0x34,
      0x4,   0x35,   0x9,    0x35,   0x4,    0x36,   0x9,    0x36,   0x4,
      0x37,  0x9,    0x37,   0x4,    0x38,   0x9,    0x38,   0x4,    0x39,
      0x9,   0x39,   0x4,    0x3a,   0x9,    0x3a,   0x4,    0x3b,   0x9,
      0x3b,  0x4,    0x3c,   0x9,    0x3c,   0x4,    0x3d,   0x9,    0x3d,
      0x4,   0x3e,   0x9,    0x3e,   0x4,    0x3f,   0x9,    0x3f,   0x4,
      0x40,  0x9,    0x40,   0x4,    0x41,   0x9,    0x41,   0x3,    0x2,
      0x3,   0x2,    0x3,    0x3,    0x3,    0x3,    0x5,    0x3,    0x87,
      0xa,   0x3,    0x3,    0x3,    0x3,    0x3,    0x7,    0x3,    0x8b,
      0xa,   0x3,    0xc,    0x3,    0xe,    0x3,    0x8e,   0xb,    0x3,
      0x3,   0x3,    0x3,    0x3,    0x3,    0x4,    0x3,    0x4,    0x5,
      0x4,   0x94,   0xa,    0x4,    0x3,    0x4,    0x3,    0x4,    0x7,
      0x4,   0x98,   0xa,    0x4,    0xc,    0x4,    0xe,    0x4,    0x9b,
      0xb,   0x4,    0x3,    0x4,    0x3,    0x4,    0x5,    0x4,    0x9f,
      0xa,   0x4,    0x3,    0x4,    0x3,    0x4,    0x3,    0x4,    0x3,
      0x4,   0x5,    0x4,    0xa5,   0xa,    0x4,    0x3,    0x5,    0x3,
      0x5,   0x5,    0x5,    0xa9,   0xa,    0x5,    0x3,    0x6,    0x3,
      0x6,   0x5,    0x6,    0xad,   0xa,    0x6,    0x3,    0x7,    0x3,
      0x7,   0x3,    0x7,    0x3,    0x7,    0x7,    0x7,    0xb3,   0xa,
      0x7,   0xc,    0x7,    0xe,    0x7,    0xb6,   0xb,    0x7,    0x5,
      0x7,   0xb8,   0xa,    0x7,    0x3,    0x8,    0x3,    0x8,    0x3,
      0x8,   0x3,    0x8,    0x7,    0x8,    0xbe,   0xa,    0x8,    0xc,
      0x8,   0xe,    0x8,    0xc1,   0xb,    0x8,    0x5,    0x8,    0xc3,
      0xa,   0x8,    0x3,    0x9,    0x3,    0x9,    0x3,    0x9,    0x5,
      0x9,   0xc8,   0xa,    0x9,    0x3,    0xa,    0x3,    0xa,    0x5,
      0xa,   0xcc,   0xa,    0xa,    0x3,    0xa,    0x3,    0xa,    0x7,
      0xa,   0xd0,   0xa,    0xa,    0xc,    0xa,    0xe,    0xa,    0xd3,
      0xb,   0xa,    0x3,    0xa,    0x3,    0xa,    0x3,    0xa,    0x3,
      0xa,   0x3,    0xa,    0x3,    0xa,    0x3,    0xa,    0x3,    0xa,
      0x3,   0xa,    0x3,    0xa,    0x5,    0xa,    0xdf,   0xa,    0xa,
      0x3,   0xb,    0x3,    0xb,    0x5,    0xb,    0xe3,   0xa,    0xb,
      0x3,   0xc,    0x3,    0xc,    0x3,    0xd,    0x3,    0xd,    0x3,
      0xd,   0x3,    0xd,    0x3,    0xd,    0x3,    0xd,    0x3,    0xd,
      0x3,   0xd,    0x3,    0xd,    0x7,    0xd,    0xf0,   0xa,    0xd,
      0xc,   0xd,    0xe,    0xd,    0xf3,   0xb,    0xd,    0x3,    0xe,
      0x3,   0xe,    0x3,    0xe,    0x3,    0xe,    0x3,    0xe,    0x3,
      0xe,   0x7,    0xe,    0xfb,   0xa,    0xe,    0xc,    0xe,    0xe,
      0xe,   0xfe,   0xb,    0xe,    0x3,    0xf,    0x3,    0xf,    0x3,
      0xf,   0x3,    0xf,    0x3,    0xf,    0x3,    0xf,    0x7,    0xf,
      0x106, 0xa,    0xf,    0xc,    0xf,    0xe,    0xf,    0x109,  0xb,
      0xf,   0x3,    0x10,   0x3,    0x10,   0x3,    0x10,   0x3,    0x10,
      0x3,   0x10,   0x3,    0x10,   0x7,    0x10,   0x111,  0xa,    0x10,
      0xc,   0x10,   0xe,    0x10,   0x114,  0xb,    0x10,   0x3,    0x11,
      0x3,   0x11,   0x3,    0x11,   0x3,    0x11,   0x3,    0x11,   0x3,
      0x11,  0x7,    0x11,   0x11c,  0xa,    0x11,   0xc,    0x11,   0xe,
      0x11,  0x11f,  0xb,    0x11,   0x3,    0x12,   0x3,    0x12,   0x3,
      0x12,  0x3,    0x12,   0x3,    0x12,   0x3,    0x12,   0x7,    0x12,
      0x127, 0xa,    0x12,   0xc,    0x12,   0xe,    0x12,   0x12a,  0xb,
      0x12,  0x3,    0x13,   0x3,    0x13,   0x3,    0x13,   0x3,    0x13,
      0x3,   0x13,   0x3,    0x13,   0x7,    0x13,   0x132,  0xa,    0x13,
      0xc,   0x13,   0xe,    0x13,   0x135,  0xb,    0x13,   0x3,    0x14,
      0x3,   0x14,   0x3,    0x14,   0x3,    0x14,   0x3,    0x14,   0x3,
      0x14,  0x7,    0x14,   0x13d,  0xa,    0x14,   0xc,    0x14,   0xe,
      0x14,  0x140,  0xb,    0x14,   0x3,    0x15,   0x3,    0x15,   0x3,
      0x15,  0x3,    0x15,   0x3,    0x15,   0x3,    0x15,   0x7,    0x15,
      0x148, 0xa,    0x15,   0xc,    0x15,   0xe,    0x15,   0x14b,  0xb,
      0x15,  0x3,    0x16,   0x3,    0x16,   0x3,    0x16,   0x5,    0x16,
      0x150, 0xa,    0x16,   0x3,    0x17,   0x3,    0x17,   0x3,    0x17,
      0x3,   0x17,   0x3,    0x17,   0x3,    0x17,   0x3,    0x17,   0x3,
      0x17,  0x3,    0x17,   0x3,    0x17,   0x3,    0x17,   0x7,    0x17,
      0x15d, 0xa,    0x17,   0xc,    0x17,   0xe,    0x17,   0x160,  0xb,
      0x17,  0x3,    0x18,   0x3,    0x18,   0x3,    0x18,   0x3,    0x18,
      0x3,   0x18,   0x3,    0x18,   0x3,    0x18,   0x3,    0x18,   0x3,
      0x18,  0x3,    0x18,   0x5,    0x18,   0x16c,  0xa,    0x18,   0x3,
      0x19,  0x3,    0x19,   0x3,    0x19,   0x3,    0x19,   0x5,    0x19,
      0x172, 0xa,    0x19,   0x5,    0x19,   0x174,  0xa,    0x19,   0x3,
      0x1a,  0x3,    0x1a,   0x5,    0x1a,   0x178,  0xa,    0x1a,   0x3,
      0x1b,  0x3,    0x1b,   0x3,    0x1b,   0x3,    0x1b,   0x3,    0x1b,
      0x3,   0x1b,   0x3,    0x1b,   0x3,    0x1b,   0x3,    0x1b,   0x3,
      0x1b,  0x3,    0x1b,   0x3,    0x1b,   0x3,    0x1b,   0x3,    0x1b,
      0x3,   0x1b,   0x3,    0x1b,   0x3,    0x1b,   0x3,    0x1b,   0x3,
      0x1b,  0x3,    0x1b,   0x3,    0x1b,   0x3,    0x1b,   0x3,    0x1b,
      0x3,   0x1b,   0x3,    0x1b,   0x5,    0x1b,   0x193,  0xa,    0x1b,
      0x3,   0x1c,   0x5,    0x1c,   0x196,  0xa,    0x1c,   0x3,    0x1d,
      0x3,   0x1d,   0x3,    0x1d,   0x3,    0x1d,   0x3,    0x1d,   0x3,
      0x1d,  0x3,    0x1d,   0x3,    0x1d,   0x3,    0x1d,   0x3,    0x1d,
      0x3,   0x1e,   0x3,    0x1e,   0x5,    0x1e,   0x1a4,  0xa,    0x1e,
      0x3,   0x1e,   0x3,    0x1e,   0x5,    0x1e,   0x1a8,  0xa,    0x1e,
      0x3,   0x1e,   0x3,    0x1e,   0x3,    0x1f,   0x5,    0x1f,   0x1ad,
      0xa,   0x1f,   0x3,    0x20,   0x3,    0x20,   0x3,    0x20,   0x3,
      0x20,  0x3,    0x20,   0x3,    0x20,   0x3,    0x20,   0x3,    0x20,
      0x3,   0x20,   0x3,    0x21,   0x3,    0x21,   0x3,    0x22,   0x3,
      0x22,  0x5,    0x22,   0x1bc,  0xa,    0x22,   0x3,    0x22,   0x3,
      0x22,  0x7,    0x22,   0x1c0,  0xa,    0x22,   0xc,    0x22,   0xe,
      0x22,  0x1c3,  0xb,    0x22,   0x3,    0x22,   0x3,    0x22,   0x3,
      0x23,  0x3,    0x23,   0x3,    0x23,   0x3,    0x23,   0x3,    0x24,
      0x3,   0x24,   0x3,    0x25,   0x3,    0x25,   0x3,    0x25,   0x3,
      0x25,  0x3,    0x25,   0x3,    0x26,   0x3,    0x26,   0x3,    0x26,
      0x5,   0x26,   0x1d5,  0xa,    0x26,   0x3,    0x27,   0x5,    0x27,
      0x1d8, 0xa,    0x27,   0x3,    0x27,   0x3,    0x27,   0x3,    0x28,
      0x3,   0x28,   0x3,    0x29,   0x3,    0x29,   0x3,    0x29,   0x3,
      0x29,  0x3,    0x29,   0x3,    0x29,   0x3,    0x29,   0x5,    0x29,
      0x1e5, 0xa,    0x29,   0x3,    0x2a,   0x3,    0x2a,   0x3,    0x2a,
      0x3,   0x2a,   0x3,    0x2a,   0x3,    0x2a,   0x3,    0x2b,   0x3,
      0x2b,  0x5,    0x2b,   0x1ef,  0xa,    0x2b,   0x3,    0x2c,   0x3,
      0x2c,  0x3,    0x2d,   0x3,    0x2d,   0x3,    0x2e,   0x3,    0x2e,
      0x3,   0x2e,   0x5,    0x2e,   0x1f8,  0xa,    0x2e,   0x3,    0x2f,
      0x3,   0x2f,   0x3,    0x2f,   0x3,    0x2f,   0x5,    0x2f,   0x1fe,
      0xa,   0x2f,   0x3,    0x2f,   0x3,    0x2f,   0x3,    0x30,   0x3,
      0x30,  0x3,    0x30,   0x6,    0x30,   0x205,  0xa,    0x30,   0xd,
      0x30,  0xe,    0x30,   0x206,  0x3,    0x31,   0x3,    0x31,   0x3,
      0x31,  0x3,    0x31,   0x3,    0x31,   0x3,    0x31,   0x3,    0x31,
      0x3,   0x31,   0x3,    0x31,   0x3,    0x31,   0x3,    0x31,   0x3,
      0x31,  0x3,    0x31,   0x3,    0x31,   0x3,    0x31,   0x3,    0x31,
      0x3,   0x31,   0x3,    0x31,   0x3,    0x31,   0x3,    0x31,   0x3,
      0x31,  0x3,    0x31,   0x3,    0x31,   0x3,    0x31,   0x3,    0x31,
      0x3,   0x31,   0x5,    0x31,   0x223,  0xa,    0x31,   0x3,    0x32,
      0x7,   0x32,   0x226,  0xa,    0x32,   0xc,    0x32,   0xe,    0x32,
      0x229, 0xb,    0x32,   0x3,    0x33,   0x5,    0x33,   0x22c,  0xa,
      0x33,  0x3,    0x33,   0x3,    0x33,   0x3,    0x33,   0x3,    0x33,
      0x3,   0x34,   0x3,    0x34,   0x5,    0x34,   0x234,  0xa,    0x34,
      0x3,   0x35,   0x3,    0x35,   0x3,    0x36,   0x3,    0x36,   0x3,
      0x36,  0x3,    0x37,   0x3,    0x37,   0x3,    0x37,   0x3,    0x38,
      0x3,   0x38,   0x3,    0x38,   0x5,    0x38,   0x241,  0xa,    0x38,
      0x3,   0x38,   0x5,    0x38,   0x244,  0xa,    0x38,   0x3,    0x38,
      0x3,   0x38,   0x3,    0x39,   0x3,    0x39,   0x5,    0x39,   0x24a,
      0xa,   0x39,   0x3,    0x39,   0x3,    0x39,   0x3,    0x39,   0x3,
      0x39,  0x3,    0x39,   0x3,    0x39,   0x3,    0x3a,   0x3,    0x3a,
      0x5,   0x3a,   0x254,  0xa,    0x3a,   0x3,    0x3a,   0x3,    0x3a,
      0x5,   0x3a,   0x258,  0xa,    0x3a,   0x3,    0x3a,   0x3,    0x3a,
      0x3,   0x3a,   0x3,    0x3a,   0x3,    0x3a,   0x3,    0x3a,   0x3,
      0x3a,  0x3,    0x3b,   0x3,    0x3b,   0x3,    0x3b,   0x3,    0x3b,
      0x3,   0x3b,   0x3,    0x3b,   0x3,    0x3b,   0x3,    0x3c,   0x5,
      0x3c,  0x269,  0xa,    0x3c,   0x3,    0x3c,   0x3,    0x3c,   0x3,
      0x3c,  0x3,    0x3c,   0x3,    0x3c,   0x3,    0x3c,   0x3,    0x3d,
      0x3,   0x3d,   0x3,    0x3d,   0x3,    0x3d,   0x3,    0x3d,   0x3,
      0x3d,  0x3,    0x3d,   0x3,    0x3e,   0x3,    0x3e,   0x3,    0x3e,
      0x3,   0x3e,   0x3,    0x3e,   0x3,    0x3e,   0x3,    0x3e,   0x3,
      0x3e,  0x3,    0x3f,   0x3,    0x3f,   0x3,    0x3f,   0x3,    0x3f,
      0x3,   0x3f,   0x3,    0x3f,   0x3,    0x3f,   0x5,    0x3f,   0x287,
      0xa,   0x3f,   0x3,    0x40,   0x3,    0x40,   0x3,    0x40,   0x3,
      0x40,  0x7,    0x40,   0x28d,  0xa,    0x40,   0xc,    0x40,   0xe,
      0x40,  0x290,  0xb,    0x40,   0x3,    0x40,   0x3,    0x40,   0x3,
      0x41,  0x3,    0x41,   0x7,    0x41,   0x296,  0xa,    0x41,   0xc,
      0x41,  0xe,    0x41,   0x299,  0xb,    0x41,   0x3,    0x41,   0x2,
      0xc,   0x18,   0x1a,   0x1c,   0x1e,   0x20,   0x22,   0x24,   0x26,
      0x28,  0x2c,   0x42,   0x2,    0x4,    0x6,    0x8,    0xa,    0xc,
      0xe,   0x10,   0x12,   0x14,   0x16,   0x18,   0x1a,   0x1c,   0x1e,
      0x20,  0x22,   0x24,   0x26,   0x28,   0x2a,   0x2c,   0x2e,   0x30,
      0x32,  0x34,   0x36,   0x38,   0x3a,   0x3c,   0x3e,   0x40,   0x42,
      0x44,  0x46,   0x48,   0x4a,   0x4c,   0x4e,   0x50,   0x52,   0x54,
      0x56,  0x58,   0x5a,   0x5c,   0x5e,   0x60,   0x62,   0x64,   0x66,
      0x68,  0x6a,   0x6c,   0x6e,   0x70,   0x72,   0x74,   0x76,   0x78,
      0x7a,  0x7c,   0x7e,   0x80,   0x2,    0xb,    0x3,    0x2,    0x39,
      0x3a,  0x4,    0x2,    0x33,   0x33,   0x3e,   0x3e,   0x3,    0x2,
      0x3f,  0x42,   0x3,    0x2,    0x43,   0x45,   0x3,    0x2,    0x34,
      0x35,  0x3,    0x2,    0x36,   0x38,   0x5,    0x2,    0x34,   0x35,
      0x3b,  0x3b,   0x4a,   0x4a,   0x3,    0x2,    0x31,   0x32,   0x4,
      0x2,   0x3c,   0x3d,   0x4c,   0x4c,   0x2,    0x2ac,  0x2,    0x82,
      0x3,   0x2,    0x2,    0x2,    0x4,    0x84,   0x3,    0x2,    0x2,
      0x2,   0x6,    0xa4,   0x3,    0x2,    0x2,    0x2,    0x8,    0xa6,
      0x3,   0x2,    0x2,    0x2,    0xa,    0xac,   0x3,    0x2,    0x2,
      0x2,   0xc,    0xb7,   0x3,    0x2,    0x2,    0x2,    0xe,    0xc2,
      0x3,   0x2,    0x2,    0x2,    0x10,   0xc4,   0x3,    0x2,    0x2,
      0x2,   0x12,   0xde,   0x3,    0x2,    0x2,    0x2,    0x14,   0xe0,
      0x3,   0x2,    0x2,    0x2,    0x16,   0xe4,   0x3,    0x2,    0x2,
      0x2,   0x18,   0xe6,   0x3,    0x2,    0x2,    0x2,    0x1a,   0xf4,
      0x3,   0x2,    0x2,    0x2,    0x1c,   0xff,   0x3,    0x2,    0x2,
      0x2,   0x1e,   0x10a,  0x3,    0x2,    0x2,    0x2,    0x20,   0x115,
      0x3,   0x2,    0x2,    0x2,    0x22,   0x120,  0x3,    0x2,    0x2,
      0x2,   0x24,   0x12b,  0x3,    0x2,    0x2,    0x2,    0x26,   0x136,
      0x3,   0x2,    0x2,    0x2,    0x28,   0x141,  0x3,    0x2,    0x2,
      0x2,   0x2a,   0x14f,  0x3,    0x2,    0x2,    0x2,    0x2c,   0x151,
      0x3,   0x2,    0x2,    0x2,    0x2e,   0x16b,  0x3,    0x2,    0x2,
      0x2,   0x30,   0x173,  0x3,    0x2,    0x2,    0x2,    0x32,   0x177,
      0x3,   0x2,    0x2,    0x2,    0x34,   0x192,  0x3,    0x2,    0x2,
      0x2,   0x36,   0x195,  0x3,    0x2,    0x2,    0x2,    0x38,   0x197,
      0x3,   0x2,    0x2,    0x2,    0x3a,   0x1a1,  0x3,    0x2,    0x2,
      0x2,   0x3c,   0x1ac,  0x3,    0x2,    0x2,    0x2,    0x3e,   0x1ae,
      0x3,   0x2,    0x2,    0x2,    0x40,   0x1b7,  0x3,    0x2,    0x2,
      0x2,   0x42,   0x1b9,  0x3,    0x2,    0x2,    0x2,    0x44,   0x1c6,
      0x3,   0x2,    0x2,    0x2,    0x46,   0x1ca,  0x3,    0x2,    0x2,
      0x2,   0x48,   0x1cc,  0x3,    0x2,    0x2,    0x2,    0x4a,   0x1d1,
      0x3,   0x2,    0x2,    0x2,    0x4c,   0x1d7,  0x3,    0x2,    0x2,
      0x2,   0x4e,   0x1db,  0x3,    0x2,    0x2,    0x2,    0x50,   0x1dd,
      0x3,   0x2,    0x2,    0x2,    0x52,   0x1e6,  0x3,    0x2,    0x2,
      0x2,   0x54,   0x1ec,  0x3,    0x2,    0x2,    0x2,    0x56,   0x1f0,
      0x3,   0x2,    0x2,    0x2,    0x58,   0x1f2,  0x3,    0x2,    0x2,
      0x2,   0x5a,   0x1f4,  0x3,    0x2,    0x2,    0x2,    0x5c,   0x1fd,
      0x3,   0x2,    0x2,    0x2,    0x5e,   0x201,  0x3,    0x2,    0x2,
      0x2,   0x60,   0x222,  0x3,    0x2,    0x2,    0x2,    0x62,   0x227,
      0x3,   0x2,    0x2,    0x2,    0x64,   0x22b,  0x3,    0x2,    0x2,
      0x2,   0x66,   0x233,  0x3,    0x2,    0x2,    0x2,    0x68,   0x235,
      0x3,   0x2,    0x2,    0x2,    0x6a,   0x237,  0x3,    0x2,    0x2,
      0x2,   0x6c,   0x23a,  0x3,    0x2,    0x2,    0x2,    0x6e,   0x23d,
      0x3,   0x2,    0x2,    0x2,    0x70,   0x247,  0x3,    0x2,    0x2,
      0x2,   0x72,   0x251,  0x3,    0x2,    0x2,    0x2,    0x74,   0x260,
      0x3,   0x2,    0x2,    0x2,    0x76,   0x268,  0x3,    0x2,    0x2,
      0x2,   0x78,   0x270,  0x3,    0x2,    0x2,    0x2,    0x7a,   0x277,
      0x3,   0x2,    0x2,    0x2,    0x7c,   0x286,  0x3,    0x2,    0x2,
      0x2,   0x7e,   0x288,  0x3,    0x2,    0x2,    0x2,    0x80,   0x297,
      0x3,   0x2,    0x2,    0x2,    0x82,   0x83,   0x7,    0x4c,   0x2,
      0x2,   0x83,   0x3,    0x3,    0x2,    0x2,    0x2,    0x84,   0x86,
      0x7,   0x3,    0x2,    0x2,    0x85,   0x87,   0x5,    0x2,    0x2,
      0x2,   0x86,   0x85,   0x3,    0x2,    0x2,    0x2,    0x86,   0x87,
      0x3,   0x2,    0x2,    0x2,    0x87,   0x8c,   0x3,    0x2,    0x2,
      0x2,   0x88,   0x89,   0x7,    0x4,    0x2,    0x2,    0x89,   0x8b,
      0x5,   0x2,    0x2,    0x2,    0x8a,   0x88,   0x3,    0x2,    0x2,
      0x2,   0x8b,   0x8e,   0x3,    0x2,    0x2,    0x2,    0x8c,   0x8a,
      0x3,   0x2,    0x2,    0x2,    0x8c,   0x8d,   0x3,    0x2,    0x2,
      0x2,   0x8d,   0x8f,   0x3,    0x2,    0x2,    0x2,    0x8e,   0x8c,
      0x3,   0x2,    0x2,    0x2,    0x8f,   0x90,   0x7,    0x5,    0x2,
      0x2,   0x90,   0x5,    0x3,    0x2,    0x2,    0x2,    0x91,   0x93,
      0x7,   0x3,    0x2,    0x2,    0x92,   0x94,   0x5,    0x2,    0x2,
      0x2,   0x93,   0x92,   0x3,    0x2,    0x2,    0x2,    0x93,   0x94,
      0x3,   0x2,    0x2,    0x2,    0x94,   0x99,   0x3,    0x2,    0x2,
      0x2,   0x95,   0x96,   0x7,    0x4,    0x2,    0x2,    0x96,   0x98,
      0x5,   0x2,    0x2,    0x2,    0x97,   0x95,   0x3,    0x2,    0x2,
      0x2,   0x98,   0x9b,   0x3,    0x2,    0x2,    0x2,    0x99,   0x97,
      0x3,   0x2,    0x2,    0x2,    0x99,   0x9a,   0x3,    0x2,    0x2,
      0x2,   0x9a,   0x9e,   0x3,    0x2,    0x2,    0x2,    0x9b,   0x99,
      0x3,   0x2,    0x2,    0x2,    0x9c,   0x9d,   0x7,    0x4,    0x2,
      0x2,   0x9d,   0x9f,   0x7,    0x46,   0x2,    0x2,    0x9e,   0x9c,
      0x3,   0x2,    0x2,    0x2,    0x9e,   0x9f,   0x3,    0x2,    0x2,
      0x2,   0x9f,   0xa0,   0x3,    0x2,    0x2,    0x2,    0xa0,   0xa5,
      0x7,   0x5,    0x2,    0x2,    0xa1,   0xa2,   0x7,    0x3,    0x2,
      0x2,   0xa2,   0xa3,   0x7,    0x46,   0x2,    0x2,    0xa3,   0xa5,
      0x7,   0x5,    0x2,    0x2,    0xa4,   0x91,   0x3,    0x2,    0x2,
      0x2,   0xa4,   0xa1,   0x3,    0x2,    0x2,    0x2,    0xa5,   0x7,
      0x3,   0x2,    0x2,    0x2,    0xa6,   0xa8,   0x7,    0x4c,   0x2,
      0x2,   0xa7,   0xa9,   0x5,    0x4,    0x3,    0x2,    0xa8,   0xa7,
      0x3,   0x2,    0x2,    0x2,    0xa8,   0xa9,   0x3,    0x2,    0x2,
      0x2,   0xa9,   0x9,    0x3,    0x2,    0x2,    0x2,    0xaa,   0xab,
      0x7,   0x6,    0x2,    0x2,    0xab,   0xad,   0x5,    0x2,    0x2,
      0x2,   0xac,   0xaa,   0x3,    0x2,    0x2,    0x2,    0xac,   0xad,
      0x3,   0x2,    0x2,    0x2,    0xad,   0xb,    0x3,    0x2,    0x2,
      0x2,   0xae,   0xaf,   0x7,    0x2c,   0x2,    0x2,    0xaf,   0xb4,
      0x5,   0x8,    0x5,    0x2,    0xb0,   0xb1,   0x7,    0x4,    0x2,
      0x2,   0xb1,   0xb3,   0x5,    0x8,    0x5,    0x2,    0xb2,   0xb0,
      0x3,   0x2,    0x2,    0x2,    0xb3,   0xb6,   0x3,    0x2,    0x2,
      0x2,   0xb4,   0xb2,   0x3,    0x2,    0x2,    0x2,    0xb4,   0xb5,
      0x3,   0x2,    0x2,    0x2,    0xb5,   0xb8,   0x3,    0x2,    0x2,
      0x2,   0xb6,   0xb4,   0x3,    0x2,    0x2,    0x2,    0xb7,   0xae,
      0x3,   0x2,    0x2,    0x2,    0xb7,   0xb8,   0x3,    0x2,    0x2,
      0x2,   0xb8,   0xd,    0x3,    0x2,    0x2,    0x2,    0xb9,   0xba,
      0x7,   0x28,   0x2,    0x2,    0xba,   0xbf,   0x7,    0x4c,   0x2,
      0x2,   0xbb,   0xbc,   0x7,    0x4,    0x2,    0x2,    0xbc,   0xbe,
      0x7,   0x4c,   0x2,    0x2,    0xbd,   0xbb,   0x3,    0x2,    0x2,
      0x2,   0xbe,   0xc1,   0x3,    0x2,    0x2,    0x2,    0xbf,   0xbd,
      0x3,   0x2,    0x2,    0x2,    0xbf,   0xc0,   0x3,    0x2,    0x2,
      0x2,   0xc0,   0xc3,   0x3,    0x2,    0x2,    0x2,    0xc1,   0xbf,
      0x3,   0x2,    0x2,    0x2,    0xc2,   0xb9,   0x3,    0x2,    0x2,
      0x2,   0xc2,   0xc3,   0x3,    0x2,    0x2,    0x2,    0xc3,   0xf,
      0x3,   0x2,    0x2,    0x2,    0xc4,   0xc5,   0x7,    0x4c,   0x2,
      0x2,   0xc5,   0xc7,   0x7,    0x6,    0x2,    0x2,    0xc6,   0xc8,
      0x5,   0x2,    0x2,    0x2,    0xc7,   0xc6,   0x3,    0x2,    0x2,
      0x2,   0xc7,   0xc8,   0x3,    0x2,    0x2,    0x2,    0xc8,   0x11,
      0x3,   0x2,    0x2,    0x2,    0xc9,   0xcb,   0x7,    0x3,    0x2,
      0x2,   0xca,   0xcc,   0x5,    0x10,   0x9,    0x2,    0xcb,   0xca,
      0x3,   0x2,    0x2,    0x2,    0xcb,   0xcc,   0x3,    0x2,    0x2,
      0x2,   0xcc,   0xd1,   0x3,    0x2,    0x2,    0x2,    0xcd,   0xce,
      0x7,   0x4,    0x2,    0x2,    0xce,   0xd0,   0x5,    0x10,   0x9,
      0x2,   0xcf,   0xcd,   0x3,    0x2,    0x2,    0x2,    0xd0,   0xd3,
      0x3,   0x2,    0x2,    0x2,    0xd1,   0xcf,   0x3,    0x2,    0x2,
      0x2,   0xd1,   0xd2,   0x3,    0x2,    0x2,    0x2,    0xd2,   0xd4,
      0x3,   0x2,    0x2,    0x2,    0xd3,   0xd1,   0x3,    0x2,    0x2,
      0x2,   0xd4,   0xdf,   0x7,    0x5,    0x2,    0x2,    0xd5,   0xd6,
      0x7,   0x3,    0x2,    0x2,    0xd6,   0xd7,   0x5,    0x10,   0x9,
      0x2,   0xd7,   0xd8,   0x7,    0x4,    0x2,    0x2,    0xd8,   0xd9,
      0x5,   0x10,   0x9,    0x2,    0xd9,   0xda,   0x7,    0x4,    0x2,
      0x2,   0xda,   0xdb,   0x7,    0x46,   0x2,    0x2,    0xdb,   0xdc,
      0x7,   0x4c,   0x2,    0x2,    0xdc,   0xdd,   0x7,    0x5,    0x2,
      0x2,   0xdd,   0xdf,   0x3,    0x2,    0x2,    0x2,    0xde,   0xc9,
      0x3,   0x2,    0x2,    0x2,    0xde,   0xd5,   0x3,    0x2,    0x2,
      0x2,   0xdf,   0x13,   0x3,    0x2,    0x2,    0x2,    0xe0,   0xe2,
      0x7,   0x4c,   0x2,    0x2,    0xe1,   0xe3,   0x5,    0x12,   0xa,
      0x2,   0xe2,   0xe1,   0x3,    0x2,    0x2,    0x2,    0xe2,   0xe3,
      0x3,   0x2,    0x2,    0x2,    0xe3,   0x15,   0x3,    0x2,    0x2,
      0x2,   0xe4,   0xe5,   0x5,    0x18,   0xd,    0x2,    0xe5,   0x17,
      0x3,   0x2,    0x2,    0x2,    0xe6,   0xe7,   0x8,    0xd,    0x1,
      0x2,   0xe7,   0xe8,   0x5,    0x1a,   0xe,    0x2,    0xe8,   0xf1,
      0x3,   0x2,    0x2,    0x2,    0xe9,   0xea,   0xc,    0x3,    0x2,
      0x2,   0xea,   0xeb,   0x7,    0x7,    0x2,    0x2,    0xeb,   0xec,
      0x5,   0x1a,   0xe,    0x2,    0xec,   0xed,   0x7,    0x6,    0x2,
      0x2,   0xed,   0xee,   0x5,    0x1a,   0xe,    0x2,    0xee,   0xf0,
      0x3,   0x2,    0x2,    0x2,    0xef,   0xe9,   0x3,    0x2,    0x2,
      0x2,   0xf0,   0xf3,   0x3,    0x2,    0x2,    0x2,    0xf1,   0xef,
      0x3,   0x2,    0x2,    0x2,    0xf1,   0xf2,   0x3,    0x2,    0x2,
      0x2,   0xf2,   0x19,   0x3,    0x2,    0x2,    0x2,    0xf3,   0xf1,
      0x3,   0x2,    0x2,    0x2,    0xf4,   0xf5,   0x8,    0xe,    0x1,
      0x2,   0xf5,   0xf6,   0x5,    0x1c,   0xf,    0x2,    0xf6,   0xfc,
      0x3,   0x2,    0x2,    0x2,    0xf7,   0xf8,   0xc,    0x3,    0x2,
      0x2,   0xf8,   0xf9,   0x7,    0x8,    0x2,    0x2,    0xf9,   0xfb,
      0x5,   0x1c,   0xf,    0x2,    0xfa,   0xf7,   0x3,    0x2,    0x2,
      0x2,   0xfb,   0xfe,   0x3,    0x2,    0x2,    0x2,    0xfc,   0xfa,
      0x3,   0x2,    0x2,    0x2,    0xfc,   0xfd,   0x3,    0x2,    0x2,
      0x2,   0xfd,   0x1b,   0x3,    0x2,    0x2,    0x2,    0xfe,   0xfc,
      0x3,   0x2,    0x2,    0x2,    0xff,   0x100,  0x8,    0xf,    0x1,
      0x2,   0x100,  0x101,  0x5,    0x1e,   0x10,   0x2,    0x101,  0x107,
      0x3,   0x2,    0x2,    0x2,    0x102,  0x103,  0xc,    0x3,    0x2,
      0x2,   0x103,  0x104,  0x7,    0x9,    0x2,    0x2,    0x104,  0x106,
      0x5,   0x1e,   0x10,   0x2,    0x105,  0x102,  0x3,    0x2,    0x2,
      0x2,   0x106,  0x109,  0x3,    0x2,    0x2,    0x2,    0x107,  0x105,
      0x3,   0x2,    0x2,    0x2,    0x107,  0x108,  0x3,    0x2,    0x2,
      0x2,   0x108,  0x1d,   0x3,    0x2,    0x2,    0x2,    0x109,  0x107,
      0x3,   0x2,    0x2,    0x2,    0x10a,  0x10b,  0x8,    0x10,   0x1,
      0x2,   0x10b,  0x10c,  0x5,    0x20,   0x11,   0x2,    0x10c,  0x112,
      0x3,   0x2,    0x2,    0x2,    0x10d,  0x10e,  0xc,    0x3,    0x2,
      0x2,   0x10e,  0x10f,  0x9,    0x2,    0x2,    0x2,    0x10f,  0x111,
      0x5,   0x20,   0x11,   0x2,    0x110,  0x10d,  0x3,    0x2,    0x2,
      0x2,   0x111,  0x114,  0x3,    0x2,    0x2,    0x2,    0x112,  0x110,
      0x3,   0x2,    0x2,    0x2,    0x112,  0x113,  0x3,    0x2,    0x2,
      0x2,   0x113,  0x1f,   0x3,    0x2,    0x2,    0x2,    0x114,  0x112,
      0x3,   0x2,    0x2,    0x2,    0x115,  0x116,  0x8,    0x11,   0x1,
      0x2,   0x116,  0x117,  0x5,    0x22,   0x12,   0x2,    0x117,  0x11d,
      0x3,   0x2,    0x2,    0x2,    0x118,  0x119,  0xc,    0x3,    0x2,
      0x2,   0x119,  0x11a,  0x9,    0x3,    0x2,    0x2,    0x11a,  0x11c,
      0x5,   0x22,   0x12,   0x2,    0x11b,  0x118,  0x3,    0x2,    0x2,
      0x2,   0x11c,  0x11f,  0x3,    0x2,    0x2,    0x2,    0x11d,  0x11b,
      0x3,   0x2,    0x2,    0x2,    0x11d,  0x11e,  0x3,    0x2,    0x2,
      0x2,   0x11e,  0x21,   0x3,    0x2,    0x2,    0x2,    0x11f,  0x11d,
      0x3,   0x2,    0x2,    0x2,    0x120,  0x121,  0x8,    0x12,   0x1,
      0x2,   0x121,  0x122,  0x5,    0x24,   0x13,   0x2,    0x122,  0x128,
      0x3,   0x2,    0x2,    0x2,    0x123,  0x124,  0xc,    0x3,    0x2,
      0x2,   0x124,  0x125,  0x9,    0x4,    0x2,    0x2,    0x125,  0x127,
      0x5,   0x24,   0x13,   0x2,    0x126,  0x123,  0x3,    0x2,    0x2,
      0x2,   0x127,  0x12a,  0x3,    0x2,    0x2,    0x2,    0x128,  0x126,
      0x3,   0x2,    0x2,    0x2,    0x128,  0x129,  0x3,    0x2,    0x2,
      0x2,   0x129,  0x23,   0x3,    0x2,    0x2,    0x2,    0x12a,  0x128,
      0x3,   0x2,    0x2,    0x2,    0x12b,  0x12c,  0x8,    0x13,   0x1,
      0x2,   0x12c,  0x12d,  0x5,    0x26,   0x14,   0x2,    0x12d,  0x133,
      0x3,   0x2,    0x2,    0x2,    0x12e,  0x12f,  0xc,    0x3,    0x2,
      0x2,   0x12f,  0x130,  0x9,    0x5,    0x2,    0x2,    0x130,  0x132,
      0x5,   0x26,   0x14,   0x2,    0x131,  0x12e,  0x3,    0x2,    0x2,
      0x2,   0x132,  0x135,  0x3,    0x2,    0x2,    0x2,    0x133,  0x131,
      0x3,   0x2,    0x2,    0x2,    0x133,  0x134,  0x3,    0x2,    0x2,
      0x2,   0x134,  0x25,   0x3,    0x2,    0x2,    0x2,    0x135,  0x133,
      0x3,   0x2,    0x2,    0x2,    0x136,  0x137,  0x8,    0x14,   0x1,
      0x2,   0x137,  0x138,  0x5,    0x28,   0x15,   0x2,    0x138,  0x13e,
      0x3,   0x2,    0x2,    0x2,    0x139,  0x13a,  0xc,    0x3,    0x2,
      0x2,   0x13a,  0x13b,  0x9,    0x6,    0x2,    0x2,    0x13b,  0x13d,
      0x5,   0x28,   0x15,   0x2,    0x13c,  0x139,  0x3,    0x2,    0x2,
      0x2,   0x13d,  0x140,  0x3,    0x2,    0x2,    0x2,    0x13e,  0x13c,
      0x3,   0x2,    0x2,    0x2,    0x13e,  0x13f,  0x3,    0x2,    0x2,
      0x2,   0x13f,  0x27,   0x3,    0x2,    0x2,    0x2,    0x140,  0x13e,
      0x3,   0x2,    0x2,    0x2,    0x141,  0x142,  0x8,    0x15,   0x1,
      0x2,   0x142,  0x143,  0x5,    0x2a,   0x16,   0x2,    0x143,  0x149,
      0x3,   0x2,    0x2,    0x2,    0x144,  0x145,  0xc,    0x3,    0x2,
      0x2,   0x145,  0x146,  0x9,    0x7,    0x2,    0x2,    0x146,  0x148,
      0x5,   0x2a,   0x16,   0x2,    0x147,  0x144,  0x3,    0x2,    0x2,
      0x2,   0x148,  0x14b,  0x3,    0x2,    0x2,    0x2,    0x149,  0x147,
      0x3,   0x2,    0x2,    0x2,    0x149,  0x14a,  0x3,    0x2,    0x2,
      0x2,   0x14a,  0x29,   0x3,    0x2,    0x2,    0x2,    0x14b,  0x149,
      0x3,   0x2,    0x2,    0x2,    0x14c,  0x150,  0x5,    0x32,   0x1a,
      0x2,   0x14d,  0x14e,  0x9,    0x8,    0x2,    0x2,    0x14e,  0x150,
      0x5,   0x2a,   0x16,   0x2,    0x14f,  0x14c,  0x3,    0x2,    0x2,
      0x2,   0x14f,  0x14d,  0x3,    0x2,    0x2,    0x2,    0x150,  0x2b,
      0x3,   0x2,    0x2,    0x2,    0x151,  0x152,  0x8,    0x17,   0x1,
      0x2,   0x152,  0x153,  0x7,    0x4c,   0x2,    0x2,    0x153,  0x15e,
      0x3,   0x2,    0x2,    0x2,    0x154,  0x155,  0xc,    0x4,    0x2,
      0x2,   0x155,  0x156,  0x7,    0xa,    0x2,    0x2,    0x156,  0x15d,
      0x7,   0x4c,   0x2,    0x2,    0x157,  0x158,  0xc,    0x3,    0x2,
      0x2,   0x158,  0x159,  0x7,    0xb,    0x2,    0x2,    0x159,  0x15a,
      0x5,   0x16,   0xc,    0x2,    0x15a,  0x15b,  0x7,    0xc,    0x2,
      0x2,   0x15b,  0x15d,  0x3,    0x2,    0x2,    0x2,    0x15c,  0x154,
      0x3,   0x2,    0x2,    0x2,    0x15c,  0x157,  0x3,    0x2,    0x2,
      0x2,   0x15d,  0x160,  0x3,    0x2,    0x2,    0x2,    0x15e,  0x15c,
      0x3,   0x2,    0x2,    0x2,    0x15e,  0x15f,  0x3,    0x2,    0x2,
      0x2,   0x15f,  0x2d,   0x3,    0x2,    0x2,    0x2,    0x160,  0x15e,
      0x3,   0x2,    0x2,    0x2,    0x161,  0x162,  0x7,    0x48,   0x2,
      0x2,   0x162,  0x16c,  0x5,    0x2c,   0x17,   0x2,    0x163,  0x164,
      0x7,   0x49,   0x2,    0x2,    0x164,  0x16c,  0x5,    0x2c,   0x17,
      0x2,   0x165,  0x166,  0x5,    0x2c,   0x17,   0x2,    0x166,  0x167,
      0x7,   0x48,   0x2,    0x2,    0x167,  0x16c,  0x3,    0x2,    0x2,
      0x2,   0x168,  0x169,  0x5,    0x2c,   0x17,   0x2,    0x169,  0x16a,
      0x7,   0x49,   0x2,    0x2,    0x16a,  0x16c,  0x3,    0x2,    0x2,
      0x2,   0x16b,  0x161,  0x3,    0x2,    0x2,    0x2,    0x16b,  0x163,
      0x3,   0x2,    0x2,    0x2,    0x16b,  0x165,  0x3,    0x2,    0x2,
      0x2,   0x16b,  0x168,  0x3,    0x2,    0x2,    0x2,    0x16c,  0x2f,
      0x3,   0x2,    0x2,    0x2,    0x16d,  0x174,  0x5,    0x2e,   0x18,
      0x2,   0x16e,  0x171,  0x5,    0x2c,   0x17,   0x2,    0x16f,  0x170,
      0x9,   0x9,    0x2,    0x2,    0x170,  0x172,  0x5,    0x16,   0xc,
      0x2,   0x171,  0x16f,  0x3,    0x2,    0x2,    0x2,    0x171,  0x172,
      0x3,   0x2,    0x2,    0x2,    0x172,  0x174,  0x3,    0x2,    0x2,
      0x2,   0x173,  0x16d,  0x3,    0x2,    0x2,    0x2,    0x173,  0x16e,
      0x3,   0x2,    0x2,    0x2,    0x174,  0x31,   0x3,    0x2,    0x2,
      0x2,   0x175,  0x178,  0x5,    0x34,   0x1b,   0x2,    0x176,  0x178,
      0x5,   0x30,   0x19,   0x2,    0x177,  0x175,  0x3,    0x2,    0x2,
      0x2,   0x177,  0x176,  0x3,    0x2,    0x2,    0x2,    0x178,  0x33,
      0x3,   0x2,    0x2,    0x2,    0x179,  0x193,  0x5,    0x44,   0x23,
      0x2,   0x17a,  0x193,  0x7,    0x50,   0x2,    0x2,    0x17b,  0x193,
      0x7,   0x4b,   0x2,    0x2,    0x17c,  0x17d,  0x7,    0x20,   0x2,
      0x2,   0x17d,  0x17e,  0x7,    0x3f,   0x2,    0x2,    0x17e,  0x17f,
      0x5,   0x2,    0x2,    0x2,    0x17f,  0x180,  0x7,    0x41,   0x2,
      0x2,   0x180,  0x181,  0x7,    0x3,    0x2,    0x2,    0x181,  0x182,
      0x5,   0x16,   0xc,    0x2,    0x182,  0x183,  0x7,    0x5,    0x2,
      0x2,   0x183,  0x184,  0x7,    0x28,   0x2,    0x2,    0x184,  0x185,
      0x7,   0x4c,   0x2,    0x2,    0x185,  0x193,  0x3,    0x2,    0x2,
      0x2,   0x186,  0x187,  0x7,    0x21,   0x2,    0x2,    0x187,  0x188,
      0x7,   0x3f,   0x2,    0x2,    0x188,  0x189,  0x5,    0x2,    0x2,
      0x2,   0x189,  0x18a,  0x7,    0x41,   0x2,    0x2,    0x18a,  0x18b,
      0x7,   0x3,    0x2,    0x2,    0x18b,  0x18c,  0x5,    0x16,   0xc,
      0x2,   0x18c,  0x18d,  0x7,    0x5,    0x2,    0x2,    0x18d,  0x193,
      0x3,   0x2,    0x2,    0x2,    0x18e,  0x18f,  0x7,    0x3,    0x2,
      0x2,   0x18f,  0x190,  0x5,    0x16,   0xc,    0x2,    0x190,  0x191,
      0x7,   0x5,    0x2,    0x2,    0x191,  0x193,  0x3,    0x2,    0x2,
      0x2,   0x192,  0x179,  0x3,    0x2,    0x2,    0x2,    0x192,  0x17a,
      0x3,   0x2,    0x2,    0x2,    0x192,  0x17b,  0x3,    0x2,    0x2,
      0x2,   0x192,  0x17c,  0x3,    0x2,    0x2,    0x2,    0x192,  0x186,
      0x3,   0x2,    0x2,    0x2,    0x192,  0x18e,  0x3,    0x2,    0x2,
      0x2,   0x193,  0x35,   0x3,    0x2,    0x2,    0x2,    0x194,  0x196,
      0x5,   0x4a,   0x26,   0x2,    0x195,  0x194,  0x3,    0x2,    0x2,
      0x2,   0x195,  0x196,  0x3,    0x2,    0x2,    0x2,    0x196,  0x37,
      0x3,   0x2,    0x2,    0x2,    0x197,  0x198,  0x7,    0x22,   0x2,
      0x2,   0x198,  0x199,  0x7,    0x3,    0x2,    0x2,    0x199,  0x19a,
      0x5,   0x36,   0x1c,   0x2,    0x19a,  0x19b,  0x7,    0xd,    0x2,
      0x2,   0x19b,  0x19c,  0x5,    0x16,   0xc,    0x2,    0x19c,  0x19d,
      0x7,   0xd,    0x2,    0x2,    0x19d,  0x19e,  0x5,    0x30,   0x19,
      0x2,   0x19e,  0x19f,  0x7,    0x5,    0x2,    0x2,    0x19f,  0x1a0,
      0x5,   0x66,   0x34,   0x2,    0x1a0,  0x39,   0x3,    0x2,    0x2,
      0x2,   0x1a1,  0x1a3,  0x7,    0xb,    0x2,    0x2,    0x1a2,  0x1a4,
      0x5,   0x16,   0xc,    0x2,    0x1a3,  0x1a2,  0x3,    0x2,    0x2,
      0x2,   0x1a3,  0x1a4,  0x3,    0x2,    0x2,    0x2,    0x1a4,  0x1a5,
      0x3,   0x2,    0x2,    0x2,    0x1a5,  0x1a7,  0x7,    0x6,    0x2,
      0x2,   0x1a6,  0x1a8,  0x5,    0x16,   0xc,    0x2,    0x1a7,  0x1a6,
      0x3,   0x2,    0x2,    0x2,    0x1a7,  0x1a8,  0x3,    0x2,    0x2,
      0x2,   0x1a8,  0x1a9,  0x3,    0x2,    0x2,    0x2,    0x1a9,  0x1aa,
      0x7,   0xc,    0x2,    0x2,    0x1aa,  0x3b,   0x3,    0x2,    0x2,
      0x2,   0x1ab,  0x1ad,  0x5,    0x3a,   0x1e,   0x2,    0x1ac,  0x1ab,
      0x3,   0x2,    0x2,    0x2,    0x1ac,  0x1ad,  0x3,    0x2,    0x2,
      0x2,   0x1ad,  0x3d,   0x3,    0x2,    0x2,    0x2,    0x1ae,  0x1af,
      0x7,   0x22,   0x2,    0x2,    0x1af,  0x1b0,  0x7,    0x3,    0x2,
      0x2,   0x1b0,  0x1b1,  0x5,    0x48,   0x25,   0x2,    0x1b1,  0x1b2,
      0x7,   0xe,    0x2,    0x2,    0x1b2,  0x1b3,  0x5,    0x16,   0xc,
      0x2,   0x1b3,  0x1b4,  0x5,    0x3c,   0x1f,   0x2,    0x1b4,  0x1b5,
      0x7,   0x5,    0x2,    0x2,    0x1b5,  0x1b6,  0x5,    0x66,   0x34,
      0x2,   0x1b6,  0x3f,   0x3,    0x2,    0x2,    0x2,    0x1b7,  0x1b8,
      0x5,   0x16,   0xc,    0x2,    0x1b8,  0x41,   0x3,    0x2,    0x2,
      0x2,   0x1b9,  0x1bb,  0x7,    0x3,    0x2,    0x2,    0x1ba,  0x1bc,
      0x5,   0x40,   0x21,   0x2,    0x1bb,  0x1ba,  0x3,    0x2,    0x2,
      0x2,   0x1bb,  0x1bc,  0x3,    0x2,    0x2,    0x2,    0x1bc,  0x1c1,
      0x3,   0x2,    0x2,    0x2,    0x1bd,  0x1be,  0x7,    0x4,    0x2,
      0x2,   0x1be,  0x1c0,  0x5,    0x40,   0x21,   0x2,    0x1bf,  0x1bd,
      0x3,   0x2,    0x2,    0x2,    0x1c0,  0x1c3,  0x3,    0x2,    0x2,
      0x2,   0x1c1,  0x1bf,  0x3,    0x2,    0x2,    0x2,    0x1c1,  0x1c2,
      0x3,   0x2,    0x2,    0x2,    0x1c2,  0x1c4,  0x3,    0x2,    0x2,
      0x2,   0x1c3,  0x1c1,  0x3,    0x2,    0x2,    0x2,    0x1c4,  0x1c5,
      0x7,   0x5,    0x2,    0x2,    0x1c5,  0x43,   0x3,    0x2,    0x2,
      0x2,   0x1c6,  0x1c7,  0x9,    0xa,    0x2,    0x2,    0x1c7,  0x1c8,
      0x5,   0x42,   0x22,   0x2,    0x1c8,  0x1c9,  0x5,    0xe,    0x8,
      0x2,   0x1c9,  0x45,   0x3,    0x2,    0x2,    0x2,    0x1ca,  0x1cb,
      0x7,   0x4c,   0x2,    0x2,    0x1cb,  0x47,   0x3,    0x2,    0x2,
      0x2,   0x1cc,  0x1cd,  0x7,    0x30,   0x2,    0x2,    0x1cd,  0x1ce,
      0x7,   0x4c,   0x2,    0x2,    0x1ce,  0x1cf,  0x7,    0x6,    0x2,
      0x2,   0x1cf,  0x1d0,  0x5,    0x2,    0x2,    0x2,    0x1d0,  0x49,
      0x3,   0x2,    0x2,    0x2,    0x1d1,  0x1d4,  0x5,    0x48,   0x25,
      0x2,   0x1d2,  0x1d3,  0x7,    0x31,   0x2,    0x2,    0x1d3,  0x1d5,
      0x5,   0x16,   0xc,    0x2,    0x1d4,  0x1d2,  0x3,    0x2,    0x2,
      0x2,   0x1d4,  0x1d5,  0x3,    0x2,    0x2,    0x2,    0x1d5,  0x4b,
      0x3,   0x2,    0x2,    0x2,    0x1d6,  0x1d8,  0x7,    0x2d,   0x2,
      0x2,   0x1d7,  0x1d6,  0x3,    0x2,    0x2,    0x2,    0x1d7,  0x1d8,
      0x3,   0x2,    0x2,    0x2,    0x1d8,  0x1d9,  0x3,    0x2,    0x2,
      0x2,   0x1d9,  0x1da,  0x5,    0x44,   0x23,   0x2,    0x1da,  0x4d,
      0x3,   0x2,    0x2,    0x2,    0x1db,  0x1dc,  0x5,    0x30,   0x19,
      0x2,   0x1dc,  0x4f,   0x3,    0x2,    0x2,    0x2,    0x1dd,  0x1de,
      0x7,   0x1f,   0x2,    0x2,    0x1de,  0x1df,  0x7,    0x3,    0x2,
      0x2,   0x1df,  0x1e0,  0x5,    0x16,   0xc,    0x2,    0x1e0,  0x1e1,
      0x7,   0x5,    0x2,    0x2,    0x1e1,  0x1e4,  0x5,    0x66,   0x34,
      0x2,   0x1e2,  0x1e3,  0x7,    0xf,    0x2,    0x2,    0x1e3,  0x1e5,
      0x5,   0x66,   0x34,   0x2,    0x1e4,  0x1e2,  0x3,    0x2,    0x2,
      0x2,   0x1e4,  0x1e5,  0x3,    0x2,    0x2,    0x2,    0x1e5,  0x51,
      0x3,   0x2,    0x2,    0x2,    0x1e6,  0x1e7,  0x7,    0x23,   0x2,
      0x2,   0x1e7,  0x1e8,  0x7,    0x3,    0x2,    0x2,    0x1e8,  0x1e9,
      0x5,   0x16,   0xc,    0x2,    0x1e9,  0x1ea,  0x7,    0x5,    0x2,
      0x2,   0x1ea,  0x1eb,  0x5,    0x66,   0x34,   0x2,    0x1eb,  0x53,
      0x3,   0x2,    0x2,    0x2,    0x1ec,  0x1ee,  0x7,    0x24,   0x2,
      0x2,   0x1ed,  0x1ef,  0x5,    0x16,   0xc,    0x2,    0x1ee,  0x1ed,
      0x3,   0x2,    0x2,    0x2,    0x1ee,  0x1ef,  0x3,    0x2,    0x2,
      0x2,   0x1ef,  0x55,   0x3,    0x2,    0x2,    0x2,    0x1f0,  0x1f1,
      0x7,   0x26,   0x2,    0x2,    0x1f1,  0x57,   0x3,    0x2,    0x2,
      0x2,   0x1f2,  0x1f3,  0x7,    0x25,   0x2,    0x2,    0x1f3,  0x59,
      0x3,   0x2,    0x2,    0x2,    0x1f4,  0x1f5,  0x7,    0x27,   0x2,
      0x2,   0x1f5,  0x1f7,  0x5,    0x46,   0x24,   0x2,    0x1f6,  0x1f8,
      0x5,   0x42,   0x22,   0x2,    0x1f7,  0x1f6,  0x3,    0x2,    0x2,
      0x2,   0x1f7,  0x1f8,  0x3,    0x2,    0x2,    0x2,    0x1f8,  0x5b,
      0x3,   0x2,    0x2,    0x2,    0x1f9,  0x1fa,  0x7,    0x2a,   0x2,
      0x2,   0x1fa,  0x1fe,  0x7,    0x4c,   0x2,    0x2,    0x1fb,  0x1fc,
      0x7,   0x2b,   0x2,    0x2,    0x1fc,  0x1fe,  0x5,    0x14,   0xb,
      0x2,   0x1fd,  0x1f9,  0x3,    0x2,    0x2,    0x2,    0x1fd,  0x1fb,
      0x3,   0x2,    0x2,    0x2,    0x1fe,  0x1ff,  0x3,    0x2,    0x2,
      0x2,   0x1ff,  0x200,  0x5,    0x66,   0x34,   0x2,    0x200,  0x5d,
      0x3,   0x2,    0x2,    0x2,    0x201,  0x202,  0x7,    0x29,   0x2,
      0x2,   0x202,  0x204,  0x5,    0x66,   0x34,   0x2,    0x203,  0x205,
      0x5,   0x5c,   0x2f,   0x2,    0x204,  0x203,  0x3,    0x2,    0x2,
      0x2,   0x205,  0x206,  0x3,    0x2,    0x2,    0x2,    0x206,  0x204,
      0x3,   0x2,    0x2,    0x2,    0x206,  0x207,  0x3,    0x2,    0x2,
      0x2,   0x207,  0x5f,   0x3,    0x2,    0x2,    0x2,    0x208,  0x209,
      0x5,   0x4a,   0x26,   0x2,    0x209,  0x20a,  0x7,    0xd,    0x2,
      0x2,   0x20a,  0x223,  0x3,    0x2,    0x2,    0x2,    0x20b,  0x20c,
      0x5,   0x4c,   0x27,   0x2,    0x20c,  0x20d,  0x7,    0xd,    0x2,
      0x2,   0x20d,  0x223,  0x3,    0x2,    0x2,    0x2,    0x20e,  0x20f,
      0x5,   0x4e,   0x28,   0x2,    0x20f,  0x210,  0x7,    0xd,    0x2,
      0x2,   0x210,  0x223,  0x3,    0x2,    0x2,    0x2,    0x211,  0x212,
      0x5,   0x54,   0x2b,   0x2,    0x212,  0x213,  0x7,    0xd,    0x2,
      0x2,   0x213,  0x223,  0x3,    0x2,    0x2,    0x2,    0x214,  0x215,
      0x5,   0x56,   0x2c,   0x2,    0x215,  0x216,  0x7,    0xd,    0x2,
      0x2,   0x216,  0x223,  0x3,    0x2,    0x2,    0x2,    0x217,  0x218,
      0x5,   0x58,   0x2d,   0x2,    0x218,  0x219,  0x7,    0xd,    0x2,
      0x2,   0x219,  0x223,  0x3,    0x2,    0x2,    0x2,    0x21a,  0x21b,
      0x5,   0x5a,   0x2e,   0x2,    0x21b,  0x21c,  0x7,    0xd,    0x2,
      0x2,   0x21c,  0x223,  0x3,    0x2,    0x2,    0x2,    0x21d,  0x223,
      0x5,   0x50,   0x29,   0x2,    0x21e,  0x223,  0x5,    0x52,   0x2a,
      0x2,   0x21f,  0x223,  0x5,    0x3e,   0x20,   0x2,    0x220,  0x223,
      0x5,   0x38,   0x1d,   0x2,    0x221,  0x223,  0x5,    0x5e,   0x30,
      0x2,   0x222,  0x208,  0x3,    0x2,    0x2,    0x2,    0x222,  0x20b,
      0x3,   0x2,    0x2,    0x2,    0x222,  0x20e,  0x3,    0x2,    0x2,
      0x2,   0x222,  0x211,  0x3,    0x2,    0x2,    0x2,    0x222,  0x214,
      0x3,   0x2,    0x2,    0x2,    0x222,  0x217,  0x3,    0x2,    0x2,
      0x2,   0x222,  0x21a,  0x3,    0x2,    0x2,    0x2,    0x222,  0x21d,
      0x3,   0x2,    0x2,    0x2,    0x222,  0x21e,  0x3,    0x2,    0x2,
      0x2,   0x222,  0x21f,  0x3,    0x2,    0x2,    0x2,    0x222,  0x220,
      0x3,   0x2,    0x2,    0x2,    0x222,  0x221,  0x3,    0x2,    0x2,
      0x2,   0x223,  0x61,   0x3,    0x2,    0x2,    0x2,    0x224,  0x226,
      0x5,   0x60,   0x31,   0x2,    0x225,  0x224,  0x3,    0x2,    0x2,
      0x2,   0x226,  0x229,  0x3,    0x2,    0x2,    0x2,    0x227,  0x225,
      0x3,   0x2,    0x2,    0x2,    0x227,  0x228,  0x3,    0x2,    0x2,
      0x2,   0x228,  0x63,   0x3,    0x2,    0x2,    0x2,    0x229,  0x227,
      0x3,   0x2,    0x2,    0x2,    0x22a,  0x22c,  0x7,    0x1e,   0x2,
      0x2,   0x22b,  0x22a,  0x3,    0x2,    0x2,    0x2,    0x22b,  0x22c,
      0x3,   0x2,    0x2,    0x2,    0x22c,  0x22d,  0x3,    0x2,    0x2,
      0x2,   0x22d,  0x22e,  0x7,    0x10,   0x2,    0x2,    0x22e,  0x22f,
      0x5,   0x62,   0x32,   0x2,    0x22f,  0x230,  0x7,    0x11,   0x2,
      0x2,   0x230,  0x65,   0x3,    0x2,    0x2,    0x2,    0x231,  0x234,
      0x5,   0x60,   0x31,   0x2,    0x232,  0x234,  0x5,    0x64,   0x33,
      0x2,   0x233,  0x231,  0x3,    0x2,    0x2,    0x2,    0x233,  0x232,
      0x3,   0x2,    0x2,    0x2,    0x234,  0x67,   0x3,    0x2,    0x2,
      0x2,   0x235,  0x236,  0x5,    0x64,   0x33,   0x2,    0x236,  0x69,
      0x3,   0x2,    0x2,    0x2,    0x237,  0x238,  0x7,    0x12,   0x2,
      0x2,   0x238,  0x239,  0x7,    0x4b,   0x2,    0x2,    0x239,  0x6b,
      0x3,   0x2,    0x2,    0x2,    0x23a,  0x23b,  0x7,    0x13,   0x2,
      0x2,   0x23b,  0x23c,  0x7,    0x4c,   0x2,    0x2,    0x23c,  0x6d,
      0x3,   0x2,    0x2,    0x2,    0x23d,  0x23e,  0x7,    0x14,   0x2,
      0x2,   0x23e,  0x240,  0x7,    0x4c,   0x2,    0x2,    0x23f,  0x241,
      0x5,   0x6c,   0x37,   0x2,    0x240,  0x23f,  0x3,    0x2,    0x2,
      0x2,   0x240,  0x241,  0x3,    0x2,    0x2,    0x2,    0x241,  0x243,
      0x3,   0x2,    0x2,    0x2,    0x242,  0x244,  0x5,    0x6a,   0x36,
      0x2,   0x243,  0x242,  0x3,    0x2,    0x2,    0x2,    0x243,  0x244,
      0x3,   0x2,    0x2,    0x2,    0x244,  0x245,  0x3,    0x2,    0x2,
      0x2,   0x245,  0x246,  0x7,    0xd,    0x2,    0x2,    0x246,  0x6f,
      0x3,   0x2,    0x2,    0x2,    0x247,  0x249,  0x7,    0x15,   0x2,
      0x2,   0x248,  0x24a,  0x7,    0x1c,   0x2,    0x2,    0x249,  0x248,
      0x3,   0x2,    0x2,    0x2,    0x249,  0x24a,  0x3,    0x2,    0x2,
      0x2,   0x24a,  0x24b,  0x3,    0x2,    0x2,    0x2,    0x24b,  0x24c,
      0x7,   0x19,   0x2,    0x2,    0x24c,  0x24d,  0x7,    0x4c,   0x2,
      0x2,   0x24d,  0x24e,  0x5,    0x4,    0x3,    0x2,    0x24e,  0x24f,
      0x5,   0xa,    0x6,    0x2,    0x24f,  0x250,  0x7,    0xd,    0x2,
      0x2,   0x250,  0x71,   0x3,    0x2,    0x2,    0x2,    0x251,  0x257,
      0x7,   0x15,   0x2,    0x2,    0x252,  0x254,  0x7,    0x1d,   0x2,
      0x2,   0x253,  0x252,  0x3,    0x2,    0x2,    0x2,    0x253,  0x254,
      0x3,   0x2,    0x2,    0x2,    0x254,  0x255,  0x3,    0x2,    0x2,
      0x2,   0x255,  0x256,  0x7,    0x16,   0x2,    0x2,    0x256,  0x258,
      0x7,   0x4b,   0x2,    0x2,    0x257,  0x253,  0x3,    0x2,    0x2,
      0x2,   0x257,  0x258,  0x3,    0x2,    0x2,    0x2,    0x258,  0x259,
      0x3,   0x2,    0x2,    0x2,    0x259,  0x25a,  0x7,    0x18,   0x2,
      0x2,   0x25a,  0x25b,  0x7,    0x4c,   0x2,    0x2,    0x25b,  0x25c,
      0x5,   0x6,    0x4,    0x2,    0x25c,  0x25d,  0x5,    0xa,    0x6,
      0x2,   0x25d,  0x25e,  0x5,    0xc,    0x7,    0x2,    0x25e,  0x25f,
      0x7,   0xd,    0x2,    0x2,    0x25f,  0x73,   0x3,    0x2,    0x2,
      0x2,   0x260,  0x261,  0x7,    0x15,   0x2,    0x2,    0x261,  0x262,
      0x7,   0x1a,   0x2,    0x2,    0x262,  0x263,  0x7,    0x4c,   0x2,
      0x2,   0x263,  0x264,  0x5,    0x6,    0x4,    0x2,    0x264,  0x265,
      0x5,   0xa,    0x6,    0x2,    0x265,  0x266,  0x7,    0xd,    0x2,
      0x2,   0x266,  0x75,   0x3,    0x2,    0x2,    0x2,    0x267,  0x269,
      0x7,   0x1c,   0x2,    0x2,    0x268,  0x267,  0x3,    0x2,    0x2,
      0x2,   0x268,  0x269,  0x3,    0x2,    0x2,    0x2,    0x269,  0x26a,
      0x3,   0x2,    0x2,    0x2,    0x26a,  0x26b,  0x7,    0x19,   0x2,
      0x2,   0x26b,  0x26c,  0x7,    0x4c,   0x2,    0x2,    0x26c,  0x26d,
      0x5,   0x12,   0xa,    0x2,    0x26d,  0x26e,  0x5,    0xa,    0x6,
      0x2,   0x26e,  0x26f,  0x5,    0x68,   0x35,   0x2,    0x26f,  0x77,
      0x3,   0x2,    0x2,    0x2,    0x270,  0x271,  0x7,    0x18,   0x2,
      0x2,   0x271,  0x272,  0x7,    0x4c,   0x2,    0x2,    0x272,  0x273,
      0x5,   0x12,   0xa,    0x2,    0x273,  0x274,  0x5,    0xa,    0x6,
      0x2,   0x274,  0x275,  0x5,    0xc,    0x7,    0x2,    0x275,  0x276,
      0x5,   0x68,   0x35,   0x2,    0x276,  0x79,   0x3,    0x2,    0x2,
      0x2,   0x277,  0x278,  0x7,    0x17,   0x2,    0x2,    0x278,  0x279,
      0x7,   0x4c,   0x2,    0x2,    0x279,  0x27a,  0x7,    0x6,    0x2,
      0x2,   0x27a,  0x27b,  0x5,    0x2,    0x2,    0x2,    0x27b,  0x27c,
      0x7,   0x31,   0x2,    0x2,    0x27c,  0x27d,  0x7,    0x4b,   0x2,
      0x2,   0x27d,  0x27e,  0x7,    0xd,    0x2,    0x2,    0x27e,  0x7b,
      0x3,   0x2,    0x2,    0x2,    0x27f,  0x287,  0x5,    0x6e,   0x38,
      0x2,   0x280,  0x287,  0x5,    0x76,   0x3c,   0x2,    0x281,  0x287,
      0x5,   0x78,   0x3d,   0x2,    0x282,  0x287,  0x5,    0x72,   0x3a,
      0x2,   0x283,  0x287,  0x5,    0x70,   0x39,   0x2,    0x284,  0x287,
      0x5,   0x74,   0x3b,   0x2,    0x285,  0x287,  0x5,    0x7a,   0x3e,
      0x2,   0x286,  0x27f,  0x3,    0x2,    0x2,    0x2,    0x286,  0x280,
      0x3,   0x2,    0x2,    0x2,    0x286,  0x281,  0x3,    0x2,    0x2,
      0x2,   0x286,  0x282,  0x3,    0x2,    0x2,    0x2,    0x286,  0x283,
      0x3,   0x2,    0x2,    0x2,    0x286,  0x284,  0x3,    0x2,    0x2,
      0x2,   0x286,  0x285,  0x3,    0x2,    0x2,    0x2,    0x287,  0x7d,
      0x3,   0x2,    0x2,    0x2,    0x288,  0x289,  0x7,    0x1b,   0x2,
      0x2,   0x289,  0x28a,  0x7,    0x4c,   0x2,    0x2,    0x28a,  0x28e,
      0x7,   0x10,   0x2,    0x2,    0x28b,  0x28d,  0x5,    0x7c,   0x3f,
      0x2,   0x28c,  0x28b,  0x3,    0x2,    0x2,    0x2,    0x28d,  0x290,
      0x3,   0x2,    0x2,    0x2,    0x28e,  0x28c,  0x3,    0x2,    0x2,
      0x2,   0x28e,  0x28f,  0x3,    0x2,    0x2,    0x2,    0x28f,  0x291,
      0x3,   0x2,    0x2,    0x2,    0x290,  0x28e,  0x3,    0x2,    0x2,
      0x2,   0x291,  0x292,  0x7,    0x11,   0x2,    0x2,    0x292,  0x7f,
      0x3,   0x2,    0x2,    0x2,    0x293,  0x296,  0x5,    0x7e,   0x40,
      0x2,   0x294,  0x296,  0x5,    0x7c,   0x3f,   0x2,    0x295,  0x293,
      0x3,   0x2,    0x2,    0x2,    0x295,  0x294,  0x3,    0x2,    0x2,
      0x2,   0x296,  0x299,  0x3,    0x2,    0x2,    0x2,    0x297,  0x295,
      0x3,   0x2,    0x2,    0x2,    0x297,  0x298,  0x3,    0x2,    0x2,
      0x2,   0x298,  0x81,   0x3,    0x2,    0x2,    0x2,    0x299,  0x297,
      0x3,   0x2,    0x2,    0x2,    0x3f,   0x86,   0x8c,   0x93,   0x99,
      0x9e,  0xa4,   0xa8,   0xac,   0xb4,   0xb7,   0xbf,   0xc2,   0xc7,
      0xcb,  0xd1,   0xde,   0xe2,   0xf1,   0xfc,   0x107,  0x112,  0x11d,
      0x128, 0x133,  0x13e,  0x149,  0x14f,  0x15c,  0x15e,  0x16b,  0x171,
      0x173, 0x177,  0x192,  0x195,  0x1a3,  0x1a7,  0x1ac,  0x1bb,  0x1c1,
      0x1d4, 0x1d7,  0x1e4,  0x1ee,  0x1f7,  0x1fd,  0x206,  0x222,  0x227,
      0x22b, 0x233,  0x240,  0x243,  0x249,  0x253,  0x257,  0x268,  0x286,
      0x28e, 0x295,  0x297,
  };

  atn::ATNDeserializer deserializer;
  _atn = deserializer.deserialize(_serializedATN);

  size_t count = _atn.getNumberOfDecisions();
  _decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) {
    _decisionToDFA.emplace_back(_atn.getDecisionState(i), i);
  }
}

TorqueParser::Initializer TorqueParser::_init;
