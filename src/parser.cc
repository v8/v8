// Copyright 2006-2008 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "api.h"
#include "ast.h"
#include "bootstrapper.h"
#include "platform.h"
#include "runtime.h"
#include "parser.h"
#include "scopes.h"

namespace v8 { namespace internal {

DECLARE_bool(lazy);
DEFINE_bool(allow_natives_syntax, false, "allow natives syntax");


class ParserFactory;
class ParserLog;
class TemporaryScope;
template <typename T> class ZoneListWrapper;


class Parser {
 public:
  Parser(Handle<Script> script, bool allow_natives_syntax,
         v8::Extension* extension, bool is_pre_parsing,
         ParserFactory* factory, ParserLog* log, ScriptDataImpl* pre_data);
  virtual ~Parser() { }

  // Pre-parse the program from the character stream; returns true on
  // success, false if a stack-overflow happened during parsing.
  bool PreParseProgram(unibrow::CharacterStream* stream);

  void ReportMessage(const char* message, Vector<const char*> args);
  virtual void ReportMessageAt(Scanner::Location loc,
                               const char* message,
                               Vector<const char*> args) = 0;


  // Returns NULL if parsing failed.
  FunctionLiteral* ParseProgram(Handle<String> source,
                                unibrow::CharacterStream* stream,
                                bool in_global_context);
  FunctionLiteral* ParseLazy(Handle<String> source,
                             Handle<String> name,
                             int start_position, bool is_expression);

 protected:

  enum Mode {
    PARSE_LAZILY,
    PARSE_EAGERLY
  };

  // Report syntax error
  void ReportUnexpectedToken(Token::Value token);

  Handle<Script> script_;
  Scanner scanner_;

  Scope* top_scope_;
  int with_nesting_level_;

  TemporaryScope* temp_scope_;
  Mode mode_;
  List<Node*>* target_stack_;  // for break, continue statements
  bool allow_natives_syntax_;
  v8::Extension* extension_;
  ParserFactory* factory_;
  ParserLog* log_;
  bool is_pre_parsing_;
  ScriptDataImpl* pre_data_;

  bool inside_with() const  { return with_nesting_level_ > 0; }
  ParserFactory* factory() const  { return factory_; }
  ParserLog* log() const { return log_; }
  Scanner& scanner()  { return scanner_; }
  Mode mode() const  { return mode_; }
  ScriptDataImpl* pre_data() const  { return pre_data_; }

  // All ParseXXX functions take as the last argument an *ok parameter
  // which is set to false if parsing failed; it is unchanged otherwise.
  // By making the 'exception handling' explicit, we are forced to check
  // for failure at the call sites.
  void* ParseSourceElements(ZoneListWrapper<Statement>* processor,
                            int end_token, bool* ok);
  Statement* ParseStatement(ZoneStringList* labels, bool* ok);
  Statement* ParseFunctionDeclaration(bool* ok);
  Statement* ParseNativeDeclaration(bool* ok);
  Block* ParseBlock(ZoneStringList* labels, bool* ok);
  Block* ParseVariableStatement(bool* ok);
  Block* ParseVariableDeclarations(bool accept_IN, Expression** var, bool* ok);
  Statement* ParseExpressionOrLabelledStatement(ZoneStringList* labels,
                                                bool* ok);
  IfStatement* ParseIfStatement(ZoneStringList* labels, bool* ok);
  Statement* ParseContinueStatement(bool* ok);
  Statement* ParseBreakStatement(ZoneStringList* labels, bool* ok);
  Statement* ParseReturnStatement(bool* ok);
  Block* WithHelper(Expression* obj, ZoneStringList* labels, bool* ok);
  Statement* ParseWithStatement(ZoneStringList* labels, bool* ok);
  CaseClause* ParseCaseClause(bool* default_seen_ptr, bool* ok);
  SwitchStatement* ParseSwitchStatement(ZoneStringList* labels, bool* ok);
  LoopStatement* ParseDoStatement(ZoneStringList* labels, bool* ok);
  LoopStatement* ParseWhileStatement(ZoneStringList* labels, bool* ok);
  Statement* ParseForStatement(ZoneStringList* labels, bool* ok);
  Statement* ParseThrowStatement(bool* ok);
  Expression* MakeCatchContext(Handle<String> id, VariableProxy* value);
  TryStatement* ParseTryStatement(bool* ok);
  DebuggerStatement* ParseDebuggerStatement(bool* ok);

  Expression* ParseExpression(bool accept_IN, bool* ok);
  Expression* ParseAssignmentExpression(bool accept_IN, bool* ok);
  Expression* ParseConditionalExpression(bool accept_IN, bool* ok);
  Expression* ParseBinaryExpression(int prec, bool accept_IN, bool* ok);
  Expression* ParseUnaryExpression(bool* ok);
  Expression* ParsePostfixExpression(bool* ok);
  Expression* ParseLeftHandSideExpression(bool* ok);
  Expression* ParseNewExpression(bool* ok);
  Expression* ParseMemberExpression(bool* ok);
  Expression* ParseMemberWithNewPrefixesExpression(List<int>* new_prefixes,
                                                   bool* ok);
  Expression* ParsePrimaryExpression(bool* ok);
  Expression* ParseArrayLiteral(bool* ok);
  Expression* ParseObjectLiteral(bool* ok);
  Expression* ParseRegExpLiteral(bool seen_equal, bool* ok);

  enum FunctionLiteralType {
    EXPRESSION,
    DECLARATION,
    NESTED
  };

  ZoneList<Expression*>* ParseArguments(bool* ok);
  FunctionLiteral* ParseFunctionLiteral(Handle<String> var_name,
                                        int function_token_position,
                                        FunctionLiteralType type,
                                        bool* ok);


  // Magical syntax support.
  Expression* ParseV8Intrinsic(bool* ok);

  INLINE(Token::Value peek()) { return scanner_.peek(); }
  INLINE(Token::Value Next()) { return scanner_.Next(); }
  INLINE(void Consume(Token::Value token));
  void Expect(Token::Value token, bool* ok);
  void ExpectSemicolon(bool* ok);

  // Get odd-ball literals.
  Literal* GetLiteralUndefined();
  Literal* GetLiteralTheHole();
  Literal* GetLiteralNumber(double value);

  Handle<String> ParseIdentifier(bool* ok);
  Handle<String> ParseIdentifierOrGetOrSet(bool* is_get,
                                           bool* is_set,
                                           bool* ok);

  // Parser support
  virtual VariableProxy* Declare(Handle<String> name, Variable::Mode mode,
                                 FunctionLiteral* fun,
                                 bool resolve,
                                 bool* ok) = 0;

  bool TargetStackContainsLabel(Handle<String> label);
  BreakableStatement* LookupBreakTarget(Handle<String> label, bool* ok);
  IterationStatement* LookupContinueTarget(Handle<String> label, bool* ok);

  void RegisterLabelUse(Label* label, int index);

  // Create a number literal.
  Literal* NewNumberLiteral(double value);

  // Generate AST node that throw a ReferenceError with the given type.
  Expression* NewThrowReferenceError(Handle<String> type);

  // Generate AST node that throw a SyntaxError with the given
  // type. The first argument may be null (in the handle sense) in
  // which case no arguments are passed to the constructor.
  Expression* NewThrowSyntaxError(Handle<String> type, Handle<Object> first);

  // Generate AST node that throw a TypeError with the given
  // type. Both arguments must be non-null (in the handle sense).
  Expression* NewThrowTypeError(Handle<String> type,
                                Handle<Object> first,
                                Handle<Object> second);

  // Generic AST generator for throwing errors from compiled code.
  Expression* NewThrowError(Handle<String> constructor,
                            Handle<String> type,
                            Vector< Handle<Object> > arguments);

  friend class Target;
  friend class TargetScope;
  friend class LexicalScope;
  friend class TemporaryScope;
};


// A temporary scope stores information during parsing, just like
// a plain scope.  However, temporary scopes are not kept around
// after parsing or referenced by syntax trees so they can be stack-
// allocated and hence used by the pre-parser.
class TemporaryScope BASE_EMBEDDED {
 public:
  explicit TemporaryScope(Parser* parser);
  ~TemporaryScope();

  int NextMaterializedLiteralIndex() {
    int next_index =
        materialized_literal_count_ + JSFunction::kLiteralsPrefixSize;
    materialized_literal_count_++;
    return next_index;
  }
  int materialized_literal_count() { return materialized_literal_count_; }

  void set_contains_array_literal() { contains_array_literal_ = true; }
  bool contains_array_literal() { return contains_array_literal_; }

  void AddProperty() { expected_property_count_++; }
  int expected_property_count() { return expected_property_count_; }
 private:
  // Captures the number of nodes that need materialization in the
  // function.  regexp literals, and boilerplate for object literals.
  int materialized_literal_count_;

  // Captures whether or not the function contains array literals.  If
  // the function contains array literals, we have to allocate space
  // for the array constructor in the literals array of the function.
  // This array constructor is used when creating the actual array
  // literals.
  bool contains_array_literal_;

  // Properties count estimation.
  int expected_property_count_;

  // Bookkeeping
  Parser* parser_;
  TemporaryScope* parent_;

  friend class Parser;
};


TemporaryScope::TemporaryScope(Parser* parser)
  : materialized_literal_count_(0),
    contains_array_literal_(false),
    expected_property_count_(0),
    parser_(parser),
    parent_(parser->temp_scope_) {
  parser->temp_scope_ = this;
}


TemporaryScope::~TemporaryScope() {
  parser_->temp_scope_ = parent_;
}


// A zone list wrapper lets code either access a access a zone list
// or appear to do so while actually ignoring all operations.
template <typename T>
class ZoneListWrapper {
 public:
  ZoneListWrapper() : list_(NULL) { }
  explicit ZoneListWrapper(int size) : list_(new ZoneList<T*>(size)) { }
  void Add(T* that) { if (list_) list_->Add(that); }
  int length() { return list_->length(); }
  ZoneList<T*>* elements() { return list_; }
  T* at(int index) { return list_->at(index); }
 private:
  ZoneList<T*>* list_;
};


// Allocation macro that should be used to allocate objects that must
// only be allocated in real parsing mode.  Note that in preparse mode
// not only is the syntax tree not created but the constructor
// arguments are not evaulated.
#define NEW(expr) (is_pre_parsing_ ? NULL : new expr)


class ParserFactory BASE_EMBEDDED {
 public:
  explicit ParserFactory(bool is_pre_parsing) :
      is_pre_parsing_(is_pre_parsing) { }

  virtual ~ParserFactory() { }

  virtual Scope* NewScope(Scope* parent, Scope::Type type, bool inside_with);

  virtual Handle<String> LookupSymbol(const char* string, int length) {
    return Handle<String>();
  }

  virtual Handle<String> EmptySymbol() {
    return Handle<String>();
  }

  virtual Expression* NewProperty(Expression* obj, Expression* key, int pos) {
    if (obj == VariableProxySentinel::this_proxy()) {
      return Property::this_property();
    } else {
      return ValidLeftHandSideSentinel::instance();
    }
  }

  virtual Expression* NewCall(Expression* expression,
                              ZoneList<Expression*>* arguments,
                              bool is_eval, int pos) {
    return Call::sentinel();
  }

  virtual Statement* EmptyStatement() {
    return NULL;
  }

  template <typename T> ZoneListWrapper<T> NewList(int size) {
    return is_pre_parsing_ ? ZoneListWrapper<T>() : ZoneListWrapper<T>(size);
  }

 private:
  bool is_pre_parsing_;
};


class ParserLog BASE_EMBEDDED {
 public:
  virtual ~ParserLog() { }

  // Records the occurrence of a function.  The returned object is
  // only guaranteed to be valid until the next function has been
  // logged.
  virtual FunctionEntry LogFunction(int start) { return FunctionEntry(); }

  virtual void LogError() { }
};


class AstBuildingParserFactory : public ParserFactory {
 public:
  AstBuildingParserFactory() : ParserFactory(false) { }

  virtual Scope* NewScope(Scope* parent, Scope::Type type, bool inside_with);

  virtual Handle<String> LookupSymbol(const char* string, int length) {
    return Factory::LookupSymbol(Vector<const char>(string, length));
  }

  virtual Handle<String> EmptySymbol() {
    return Factory::empty_symbol();
  }

  virtual Expression* NewProperty(Expression* obj, Expression* key, int pos) {
    return new Property(obj, key, pos);
  }

  virtual Expression* NewCall(Expression* expression,
                              ZoneList<Expression*>* arguments,
                              bool is_eval, int pos) {
    return new Call(expression, arguments, is_eval, pos);
  }

  virtual Statement* EmptyStatement() {
    // Use a statically allocated empty statement singleton to avoid
    // allocating lots and lots of empty statements.
    static v8::internal::EmptyStatement empty;
    return &empty;
  }
};


class ParserRecorder: public ParserLog {
 public:
  ParserRecorder();
  virtual FunctionEntry LogFunction(int start);
  virtual void LogError() { }
  virtual void LogMessage(Scanner::Location loc,
                          const char* message,
                          Vector<const char*> args);
  void WriteString(Vector<const char> str);
  static const char* ReadString(unsigned* start, int* chars);
  List<unsigned>* store() { return &store_; }
 private:
  bool has_error_;
  List<unsigned> store_;
};


FunctionEntry ScriptDataImpl::GetFunctionEnd(int start) {
  if (nth(last_entry_).start_pos() > start) {
    // If the last entry we looked up is higher than what we're
    // looking for then it's useless and we reset it.
    last_entry_ = 0;
  }
  for (int i = last_entry_; i < EntryCount(); i++) {
    FunctionEntry entry = nth(i);
    if (entry.start_pos() == start) {
      last_entry_ = i;
      return entry;
    }
  }
  return FunctionEntry();
}


bool ScriptDataImpl::SanityCheck() {
  if (store_.length() < static_cast<int>(ScriptDataImpl::kHeaderSize))
    return false;
  if (magic() != ScriptDataImpl::kMagicNumber)
    return false;
  if (version() != ScriptDataImpl::kCurrentVersion)
    return false;
  return true;
}


int ScriptDataImpl::EntryCount() {
  return (store_.length() - kHeaderSize) / FunctionEntry::kSize;
}


FunctionEntry ScriptDataImpl::nth(int n) {
  int offset = kHeaderSize + n * FunctionEntry::kSize;
  return FunctionEntry(Vector<unsigned>(store_.start() + offset,
                                        FunctionEntry::kSize));
}


ParserRecorder::ParserRecorder()
  : has_error_(false), store_(4) {
  Vector<unsigned> preamble = store()->AddBlock(0, ScriptDataImpl::kHeaderSize);
  preamble[ScriptDataImpl::kMagicOffset] = ScriptDataImpl::kMagicNumber;
  preamble[ScriptDataImpl::kVersionOffset] = ScriptDataImpl::kCurrentVersion;
  preamble[ScriptDataImpl::kHasErrorOffset] = false;
}


void ParserRecorder::WriteString(Vector<const char> str) {
  store()->Add(str.length());
  for (int i = 0; i < str.length(); i++)
    store()->Add(str[i]);
}


const char* ParserRecorder::ReadString(unsigned* start, int* chars) {
  int length = start[0];
  char* result = NewArray<char>(length + 1);
  for (int i = 0; i < length; i++)
    result[i] = start[i + 1];
  result[length] = '\0';
  if (chars != NULL) *chars = length;
  return result;
}


void ParserRecorder::LogMessage(Scanner::Location loc, const char* message,
                                Vector<const char*> args) {
  if (has_error_) return;
  store()->Rewind(ScriptDataImpl::kHeaderSize);
  store()->at(ScriptDataImpl::kHasErrorOffset) = true;
  store()->Add(loc.beg_pos);
  store()->Add(loc.end_pos);
  store()->Add(args.length());
  WriteString(CStrVector(message));
  for (int i = 0; i < args.length(); i++)
    WriteString(CStrVector(args[i]));
}


Scanner::Location ScriptDataImpl::MessageLocation() {
  int beg_pos = Read(0);
  int end_pos = Read(1);
  return Scanner::Location(beg_pos, end_pos);
}


const char* ScriptDataImpl::BuildMessage() {
  unsigned* start = ReadAddress(3);
  return ParserRecorder::ReadString(start, NULL);
}


Vector<const char*> ScriptDataImpl::BuildArgs() {
  int arg_count = Read(2);
  const char** array = NewArray<const char*>(arg_count);
  int pos = ScriptDataImpl::kHeaderSize + Read(3);
  for (int i = 0; i < arg_count; i++) {
    int count = 0;
    array[i] = ParserRecorder::ReadString(ReadAddress(pos), &count);
    pos += count + 1;
  }
  return Vector<const char*>(array, arg_count);
}


unsigned ScriptDataImpl::Read(int position) {
  return store_[ScriptDataImpl::kHeaderSize + position];
}


unsigned* ScriptDataImpl::ReadAddress(int position) {
  return &store_[ScriptDataImpl::kHeaderSize + position];
}


FunctionEntry ParserRecorder::LogFunction(int start) {
  if (has_error_) return FunctionEntry();
  FunctionEntry result(store()->AddBlock(0, FunctionEntry::kSize));
  result.set_start_pos(start);
  return result;
}


class AstBuildingParser : public Parser {
 public:
  AstBuildingParser(Handle<Script> script, bool allow_natives_syntax,
                    v8::Extension* extension, ScriptDataImpl* pre_data)
      : Parser(script, allow_natives_syntax, extension, false,
               factory(), log(), pre_data) { }
  virtual void ReportMessageAt(Scanner::Location loc, const char* message,
                               Vector<const char*> args);
  virtual VariableProxy* Declare(Handle<String> name, Variable::Mode mode,
                                 FunctionLiteral* fun, bool resolve, bool* ok);
  AstBuildingParserFactory* factory() { return &factory_; }
  ParserLog* log() { return &log_; }

 private:
  ParserLog log_;
  AstBuildingParserFactory factory_;
};


class PreParser : public Parser {
 public:
  PreParser(Handle<Script> script, bool allow_natives_syntax,
            v8::Extension* extension)
      : Parser(script, allow_natives_syntax, extension, true,
               factory(), recorder(), NULL)
      , factory_(true) { }
  virtual void ReportMessageAt(Scanner::Location loc, const char* message,
                               Vector<const char*> args);
  virtual VariableProxy* Declare(Handle<String> name, Variable::Mode mode,
                                 FunctionLiteral* fun, bool resolve, bool* ok);
  ParserFactory* factory() { return &factory_; }
  ParserRecorder* recorder() { return &recorder_; }

 private:
  ParserRecorder recorder_;
  ParserFactory factory_;
};


Scope* AstBuildingParserFactory::NewScope(Scope* parent, Scope::Type type,
                                          bool inside_with) {
  Scope* result = new Scope(parent, type);
  result->Initialize(inside_with);
  return result;
}


Scope* ParserFactory::NewScope(Scope* parent, Scope::Type type,
                               bool inside_with) {
  ASSERT(parent != NULL);
  parent->type_ = type;
  return parent;
}


VariableProxy* PreParser::Declare(Handle<String> name, Variable::Mode mode,
                                  FunctionLiteral* fun, bool resolve,
                                  bool* ok) {
  return NULL;
}



// ----------------------------------------------------------------------------
// Target is a support class to facilitate manipulation of the
// Parser's target_stack_ (the stack of potential 'break' and
// 'continue' statement targets). Upon construction, a new target is
// added; it is removed upon destruction.

class Target BASE_EMBEDDED {
 public:
  Target(Parser* parser, Node* node) : parser_(parser) {
    parser_->target_stack_->Add(node);
  }

  ~Target() {
    parser_->target_stack_->RemoveLast();
  }

 private:
  Parser* parser_;
};


class TargetScope BASE_EMBEDDED {
 public:
  explicit TargetScope(Parser* parser)
      : parser_(parser), previous_(parser->target_stack_), stack_(0) {
    parser_->target_stack_ = &stack_;
  }

  ~TargetScope() {
    ASSERT(stack_.is_empty());
    parser_->target_stack_ = previous_;
  }

 private:
  Parser* parser_;
  List<Node*>* previous_;
  List<Node*> stack_;
};


// ----------------------------------------------------------------------------
// LexicalScope is a support class to facilitate manipulation of the
// Parser's scope stack. The constructor sets the parser's top scope
// to the incoming scope, and the destructor resets it.

class LexicalScope BASE_EMBEDDED {
 public:
  LexicalScope(Parser* parser, Scope* scope)
    : parser_(parser),
      prev_scope_(parser->top_scope_),
      prev_level_(parser->with_nesting_level_) {
    parser_->top_scope_ = scope;
    parser_->with_nesting_level_ = 0;
  }

  ~LexicalScope() {
    parser_->top_scope_ = prev_scope_;
    parser_->with_nesting_level_ = prev_level_;
  }

 private:
  Parser* parser_;
  Scope* prev_scope_;
  int prev_level_;
};


// ----------------------------------------------------------------------------
// The CHECK_OK macro is a convenient macro to enforce error
// handling for functions that may fail (by returning !*ok).
//
// CAUTION: This macro appends extra statements after a call,
// thus it must never be used where only a single statement
// is correct (e.g. an if statement branch w/o braces)!

#define CHECK_OK  ok);   \
  if (!*ok) return NULL; \
  ((void)0
#define DUMMY )  // to make indentation work
#undef DUMMY


// ----------------------------------------------------------------------------
// Implementation of Parser

Parser::Parser(Handle<Script> script,
               bool allow_natives_syntax,
               v8::Extension* extension,
               bool is_pre_parsing,
               ParserFactory* factory,
               ParserLog* log,
               ScriptDataImpl* pre_data)
    : script_(script),
      scanner_(is_pre_parsing),
      top_scope_(NULL),
      with_nesting_level_(0),
      temp_scope_(NULL),
      target_stack_(NULL),
      allow_natives_syntax_(allow_natives_syntax),
      extension_(extension),
      factory_(factory),
      log_(log),
      is_pre_parsing_(is_pre_parsing),
      pre_data_(pre_data) {
}


bool Parser::PreParseProgram(unibrow::CharacterStream* stream) {
  StatsRateScope timer(&Counters::pre_parse);
  StackGuard guard;
  AssertNoZoneAllocation assert_no_zone_allocation;
  AssertNoAllocation assert_no_allocation;
  NoHandleAllocation no_handle_allocation;
  scanner_.Init(Handle<String>(), stream, 0);
  ASSERT(target_stack_ == NULL);
  mode_ = PARSE_EAGERLY;
  DummyScope top_scope;
  LexicalScope scope(this, &top_scope);
  TemporaryScope temp_scope(this);
  ZoneListWrapper<Statement> processor;
  bool ok = true;
  ParseSourceElements(&processor, Token::EOS, &ok);
  return !scanner().stack_overflow();
}


FunctionLiteral* Parser::ParseProgram(Handle<String> source,
                                      unibrow::CharacterStream* stream,
                                      bool in_global_context) {
  StatsRateScope timer(&Counters::parse);
  Counters::total_parse_size.Increment(source->length());

  // Initialize parser state.
  source->TryFlatten();
  scanner_.Init(source, stream, 0);
  ASSERT(target_stack_ == NULL);

  // Compute the parsing mode.
  mode_ = FLAG_lazy ? PARSE_LAZILY : PARSE_EAGERLY;
  if (allow_natives_syntax_ || extension_ != NULL) mode_ = PARSE_EAGERLY;

  Scope::Type type =
    in_global_context
      ? Scope::GLOBAL_SCOPE
      : Scope::EVAL_SCOPE;
  Handle<String> no_name = factory()->EmptySymbol();

  FunctionLiteral* result = NULL;
  { Scope* scope = factory()->NewScope(top_scope_, type, inside_with());
    LexicalScope lexical_scope(this, scope);
    TemporaryScope temp_scope(this);
    ZoneListWrapper<Statement> body(16);
    bool ok = true;
    ParseSourceElements(&body, Token::EOS, &ok);
    if (ok) {
      result = NEW(FunctionLiteral(no_name, top_scope_,
                                   body.elements(),
                                   temp_scope.materialized_literal_count(),
                                   temp_scope.contains_array_literal(),
                                   temp_scope.expected_property_count(),
                                   0, 0, source->length(), false));
    } else if (scanner().stack_overflow()) {
      Top::StackOverflow();
    }
  }

  // Make sure the target stack is empty.
  ASSERT(target_stack_ == NULL);

  // If there was a syntax error we have to get rid of the AST
  // and it is not safe to do so before the scope has been deleted.
  if (result == NULL) Zone::DeleteAll();
  return result;
}


FunctionLiteral* Parser::ParseLazy(Handle<String> source,
                                   Handle<String> name,
                                   int start_position,
                                   bool is_expression) {
  StatsRateScope timer(&Counters::parse_lazy);
  Counters::total_parse_size.Increment(source->length());
  SafeStringInputBuffer buffer(source.location());

  // Initialize parser state.
  source->TryFlatten();
  scanner_.Init(source, &buffer, start_position);
  ASSERT(target_stack_ == NULL);
  mode_ = PARSE_EAGERLY;

  // Place holder for the result.
  FunctionLiteral* result = NULL;

  {
    // Parse the function literal.
    Handle<String> no_name = factory()->EmptySymbol();
    Scope* scope =
        factory()->NewScope(top_scope_, Scope::GLOBAL_SCOPE, inside_with());
    LexicalScope lexical_scope(this, scope);
    TemporaryScope temp_scope(this);

    FunctionLiteralType type = is_expression ? EXPRESSION : DECLARATION;
    bool ok = true;
    result = ParseFunctionLiteral(name, kNoPosition, type, &ok);
    // Make sure the results agree.
    ASSERT(ok == (result != NULL));
    // The only errors should be stack overflows.
    ASSERT(ok || scanner_.stack_overflow());
  }

  // Make sure the target stack is empty.
  ASSERT(target_stack_ == NULL);

  // If there was a stack overflow we have to get rid of AST and it is
  // not safe to do before scope has been deleted.
  if (result == NULL) {
    Top::StackOverflow();
    Zone::DeleteAll();
  }
  return result;
}


void Parser::ReportMessage(const char* type, Vector<const char*> args) {
  Scanner::Location source_location = scanner_.location();
  ReportMessageAt(source_location, type, args);
}


void AstBuildingParser::ReportMessageAt(Scanner::Location source_location,
                                        const char* type,
                                        Vector<const char*> args) {
  MessageLocation location(script_,
                           source_location.beg_pos, source_location.end_pos);
  Handle<JSArray> array = Factory::NewJSArray(args.length());
  for (int i = 0; i < args.length(); i++) {
    SetElement(array, i, Factory::NewStringFromUtf8(CStrVector(args[i])));
  }
  Handle<Object> result = Factory::NewSyntaxError(type, array);
  Top::Throw(*result, &location);
}


void PreParser::ReportMessageAt(Scanner::Location source_location,
                                const char* type,
                                Vector<const char*> args) {
  recorder()->LogMessage(source_location, type, args);
}


void* Parser::ParseSourceElements(ZoneListWrapper<Statement>* processor,
                                  int end_token,
                                  bool* ok) {
  // SourceElements ::
  //   (Statement)* <end_token>

  // Allocate a target stack to use for this set of source
  // elements. This way, all scripts and functions get their own
  // target stack thus avoiding illegal breaks and continues across
  // functions.
  TargetScope scope(this);

  ASSERT(processor != NULL);
  while (peek() != end_token) {
    Statement* stat = ParseStatement(NULL, CHECK_OK);
    if (stat && !stat->IsEmpty()) processor->Add(stat);
  }
  return 0;
}


Statement* Parser::ParseStatement(ZoneStringList* labels, bool* ok) {
  // Statement ::
  //   Block
  //   VariableStatement
  //   EmptyStatement
  //   ExpressionStatement
  //   IfStatement
  //   IterationStatement
  //   ContinueStatement
  //   BreakStatement
  //   ReturnStatement
  //   WithStatement
  //   LabelledStatement
  //   SwitchStatement
  //   ThrowStatement
  //   TryStatement
  //   DebuggerStatement

  // Note: Since labels can only be used by 'break' and 'continue'
  // statements, which themselves are only valid within blocks,
  // iterations or 'switch' statements (i.e., BreakableStatements),
  // labels can be simply ignored in all other cases; except for
  // trivial labelled break statements 'label: break label' which is
  // parsed into an empty statement.

  // Keep the source position of the statement
  int statement_pos = scanner().peek_location().beg_pos;
  Statement* stmt = NULL;
  switch (peek()) {
    case Token::LBRACE:
      return ParseBlock(labels, ok);

    case Token::CONST:  // fall through
    case Token::VAR:
      stmt = ParseVariableStatement(ok);
      break;

    case Token::SEMICOLON:
      Next();
      return factory()->EmptyStatement();

    case Token::IF:
      stmt = ParseIfStatement(labels, ok);
      break;

    case Token::DO:
      stmt = ParseDoStatement(labels, ok);
      break;

    case Token::WHILE:
      stmt = ParseWhileStatement(labels, ok);
      break;

    case Token::FOR:
      stmt = ParseForStatement(labels, ok);
      break;

    case Token::CONTINUE:
      stmt = ParseContinueStatement(ok);
      break;

    case Token::BREAK:
      stmt = ParseBreakStatement(labels, ok);
      break;

    case Token::RETURN:
      stmt = ParseReturnStatement(ok);
      break;

    case Token::WITH:
      stmt = ParseWithStatement(labels, ok);
      break;

    case Token::SWITCH:
      stmt = ParseSwitchStatement(labels, ok);
      break;

    case Token::THROW:
      stmt = ParseThrowStatement(ok);
      break;

    case Token::TRY: {
      // NOTE: It is somewhat complicated to have labels on
      // try-statements. When breaking out of a try-finally statement,
      // one must take great care not to treat it as a
      // fall-through. It is much easier just to wrap the entire
      // try-statement in a statement block and put the labels there
      Block* result = NEW(Block(labels, 1, false));
      Target target(this, result);
      TryStatement* statement = ParseTryStatement(CHECK_OK);
      if (result) result->AddStatement(statement);
      return result;
    }

    case Token::FUNCTION:
      return ParseFunctionDeclaration(ok);

    case Token::NATIVE:
      return ParseNativeDeclaration(ok);

    case Token::DEBUGGER:
      stmt = ParseDebuggerStatement(ok);
      break;

    default:
      stmt = ParseExpressionOrLabelledStatement(labels, ok);
  }

  // Store the source position of the statement
  if (stmt != NULL) stmt->set_statement_pos(statement_pos);
  return stmt;
}


VariableProxy* AstBuildingParser::Declare(Handle<String> name,
                                          Variable::Mode mode,
                                          FunctionLiteral* fun,
                                          bool resolve,
                                          bool* ok) {
  Variable* var = NULL;
  // If we are inside a function, a declaration of a variable
  // is a truly local variable, and the scope of the variable
  // is always the function scope.

  // If a function scope exists, then we can statically declare this
  // variable and also set its mode. In any case, a Declaration node
  // will be added to the scope so that the declaration can be added
  // to the corresponding activation frame at runtime if necessary.
  // For instance declarations inside an eval scope need to be added
  // to the calling function context.
  if (top_scope_->is_function_scope()) {
    // Declare the variable in the function scope.
    var = top_scope_->Lookup(name);
    if (var == NULL) {
      // Declare the name.
      var = top_scope_->Declare(name, mode);
    } else {
      // The name was declared before; check for conflicting
      // re-declarations. If the previous declaration was a const or the
      // current declaration is a const then we have a conflict. There is
      // similar code in runtime.cc in the Declare functions.
      if ((mode == Variable::CONST) || (var->mode() == Variable::CONST)) {
        // We only have vars and consts in declarations.
        ASSERT(var->mode() == Variable::VAR ||
               var->mode() == Variable::CONST);
        const char* type = (var->mode() == Variable::VAR) ? "var" : "const";
        Handle<String> type_string =
            Factory::NewStringFromUtf8(CStrVector(type), TENURED);
        Expression* expression =
            NewThrowTypeError(Factory::redeclaration_symbol(),
                              type_string, name);
        top_scope_->SetIllegalRedeclaration(expression);
      }
    }
  }

  // We add a declaration node for every declaration. The compiler
  // will only generate code if necessary. In particular, declarations
  // for inner local variables that do not represent functions won't
  // result in any generated code.
  //
  // Note that we always add an unresolved proxy even if it's not
  // used, simply because we don't know in this method (w/o extra
  // parameters) if the proxy is needed or not. The proxy will be
  // bound during variable resolution time unless it was pre-bound
  // below.
  //
  // WARNING: This will lead to multiple declaration nodes for the
  // same variable if it is declared several times. This is not a
  // semantic issue as long as we keep the source order, but it may be
  // a performance issue since it may lead to repeated
  // Runtime::DeclareContextSlot() calls.
  VariableProxy* proxy = top_scope_->NewUnresolved(name, inside_with());
  top_scope_->AddDeclaration(NEW(Declaration(proxy, mode, fun)));

  // For global const variables we bind the proxy to a variable.
  if (mode == Variable::CONST && top_scope_->is_global_scope()) {
    ASSERT(resolve);  // should be set by all callers
    var = NEW(Variable(top_scope_, name, Variable::CONST, true, false));
  }

  // If requested and we have a local variable, bind the proxy to the variable
  // at parse-time. This is used for functions (and consts) declared inside
  // statements: the corresponding function (or const) variable must be in the
  // function scope and not a statement-local scope, e.g. as provided with a
  // 'with' statement:
  //
  //   with (obj) {
  //     function f() {}
  //   }
  //
  // which is translated into:
  //
  //   with (obj) {
  //     // in this case this is not: 'var f; f = function () {};'
  //     var f = function () {};
  //   }
  //
  // Note that if 'f' is accessed from inside the 'with' statement, it
  // will be allocated in the context (because we must be able to look
  // it up dynamically) but it will also be accessed statically, i.e.,
  // with a context slot index and a context chain length for this
  // initialization code. Thus, inside the 'with' statement, we need
  // both access to the static and the dynamic context chain; the
  // runtime needs to provide both.
  if (resolve && var != NULL) proxy->BindTo(var);

  return proxy;
}


// Language extension which is only enabled for source files loaded
// through the API's extension mechanism.  A native function
// declaration is resolved by looking up the function through a
// callback provided by the extension.
Statement* Parser::ParseNativeDeclaration(bool* ok) {
  if (extension_ == NULL) {
    ReportUnexpectedToken(Token::NATIVE);
    *ok = false;
    return NULL;
  }

  Expect(Token::NATIVE, CHECK_OK);
  Expect(Token::FUNCTION, CHECK_OK);
  Handle<String> name = ParseIdentifier(CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  bool done = (peek() == Token::RPAREN);
  while (!done) {
    ParseIdentifier(CHECK_OK);
    done = (peek() == Token::RPAREN);
    if (!done) Expect(Token::COMMA, CHECK_OK);
  }
  Expect(Token::RPAREN, CHECK_OK);
  Expect(Token::SEMICOLON, CHECK_OK);

  if (is_pre_parsing_) return NULL;

  // Make sure that the function containing the native declaration
  // isn't lazily compiled. The extension structures are only
  // accessible while parsing the first time not when reparsing
  // because of lazy compilation.
  top_scope_->ForceEagerCompilation();

  // Compute the function template for the native function.
  v8::Handle<v8::FunctionTemplate> fun_template =
      extension_->GetNativeFunction(v8::Utils::ToLocal(name));
  ASSERT(!fun_template.IsEmpty());

  // Instantiate the function and create a boilerplate function from it.
  Handle<JSFunction> fun = Utils::OpenHandle(*fun_template->GetFunction());
  const int literals = fun->NumberOfLiterals();
  Handle<Code> code = Handle<Code>(fun->shared()->code());
  Handle<JSFunction> boilerplate =
      Factory::NewFunctionBoilerplate(name, literals, false, code);

  // Copy the function data to the boilerplate. Used by
  // builtins.cc:HandleApiCall to perform argument type checks and to
  // find the right native code to call.
  boilerplate->shared()->set_function_data(fun->shared()->function_data());

  // TODO(1240846): It's weird that native function declarations are
  // introduced dynamically when we meet their declarations, whereas
  // other functions are setup when entering the surrounding scope.
  FunctionBoilerplateLiteral* lit =
      NEW(FunctionBoilerplateLiteral(boilerplate));
  VariableProxy* var = Declare(name, Variable::VAR, NULL, true, CHECK_OK);
  return NEW(ExpressionStatement(
                 new Assignment(Token::INIT_VAR, var, lit, kNoPosition)));
}


Statement* Parser::ParseFunctionDeclaration(bool* ok) {
  // Parse a function literal. We may or may not have a function name.
  // If we have a name we use it as the variable name for the function
  // (a function declaration) and not as the function name of a function
  // expression.

  Expect(Token::FUNCTION, CHECK_OK);
  int function_token_position = scanner().location().beg_pos;

  Handle<String> name;
  if (peek() == Token::IDENTIFIER) name = ParseIdentifier(CHECK_OK);
  FunctionLiteral* fun = ParseFunctionLiteral(name, function_token_position,
                                              DECLARATION, CHECK_OK);

  if (name.is_null()) {
    // We don't have a name - it is always an anonymous function
    // expression.
    return NEW(ExpressionStatement(fun));
  } else {
    // We have a name so even if we're not at the top-level of the
    // global or a function scope, we treat is as such and introduce
    // the function with it's initial value upon entering the
    // corresponding scope.
    Declare(name, Variable::VAR, fun, true, CHECK_OK);
    return factory()->EmptyStatement();
  }
}


Block* Parser::ParseBlock(ZoneStringList* labels, bool* ok) {
  // Block ::
  //   '{' Statement* '}'

  // Note that a Block does not introduce a new execution scope!
  // (ECMA-262, 3rd, 12.2)
  //
  // Construct block expecting 16 statements.
  Block* result = NEW(Block(labels, 16, false));
  Target target(this, result);
  Expect(Token::LBRACE, CHECK_OK);
  while (peek() != Token::RBRACE) {
    Statement* stat = ParseStatement(NULL, CHECK_OK);
    if (stat && !stat->IsEmpty()) result->AddStatement(stat);
  }
  Expect(Token::RBRACE, CHECK_OK);
  return result;
}


Block* Parser::ParseVariableStatement(bool* ok) {
  // VariableStatement ::
  //   VariableDeclarations ';'

  Expression* dummy;  // to satisfy the ParseVariableDeclarations() signature
  Block* result = ParseVariableDeclarations(true, &dummy, CHECK_OK);
  ExpectSemicolon(CHECK_OK);
  return result;
}


// If the variable declaration declares exactly one non-const
// variable, then *var is set to that variable. In all other cases,
// *var is untouched; in particular, it is the caller's responsibility
// to initialize it properly. This mechanism is used for the parsing
// of 'for-in' loops.
Block* Parser::ParseVariableDeclarations(bool accept_IN,
                                         Expression** var,
                                         bool* ok) {
  // VariableDeclarations ::
  //   ('var' | 'const') (Identifier ('=' AssignmentExpression)?)+[',']

  Variable::Mode mode = Variable::VAR;
  bool is_const = false;
  if (peek() == Token::VAR) {
    Consume(Token::VAR);
  } else if (peek() == Token::CONST) {
    Consume(Token::CONST);
    mode = Variable::CONST;
    is_const = true;
  } else {
    UNREACHABLE();  // by current callers
  }

  // The scope of a variable/const declared anywhere inside a function
  // is the entire function (ECMA-262, 3rd, 10.1.3, and 12.2). Thus we can
  // transform a source-level variable/const declaration into a (Function)
  // Scope declaration, and rewrite the source-level initialization into an
  // assignment statement. We use a block to collect multiple assignments.
  //
  // We mark the block as initializer block because we don't want the
  // rewriter to add a '.result' assignment to such a block (to get compliant
  // behavior for code such as print(eval('var x = 7')), and for cosmetic
  // reasons when pretty-printing. Also, unless an assignment (initialization)
  // is inside an initializer block, it is ignored.
  //
  // Create new block with one expected declaration.
  Block* block = NEW(Block(NULL, 1, true));
  VariableProxy* last_var = NULL;  // the last variable declared
  int nvars = 0;  // the number of variables declared
  do {
    // Parse variable name.
    if (nvars > 0) Consume(Token::COMMA);
    Handle<String> name = ParseIdentifier(CHECK_OK);

    // Declare variable.
    // Note that we *always* must treat the initial value via a separate init
    // assignment for variables and constants because the value must be assigned
    // when the variable is encountered in the source. But the variable/constant
    // is declared (and set to 'undefined') upon entering the function within
    // which the variable or constant is declared. Only function variables have
    // an initial value in the declaration (because they are initialized upon
    // entering the function).
    //
    // If we have a const declaration, in an inner scope, the proxy is always
    // bound to the declared variable (independent of possibly surrounding with
    // statements).
    last_var = Declare(name, mode, NULL,
                       is_const /* always bound for CONST! */,
                       CHECK_OK);
    nvars++;

    // Parse initialization expression if present and/or needed. A
    // declaration of the form:
    //
    //    var v = x;
    //
    // is syntactic sugar for:
    //
    //    var v; v = x;
    //
    // In particular, we need to re-lookup 'v' as it may be a
    // different 'v' than the 'v' in the declaration (if we are inside
    // a 'with' statement that makes a object property with name 'v'
    // visible).
    //
    // However, note that const declarations are different! A const
    // declaration of the form:
    //
    //   const c = x;
    //
    // is *not* syntactic sugar for:
    //
    //   const c; c = x;
    //
    // The "variable" c initialized to x is the same as the declared
    // one - there is no re-lookup (see the last parameter of the
    // Declare() call above).

    Expression* value = NULL;
    int position = -1;
    if (peek() == Token::ASSIGN) {
      Expect(Token::ASSIGN, CHECK_OK);
      position = scanner().location().beg_pos;
      value = ParseAssignmentExpression(accept_IN, CHECK_OK);
    }

    // Make sure that 'const c' actually initializes 'c' to undefined
    // even though it seems like a stupid thing to do.
    if (value == NULL && is_const) {
      value = GetLiteralUndefined();
    }

    // Global variable declarations must be compiled in a specific
    // way. When the script containing the global variable declaration
    // is entered, the global variable must be declared, so that if it
    // doesn't exist (not even in a prototype of the global object) it
    // gets created with an initial undefined value. This is handled
    // by the declarations part of the function representing the
    // top-level global code; see Runtime::DeclareGlobalVariable. If
    // it already exists (in the object or in a prototype), it is
    // *not* touched until the variable declaration statement is
    // executed.
    //
    // Executing the variable declaration statement will always
    // guarantee to give the global object a "local" variable; a
    // variable defined in the global object and not in any
    // prototype. This way, global variable declarations can shadow
    // properties in the prototype chain, but only after the variable
    // declaration statement has been executed. This is important in
    // browsers where the global object (window) has lots of
    // properties defined in prototype objects.

    if (!is_pre_parsing_ && top_scope_->is_global_scope()) {
      // Compute the arguments for the runtime call.
      ZoneList<Expression*>* arguments = new ZoneList<Expression*>(2);
      // Be careful not to assign a value to the global variable if
      // we're in a with. The initialization value should not
      // necessarily be stored in the global object in that case,
      // which is why we need to generate a separate assignment node.
      arguments->Add(NEW(Literal(name)));  // we have at least 1 parameter
      if (is_const || (value != NULL && !inside_with())) {
        arguments->Add(value);
        value = NULL;  // zap the value to avoid the unnecessary assignment
      }
      // Construct the call to Runtime::DeclareGlobal{Variable,Const}Locally
      // and add it to the initialization statement block. Note that
      // this function does different things depending on if we have
      // 1 or 2 parameters.
      CallRuntime* initialize;
      if (is_const) {
        initialize =
          NEW(CallRuntime(
            Factory::InitializeConstGlobal_symbol(),
            Runtime::FunctionForId(Runtime::kInitializeConstGlobal),
            arguments));
      } else {
        initialize =
          NEW(CallRuntime(
            Factory::InitializeVarGlobal_symbol(),
            Runtime::FunctionForId(Runtime::kInitializeVarGlobal),
            arguments));
      }
      block->AddStatement(NEW(ExpressionStatement(initialize)));
    }

    // Add an assignment node to the initialization statement block if
    // we still have a pending initialization value. We must distinguish
    // between variables and constants: Variable initializations are simply
    // assignments (with all the consequences if they are inside a 'with'
    // statement - they may change a 'with' object property). Constant
    // initializations always assign to the declared constant which is
    // always at the function scope level. This is only relevant for
    // dynamically looked-up variables and constants (the start context
    // for constant lookups is always the function context, while it is
    // the top context for variables). Sigh...
    if (value != NULL) {
      Token::Value op = (is_const ? Token::INIT_CONST : Token::INIT_VAR);
      Assignment* assignment = NEW(Assignment(op, last_var, value, position));
      if (block) block->AddStatement(NEW(ExpressionStatement(assignment)));
    }
  } while (peek() == Token::COMMA);

  if (!is_const && nvars == 1) {
    // We have a single, non-const variable.
    if (is_pre_parsing_) {
      // If we're preparsing then we need to set the var to something
      // in order for for-in loops to parse correctly.
      *var = ValidLeftHandSideSentinel::instance();
    } else {
      ASSERT(last_var != NULL);
      *var = last_var;
    }
  }

  return block;
}


static bool ContainsLabel(ZoneStringList* labels, Handle<String> label) {
  ASSERT(!label.is_null());
  if (labels != NULL)
    for (int i = labels->length(); i-- > 0; )
      if (labels->at(i).is_identical_to(label))
        return true;

  return false;
}


Statement* Parser::ParseExpressionOrLabelledStatement(ZoneStringList* labels,
                                                      bool* ok) {
  // ExpressionStatement | LabelledStatement ::
  //   Expression ';'
  //   Identifier ':' Statement

  Expression* expr = ParseExpression(true, CHECK_OK);
  if (peek() == Token::COLON && expr &&
      expr->AsVariableProxy() != NULL &&
      !expr->AsVariableProxy()->is_this()) {
    VariableProxy* var = expr->AsVariableProxy();
    Handle<String> label = var->name();
    // TODO(1240780): We don't check for redeclaration of labels
    // during preparsing since keeping track of the set of active
    // labels requires nontrivial changes to the way scopes are
    // structured.  However, these are probably changes we want to
    // make later anyway so we should go back and fix this then.
    if (!is_pre_parsing_) {
      if (ContainsLabel(labels, label) || TargetStackContainsLabel(label)) {
        SmartPointer<char> c_string = label->ToCString(DISALLOW_NULLS);
        const char* elms[2] = { "Label", *c_string };
        Vector<const char*> args(elms, 2);
        ReportMessage("redeclaration", args);
        *ok = false;
        return NULL;
      }
      if (labels == NULL) labels = new ZoneStringList(4);
      labels->Add(label);
      // Remove the "ghost" variable that turned out to be a label
      // from the top scope. This way, we don't try to resolve it
      // during the scope processing.
      top_scope_->RemoveUnresolved(var);
    }
    Expect(Token::COLON, CHECK_OK);
    return ParseStatement(labels, ok);
  }

  // Parsed expression statement.
  ExpectSemicolon(CHECK_OK);
  return NEW(ExpressionStatement(expr));
}


IfStatement* Parser::ParseIfStatement(ZoneStringList* labels, bool* ok) {
  // IfStatement ::
  //   'if' '(' Expression ')' Statement ('else' Statement)?

  Expect(Token::IF, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  Expression* condition = ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);
  Statement* then_statement = ParseStatement(labels, CHECK_OK);
  Statement* else_statement = NULL;
  if (peek() == Token::ELSE) {
    Next();
    else_statement = ParseStatement(labels, CHECK_OK);
  } else if (!is_pre_parsing_) {
    else_statement = factory()->EmptyStatement();
  }
  return NEW(IfStatement(condition, then_statement, else_statement));
}


Statement* Parser::ParseContinueStatement(bool* ok) {
  // ContinueStatement ::
  //   'continue' Identifier? ';'

  Expect(Token::CONTINUE, CHECK_OK);
  Handle<String> label(static_cast<String**>(NULL));
  Token::Value tok = peek();
  if (!scanner_.has_line_terminator_before_next() &&
      tok != Token::SEMICOLON && tok != Token::RBRACE) {
    label = ParseIdentifier(CHECK_OK);
  }
  IterationStatement* target = NULL;
  if (!is_pre_parsing_) {
    target = LookupContinueTarget(label, CHECK_OK);
    if (target == NULL) {
      // Illegal continue statement.  To be consistent with KJS we delay
      // reporting of the syntax error until runtime.
      Handle<String> error_type = Factory::illegal_continue_symbol();
      if (!label.is_null()) error_type = Factory::unknown_label_symbol();
      Expression* throw_error = NewThrowSyntaxError(error_type, label);
      return NEW(ExpressionStatement(throw_error));
    }
  }
  ExpectSemicolon(CHECK_OK);
  return NEW(ContinueStatement(target));
}


Statement* Parser::ParseBreakStatement(ZoneStringList* labels, bool* ok) {
  // BreakStatement ::
  //   'break' Identifier? ';'

  Expect(Token::BREAK, CHECK_OK);
  Handle<String> label;
  Token::Value tok = peek();
  if (!scanner_.has_line_terminator_before_next() &&
      tok != Token::SEMICOLON && tok != Token::RBRACE) {
    label = ParseIdentifier(CHECK_OK);
  }
  // Parse labelled break statements that target themselves into
  // empty statements, e.g. 'l1: l2: l3: break l2;'
  if (!label.is_null() && ContainsLabel(labels, label)) {
    return factory()->EmptyStatement();
  }
  BreakableStatement* target = NULL;
  if (!is_pre_parsing_) {
    target = LookupBreakTarget(label, CHECK_OK);
    if (target == NULL) {
      // Illegal break statement.  To be consistent with KJS we delay
      // reporting of the syntax error until runtime.
      Handle<String> error_type = Factory::illegal_break_symbol();
      if (!label.is_null()) error_type = Factory::unknown_label_symbol();
      Expression* throw_error = NewThrowSyntaxError(error_type, label);
      return NEW(ExpressionStatement(throw_error));
    }
  }
  ExpectSemicolon(CHECK_OK);
  return NEW(BreakStatement(target));
}


Statement* Parser::ParseReturnStatement(bool* ok) {
  // ReturnStatement ::
  //   'return' Expression? ';'

  // Consume the return token. It is necessary to do the before
  // reporting any errors on it, because of the way errors are
  // reported (underlining).
  Expect(Token::RETURN, CHECK_OK);

  // An ECMAScript program is considered syntactically incorrect if it
  // contains a return statement that is not within the body of a
  // function. See ECMA-262, section 12.9, page 67.
  //
  // To be consistent with KJS we report the syntax error at runtime.
  if (!is_pre_parsing_ && !top_scope_->is_function_scope()) {
    Handle<String> type = Factory::illegal_return_symbol();
    Expression* throw_error = NewThrowSyntaxError(type, Handle<Object>::null());
    return NEW(ExpressionStatement(throw_error));
  }

  Token::Value tok = peek();
  if (scanner_.has_line_terminator_before_next() ||
      tok == Token::SEMICOLON ||
      tok == Token::RBRACE ||
      tok == Token::EOS) {
    ExpectSemicolon(CHECK_OK);
    return NEW(ReturnStatement(GetLiteralUndefined()));
  }

  Expression* expr = ParseExpression(true, CHECK_OK);
  ExpectSemicolon(CHECK_OK);
  return NEW(ReturnStatement(expr));
}


Block* Parser::WithHelper(Expression* obj, ZoneStringList* labels, bool* ok) {
  // Parse the statement and collect escaping labels.
  ZoneList<Label*>* label_list = NEW(ZoneList<Label*>(0));
  LabelCollector collector(label_list);
  Statement* stat;
  { Target target(this, &collector);
    with_nesting_level_++;
    top_scope_->RecordWithStatement();
    stat = ParseStatement(labels, CHECK_OK);
    with_nesting_level_--;
  }
  // Create resulting block with two statements.
  // 1: Evaluate the with expression.
  // 2: The try-finally block evaluating the body.
  Block* result = NEW(Block(NULL, 2, false));

  if (result) {
    result->AddStatement(NEW(WithEnterStatement(obj)));

    // Create body block.
    Block* body = NEW(Block(NULL, 1, false));
    body->AddStatement(stat);

    // Create exit block.
    Block* exit = NEW(Block(NULL, 1, false));
    exit->AddStatement(NEW(WithExitStatement()));

    // Return a try-finally statement.
    TryFinally* wrapper = NEW(TryFinally(body, NULL, exit));
    wrapper->set_escaping_labels(collector.labels());
    result->AddStatement(wrapper);
    return result;
  } else {
    return NULL;
  }
}


Statement* Parser::ParseWithStatement(ZoneStringList* labels, bool* ok) {
  // WithStatement ::
  //   'with' '(' Expression ')' Statement

  // We do not allow the use of 'with' statements in the internal JS
  // code. If 'with' statements were allowed, the simplified setup of
  // the runtime context chain would allow access to properties in the
  // global object from within a 'with' statement.
  ASSERT(!Bootstrapper::IsActive());

  Expect(Token::WITH, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  Expression* expr = ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);

  return WithHelper(expr, labels, CHECK_OK);
}


CaseClause* Parser::ParseCaseClause(bool* default_seen_ptr, bool* ok) {
  // CaseClause ::
  //   'case' Expression ':' Statement*
  //   'default' ':' Statement*

  Expression* label = NULL;  // NULL expression indicates default case
  if (peek() == Token::CASE) {
    Expect(Token::CASE, CHECK_OK);
    label = ParseExpression(true, CHECK_OK);
  } else {
    Expect(Token::DEFAULT, CHECK_OK);
    if (*default_seen_ptr) {
      ReportMessage("multiple_defaults_in_switch",
                    Vector<const char*>::empty());
      *ok = false;
      return NULL;
    }
    *default_seen_ptr = true;
  }
  Expect(Token::COLON, CHECK_OK);

  ZoneListWrapper<Statement> statements = factory()->NewList<Statement>(5);
  while (peek() != Token::CASE &&
         peek() != Token::DEFAULT &&
         peek() != Token::RBRACE) {
    Statement* stat = ParseStatement(NULL, CHECK_OK);
    statements.Add(stat);
  }

  return NEW(CaseClause(label, statements.elements()));
}


SwitchStatement* Parser::ParseSwitchStatement(ZoneStringList* labels,
                                              bool* ok) {
  // SwitchStatement ::
  //   'switch' '(' Expression ')' '{' CaseClause* '}'

  SwitchStatement* statement = NEW(SwitchStatement(labels));
  Target target(this, statement);

  Expect(Token::SWITCH, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  Expression* tag = ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);

  bool default_seen = false;
  ZoneListWrapper<CaseClause> cases = factory()->NewList<CaseClause>(4);
  Expect(Token::LBRACE, CHECK_OK);
  while (peek() != Token::RBRACE) {
    CaseClause* clause = ParseCaseClause(&default_seen, CHECK_OK);
    cases.Add(clause);
  }
  Expect(Token::RBRACE, CHECK_OK);

  if (statement) statement->Initialize(tag, cases.elements());
  return statement;
}


Statement* Parser::ParseThrowStatement(bool* ok) {
  // ThrowStatement ::
  //   'throw' Expression ';'

  Expect(Token::THROW, CHECK_OK);
  int pos = scanner().location().beg_pos;
  if (scanner_.has_line_terminator_before_next()) {
    ReportMessage("newline_after_throw", Vector<const char*>::empty());
    *ok = false;
    return NULL;
  }
  Expression* exception = ParseExpression(true, CHECK_OK);
  ExpectSemicolon(CHECK_OK);

  return NEW(ExpressionStatement(new Throw(exception, pos)));
}


Expression* Parser::MakeCatchContext(Handle<String> id, VariableProxy* value) {
  ZoneListWrapper<ObjectLiteral::Property> properties =
      factory()->NewList<ObjectLiteral::Property>(1);
  Literal* key = NEW(Literal(id));
  ObjectLiteral::Property* property = NEW(ObjectLiteral::Property(key, value));
  properties.Add(property);

  // This must be called always, even during pre-parsing!
  // (Computation of literal index must happen before pre-parse bailout.)
  int literal_index = temp_scope_->NextMaterializedLiteralIndex();
  if (is_pre_parsing_) {
    return NULL;
  }

  // Construct the expression for calling Runtime::CreateObjectLiteral
  // with the literal array as argument.
  Handle<FixedArray> constant_properties = Factory::empty_fixed_array();
  ZoneList<Expression*>* arguments = new ZoneList<Expression*>(1);
  arguments->Add(new Literal(constant_properties));

  return new ObjectLiteral(constant_properties,
                           properties.elements(),
                           literal_index);
}


TryStatement* Parser::ParseTryStatement(bool* ok) {
  // TryStatement ::
  //   'try' Block Catch
  //   'try' Block Finally
  //   'try' Block Catch Finally
  //
  // Catch ::
  //   'catch' '(' Identifier ')' Block
  //
  // Finally ::
  //   'finally' Block

  Expect(Token::TRY, CHECK_OK);

  ZoneList<Label*>* label_list = NEW(ZoneList<Label*>(0));
  LabelCollector collector(label_list);
  Block* try_block;

  { Target target(this, &collector);
    try_block = ParseBlock(NULL, CHECK_OK);
  }

  Block* catch_block = NULL;
  VariableProxy* catch_var = NULL;
  Block* finally_block = NULL;

  Token::Value tok = peek();
  if (tok != Token::CATCH && tok != Token::FINALLY) {
    ReportMessage("no_catch_or_finally", Vector<const char*>::empty());
    *ok = false;
    return NULL;
  }

  // If we can break out from the catch block and there is a finally block,
  // then we will need to collect labels from the catch block. Since we don't
  // know yet if there will be a finally block, we always collect the labels.
  ZoneList<Label*>* catch_label_list = NEW(ZoneList<Label*>(0));
  LabelCollector catch_collector(catch_label_list);
  bool has_catch = false;
  if (tok == Token::CATCH) {
    has_catch = true;
    Consume(Token::CATCH);

    Expect(Token::LPAREN, CHECK_OK);
    Handle<String> name = ParseIdentifier(CHECK_OK);
    Expect(Token::RPAREN, CHECK_OK);

    if (peek() == Token::LBRACE) {
      // Allocate a temporary for holding the finally state while
      // executing the finally block.
      catch_var = top_scope_->NewTemporary(Factory::catch_var_symbol());
      Expression* obj = MakeCatchContext(name, catch_var);
      { Target target(this, &catch_collector);
        catch_block = WithHelper(obj, NULL, CHECK_OK);
      }
    } else {
      Expect(Token::LBRACE, CHECK_OK);
    }

    tok = peek();
  }

  VariableProxy* finally_var = NULL;
  if (tok == Token::FINALLY || !has_catch) {
    Consume(Token::FINALLY);
    // Declare a variable for holding the finally state while
    // executing the finally block.
    finally_var = top_scope_->NewTemporary(Factory::finally_state_symbol());
    finally_block = ParseBlock(NULL, CHECK_OK);
  }

  // Simplify the AST nodes by converting:
  //   'try { } catch { } finally { }'
  // to:
  //   'try { try { } catch { } } finally { }'

  if (!is_pre_parsing_ && catch_block != NULL && finally_block != NULL) {
    TryCatch* statement = NEW(TryCatch(try_block, catch_var, catch_block));
    statement->set_escaping_labels(collector.labels());
    try_block = NEW(Block(NULL, 1, false));
    try_block->AddStatement(statement);
    catch_block = NULL;
  }

  TryStatement* result = NULL;
  if (!is_pre_parsing_) {
    if (catch_block != NULL) {
      ASSERT(finally_block == NULL);
      result = NEW(TryCatch(try_block, catch_var, catch_block));
      result->set_escaping_labels(collector.labels());
    } else {
      ASSERT(finally_block != NULL);
      result = NEW(TryFinally(try_block, finally_var, finally_block));
      // Add the labels of the try block and the catch block.
      for (int i = 0; i < collector.labels()->length(); i++) {
        catch_collector.labels()->Add(collector.labels()->at(i));
      }
      result->set_escaping_labels(catch_collector.labels());
    }
  }

  return result;
}


LoopStatement* Parser::ParseDoStatement(ZoneStringList* labels, bool* ok) {
  // DoStatement ::
  //   'do' Statement 'while' '(' Expression ')' ';'

  LoopStatement* loop = NEW(LoopStatement(labels, LoopStatement::DO_LOOP));
  Target target(this, loop);

  Expect(Token::DO, CHECK_OK);
  Statement* body = ParseStatement(NULL, CHECK_OK);
  Expect(Token::WHILE, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  Expression* cond = ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);

  // Allow do-statements to be terminated with and without
  // semi-colons. This allows code such as 'do;while(0)return' to
  // parse, which would not be the case if we had used the
  // ExpectSemicolon() functionality here.
  if (peek() == Token::SEMICOLON) Consume(Token::SEMICOLON);

  if (loop) loop->Initialize(NULL, cond, NULL, body);
  return loop;
}


LoopStatement* Parser::ParseWhileStatement(ZoneStringList* labels, bool* ok) {
  // WhileStatement ::
  //   'while' '(' Expression ')' Statement

  LoopStatement* loop = NEW(LoopStatement(labels, LoopStatement::WHILE_LOOP));
  Target target(this, loop);

  Expect(Token::WHILE, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  Expression* cond = ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);
  Statement* body = ParseStatement(NULL, CHECK_OK);

  if (loop) loop->Initialize(NULL, cond, NULL, body);
  return loop;
}


Statement* Parser::ParseForStatement(ZoneStringList* labels, bool* ok) {
  // ForStatement ::
  //   'for' '(' Expression? ';' Expression? ';' Expression? ')' Statement

  Statement* init = NULL;

  Expect(Token::FOR, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  if (peek() != Token::SEMICOLON) {
    if (peek() == Token::VAR || peek() == Token::CONST) {
      Expression* each = NULL;
      Block* variable_statement =
          ParseVariableDeclarations(false, &each, CHECK_OK);
      if (peek() == Token::IN && each != NULL) {
        ForInStatement* loop = NEW(ForInStatement(labels));
        Target target(this, loop);

        Expect(Token::IN, CHECK_OK);
        Expression* enumerable = ParseExpression(true, CHECK_OK);
        Expect(Token::RPAREN, CHECK_OK);

        Statement* body = ParseStatement(NULL, CHECK_OK);
        if (is_pre_parsing_) {
          return NULL;
        } else {
          loop->Initialize(each, enumerable, body);
          Block* result = NEW(Block(NULL, 2, false));
          result->AddStatement(variable_statement);
          result->AddStatement(loop);
          // Parsed for-in loop w/ variable/const declaration.
          return result;
        }

      } else {
        init = variable_statement;
      }

    } else {
      Expression* expression = ParseExpression(false, CHECK_OK);
      if (peek() == Token::IN) {
        // Report syntax error if the expression is an invalid
        // left-hand side expression.
        if (expression == NULL || !expression->IsValidLeftHandSide()) {
          if (expression != NULL && expression->AsCall() != NULL) {
            // According to ECMA-262 host function calls are permitted to
            // return references.  This cannot happen in our system so we
            // will always get an error.  We could report this as a syntax
            // error here but for compatibility with KJS and SpiderMonkey we
            // choose to report the error at runtime.
            Handle<String> type = Factory::invalid_lhs_in_for_in_symbol();
            expression = NewThrowReferenceError(type);
          } else {
            // Invalid left hand side expressions that are not function
            // calls are reported as syntax errors at compile time.
            ReportMessage("invalid_lhs_in_for_in",
                          Vector<const char*>::empty());
            *ok = false;
            return NULL;
          }
        }
        ForInStatement* loop = NEW(ForInStatement(labels));
        Target target(this, loop);

        Expect(Token::IN, CHECK_OK);
        Expression* enumerable = ParseExpression(true, CHECK_OK);
        Expect(Token::RPAREN, CHECK_OK);

        Statement* body = ParseStatement(NULL, CHECK_OK);
        if (loop) loop->Initialize(expression, enumerable, body);

        // Parsed for-in loop.
        return loop;

      } else {
        init = NEW(ExpressionStatement(expression));
      }
    }
  }

  // Standard 'for' loop
  LoopStatement* loop = NEW(LoopStatement(labels, LoopStatement::FOR_LOOP));
  Target target(this, loop);

  // Parsed initializer at this point.
  Expect(Token::SEMICOLON, CHECK_OK);

  Expression* cond = NULL;
  if (peek() != Token::SEMICOLON) {
    cond = ParseExpression(true, CHECK_OK);
  }
  Expect(Token::SEMICOLON, CHECK_OK);

  Statement* next = NULL;
  if (peek() != Token::RPAREN) {
    Expression* exp = ParseExpression(true, CHECK_OK);
    next = NEW(ExpressionStatement(exp));
  }
  Expect(Token::RPAREN, CHECK_OK);

  Statement* body = ParseStatement(NULL, CHECK_OK);

  if (loop) loop->Initialize(init, cond, next, body);
  return loop;
}


// Precedence = 1
Expression* Parser::ParseExpression(bool accept_IN, bool* ok) {
  // Expression ::
  //   AssignmentExpression
  //   Expression ',' AssignmentExpression

  Expression* result = ParseAssignmentExpression(accept_IN, CHECK_OK);
  while (peek() == Token::COMMA) {
    Expect(Token::COMMA, CHECK_OK);
    Expression* right = ParseAssignmentExpression(accept_IN, CHECK_OK);
    result = NEW(BinaryOperation(Token::COMMA, result, right));
  }
  return result;
}


// Precedence = 2
Expression* Parser::ParseAssignmentExpression(bool accept_IN, bool* ok) {
  // AssignmentExpression ::
  //   ConditionalExpression
  //   LeftHandSideExpression AssignmentOperator AssignmentExpression

  Expression* expression = ParseConditionalExpression(accept_IN, CHECK_OK);

  if (!Token::IsAssignmentOp(peek())) {
    // Parsed conditional expression only (no assignment).
    return expression;
  }

  if (expression == NULL || !expression->IsValidLeftHandSide()) {
    if (expression != NULL && expression->AsCall() != NULL) {
      // According to ECMA-262 host function calls are permitted to
      // return references.  This cannot happen in our system so we
      // will always get an error.  We could report this as a syntax
      // error here but for compatibility with KJS and SpiderMonkey we
      // choose to report the error at runtime.
      Handle<String> type = Factory::invalid_lhs_in_assignment_symbol();
      expression = NewThrowReferenceError(type);
    } else {
      // Invalid left hand side expressions that are not function
      // calls are reported as syntax errors at compile time.
      //
      // NOTE: KJS sometimes delay the error reporting to runtime. If
      // we want to be completely compatible we should do the same.
      // For example: "(x++) = 42" gives a reference error at runtime
      // with KJS whereas we report a syntax error at compile time.
      ReportMessage("invalid_lhs_in_assignment", Vector<const char*>::empty());
      *ok = false;
      return NULL;
    }
  }


  Token::Value op = Next();  // Get assignment operator.
  int pos = scanner().location().beg_pos;
  Expression* right = ParseAssignmentExpression(accept_IN, CHECK_OK);

  // TODO(1231235): We try to estimate the set of properties set by
  // constructors. We define a new property whenever there is an
  // assignment to a property of 'this'. We should probably only add
  // properties if we haven't seen them before. Otherwise we'll
  // probably overestimate the number of properties.
  Property* property = expression ? expression->AsProperty() : NULL;
  if (op == Token::ASSIGN &&
      property != NULL &&
      property->obj()->AsVariableProxy() != NULL &&
      property->obj()->AsVariableProxy()->is_this()) {
    temp_scope_->AddProperty();
  }

  return NEW(Assignment(op, expression, right, pos));
}


// Precedence = 3
Expression* Parser::ParseConditionalExpression(bool accept_IN, bool* ok) {
  // ConditionalExpression ::
  //   LogicalOrExpression
  //   LogicalOrExpression '?' AssignmentExpression ':' AssignmentExpression

  // We start using the binary expression parser for prec >= 4 only!
  Expression* expression = ParseBinaryExpression(4, accept_IN, CHECK_OK);
  if (peek() != Token::CONDITIONAL) return expression;
  Consume(Token::CONDITIONAL);
  // In parsing the first assignment expression in conditional
  // expressions we always accept the 'in' keyword; see ECMA-262,
  // section 11.12, page 58.
  Expression* left = ParseAssignmentExpression(true, CHECK_OK);
  Expect(Token::COLON, CHECK_OK);
  Expression* right = ParseAssignmentExpression(accept_IN, CHECK_OK);
  return NEW(Conditional(expression, left, right));
}


static int Precedence(Token::Value tok, bool accept_IN) {
  if (tok == Token::IN && !accept_IN)
    return 0;  // 0 precedence will terminate binary expression parsing

  return Token::Precedence(tok);
}


// Precedence >= 4
Expression* Parser::ParseBinaryExpression(int prec, bool accept_IN, bool* ok) {
  ASSERT(prec >= 4);
  Expression* x = ParseUnaryExpression(CHECK_OK);
  for (int prec1 = Precedence(peek(), accept_IN); prec1 >= prec; prec1--) {
    // prec1 >= 4
    while (Precedence(peek(), accept_IN) == prec1) {
      Token::Value op = Next();
      Expression* y = ParseBinaryExpression(prec1 + 1, accept_IN, CHECK_OK);

      // Compute some expressions involving only number literals.
      if (x && x->AsLiteral() && x->AsLiteral()->handle()->IsNumber() &&
          y && y->AsLiteral() && y->AsLiteral()->handle()->IsNumber()) {
        double x_val = x->AsLiteral()->handle()->Number();
        double y_val = y->AsLiteral()->handle()->Number();

        switch (op) {
          case Token::ADD:
            x = NewNumberLiteral(x_val + y_val);
            continue;
          case Token::SUB:
            x = NewNumberLiteral(x_val - y_val);
            continue;
          case Token::MUL:
            x = NewNumberLiteral(x_val * y_val);
            continue;
          case Token::DIV:
            x = NewNumberLiteral(x_val / y_val);
            continue;
          case Token::BIT_OR:
            x = NewNumberLiteral(DoubleToInt32(x_val) | DoubleToInt32(y_val));
            continue;
          case Token::BIT_AND:
            x = NewNumberLiteral(DoubleToInt32(x_val) & DoubleToInt32(y_val));
            continue;
          case Token::BIT_XOR:
            x = NewNumberLiteral(DoubleToInt32(x_val) ^ DoubleToInt32(y_val));
            continue;
          case Token::SHL: {
            int value = DoubleToInt32(x_val) << (DoubleToInt32(y_val) & 0x1f);
            x = NewNumberLiteral(value);
            continue;
          }
          case Token::SHR: {
            uint32_t shift = DoubleToInt32(y_val) & 0x1f;
            uint32_t value = DoubleToUint32(x_val) >> shift;
            x = NewNumberLiteral(value);
            continue;
          }
          case Token::SAR: {
            uint32_t shift = DoubleToInt32(y_val) & 0x1f;
            int value = ArithmeticShiftRight(DoubleToInt32(x_val), shift);
            x = NewNumberLiteral(value);
            continue;
          }
          default:
            break;
        }
      }

      // For now we distinguish between comparisons and other binary
      // operations.  (We could combine the two and get rid of this
      // code an AST node eventually.)
      if (Token::IsCompareOp(op)) {
        // We have a comparison.
        Token::Value cmp = op;
        switch (op) {
          case Token::NE: cmp = Token::EQ; break;
          case Token::NE_STRICT: cmp = Token::EQ_STRICT; break;
          default: break;
        }
        x = NEW(CompareOperation(cmp, x, y));
        if (cmp != op) {
          // The comparison was negated - add a NOT.
          x = NEW(UnaryOperation(Token::NOT, x));
        }

      } else {
        // We have a "normal" binary operation.
        x = NEW(BinaryOperation(op, x, y));
      }
    }
  }
  return x;
}


Expression* Parser::ParseUnaryExpression(bool* ok) {
  // UnaryExpression ::
  //   PostfixExpression
  //   'delete' UnaryExpression
  //   'void' UnaryExpression
  //   'typeof' UnaryExpression
  //   '++' UnaryExpression
  //   '--' UnaryExpression
  //   '+' UnaryExpression
  //   '-' UnaryExpression
  //   '~' UnaryExpression
  //   '!' UnaryExpression

  Token::Value op = peek();
  if (Token::IsUnaryOp(op)) {
    op = Next();
    Expression* x = ParseUnaryExpression(CHECK_OK);

    // Compute some expressions involving only number literals.
    if (x && x->AsLiteral() && x->AsLiteral()->handle()->IsNumber()) {
      double x_val = x->AsLiteral()->handle()->Number();
      switch (op) {
        case Token::ADD:
          return x;
        case Token::SUB:
          return NewNumberLiteral(-x_val);
        case Token::BIT_NOT:
          return NewNumberLiteral(~DoubleToInt32(x_val));
        default: break;
      }
    }

    return NEW(UnaryOperation(op, x));

  } else if (Token::IsCountOp(op)) {
    op = Next();
    Expression* x = ParseUnaryExpression(CHECK_OK);
    if (x == NULL || !x->IsValidLeftHandSide()) {
      if (x != NULL && x->AsCall() != NULL) {
        // According to ECMA-262 host function calls are permitted to
        // return references.  This cannot happen in our system so we
        // will always get an error.  We could report this as a syntax
        // error here but for compatibility with KJS and SpiderMonkey we
        // choose to report the error at runtime.
        Handle<String> type = Factory::invalid_lhs_in_prefix_op_symbol();
        x = NewThrowReferenceError(type);
      } else {
        // Invalid left hand side expressions that are not function
        // calls are reported as syntax errors at compile time.
        ReportMessage("invalid_lhs_in_prefix_op", Vector<const char*>::empty());
        *ok = false;
        return NULL;
      }
    }
    return NEW(CountOperation(true /* prefix */, op, x));

  } else {
    return ParsePostfixExpression(ok);
  }
}


Expression* Parser::ParsePostfixExpression(bool* ok) {
  // PostfixExpression ::
  //   LeftHandSideExpression ('++' | '--')?

  Expression* result = ParseLeftHandSideExpression(CHECK_OK);
  if (!scanner_.has_line_terminator_before_next() && Token::IsCountOp(peek())) {
    if (result == NULL || !result->IsValidLeftHandSide()) {
      if (result != NULL && result->AsCall() != NULL) {
        // According to ECMA-262 host function calls are permitted to
        // return references.  This cannot happen in our system so we
        // will always get an error.  We could report this as a syntax
        // error here but for compatibility with KJS and SpiderMonkey we
        // choose to report the error at runtime.
        Handle<String> type = Factory::invalid_lhs_in_postfix_op_symbol();
        result = NewThrowReferenceError(type);
      } else {
        // Invalid left hand side expressions that are not function
        // calls are reported as syntax errors at compile time.
        ReportMessage("invalid_lhs_in_postfix_op",
                      Vector<const char*>::empty());
        *ok = false;
        return NULL;
      }
    }
    Token::Value next = Next();
    result = NEW(CountOperation(false /* postfix */, next, result));
  }
  return result;
}


Expression* Parser::ParseLeftHandSideExpression(bool* ok) {
  // LeftHandSideExpression ::
  //   (NewExpression | MemberExpression) ...

  Expression* result;
  if (peek() == Token::NEW) {
    result = ParseNewExpression(CHECK_OK);
  } else {
    result = ParseMemberExpression(CHECK_OK);
  }

  while (true) {
    switch (peek()) {
      case Token::LBRACK: {
        Consume(Token::LBRACK);
        int pos = scanner().location().beg_pos;
        Expression* index = ParseExpression(true, CHECK_OK);
        result = factory()->NewProperty(result, index, pos);
        Expect(Token::RBRACK, CHECK_OK);
        break;
      }

      case Token::LPAREN: {
        int pos = scanner().location().beg_pos;
        ZoneList<Expression*>* args = ParseArguments(CHECK_OK);

        // Keep track of eval() calls since they disable all local variable
        // optimizations. We can ignore locally declared variables with
        // name 'eval' since they override the global 'eval' function. We
        // only need to look at unresolved variables (VariableProxies).

        if (!is_pre_parsing_) {
          // We assume that only a function called 'eval' can be used
          // to invoke the global eval() implementation. This permits
          // for massive optimizations.
          VariableProxy* callee = result->AsVariableProxy();
          if (callee != NULL && callee->IsVariable(Factory::eval_symbol())) {
            // We do not allow direct calls to 'eval' in our internal
            // JS files. Use builtin functions instead.
            ASSERT(!Bootstrapper::IsActive());
            top_scope_->RecordEvalCall();
          } else {
            // This is rather convoluted code to check if we're calling
            // a function named 'eval' through a property access. If so,
            // we mark it as a possible eval call (we don't know if the
            // receiver will resolve to the global object or not), but
            // we do not treat the call as an eval() call - we let the
            // call get through to the JavaScript eval code defined in
            // v8natives.js.
            Property* property = result->AsProperty();
            if (property != NULL) {
              Literal* key = property->key()->AsLiteral();
              if (key != NULL &&
                  key->handle().is_identical_to(Factory::eval_symbol())) {
                // We do not allow direct calls to 'eval' in our
                // internal JS files. Use builtin functions instead.
                ASSERT(!Bootstrapper::IsActive());
                top_scope_->RecordEvalCall();
              }
            }
          }
        }

        // Optimize the eval() case w/o arguments so we
        // don't need to handle it every time at runtime.
        //
        // Note: For now we don't do static eval analysis
        // as it appears that we need to be able to call
        // eval() via alias names. We leave the code as
        // is, in case we want to enable this again in the
        // future.
        const bool is_eval = false;
        if (is_eval && args->length() == 0) {
          result = NEW(Literal(Factory::undefined_value()));
        } else {
          result = factory()->NewCall(result, args, is_eval, pos);
        }
        break;
      }

      case Token::PERIOD: {
        Consume(Token::PERIOD);
        int pos = scanner().location().beg_pos;
        Handle<String> name = ParseIdentifier(CHECK_OK);
        result = factory()->NewProperty(result, NEW(Literal(name)), pos);
        break;
      }

      default:
        return result;
    }
  }
}


Expression* Parser::ParseNewExpression(bool* ok) {
  // NewExpression ::
  //   ('new')+ MemberExpression

  // The grammar for new expressions is pretty warped. The keyword
  // 'new' can either be a part of the new expression (where it isn't
  // followed by an argument list) or a part of the member expression,
  // where it must be followed by an argument list. To accommodate
  // this, we parse the 'new' keywords greedily and keep track of how
  // many we have parsed. This information is then passed on to the
  // member expression parser, which is only allowed to match argument
  // lists as long as it has 'new' prefixes left
  List<int> new_positions(4);
  while (peek() == Token::NEW) {
    Consume(Token::NEW);
    new_positions.Add(scanner().location().beg_pos);
  }
  ASSERT(new_positions.length() > 0);

  Expression* result =
      ParseMemberWithNewPrefixesExpression(&new_positions, CHECK_OK);
  while (!new_positions.is_empty()) {
    int last = new_positions.RemoveLast();
    result = NEW(CallNew(result, new ZoneList<Expression*>(0), last));
  }
  return result;
}


Expression* Parser::ParseMemberExpression(bool* ok) {
  static List<int> new_positions(0);
  return ParseMemberWithNewPrefixesExpression(&new_positions, ok);
}


Expression* Parser::ParseMemberWithNewPrefixesExpression(
    List<int>* new_positions,
    bool* ok) {
  // MemberExpression ::
  //   (PrimaryExpression | FunctionLiteral)
  //     ('[' Expression ']' | '.' Identifier | Arguments)*

  // Parse the initial primary or function expression.
  Expression* result = NULL;
  if (peek() == Token::FUNCTION) {
    Expect(Token::FUNCTION, CHECK_OK);
    int function_token_position = scanner().location().beg_pos;
    Handle<String> name;
    if (peek() == Token::IDENTIFIER) name = ParseIdentifier(CHECK_OK);
    result = ParseFunctionLiteral(name, function_token_position,
                                  NESTED, CHECK_OK);
  } else {
    result = ParsePrimaryExpression(CHECK_OK);
  }

  while (true) {
    switch (peek()) {
      case Token::LBRACK: {
        Consume(Token::LBRACK);
        int pos = scanner().location().beg_pos;
        Expression* index = ParseExpression(true, CHECK_OK);
        result = factory()->NewProperty(result, index, pos);
        Expect(Token::RBRACK, CHECK_OK);
        break;
      }
      case Token::PERIOD: {
        Consume(Token::PERIOD);
        int pos = scanner().location().beg_pos;
        Handle<String> name = ParseIdentifier(CHECK_OK);
        result = factory()->NewProperty(result, NEW(Literal(name)), pos);
        break;
      }
      case Token::LPAREN: {
        if (new_positions->is_empty()) return result;
        // Consume one of the new prefixes (already parsed).
        ZoneList<Expression*>* args = ParseArguments(CHECK_OK);
        int last = new_positions->RemoveLast();
        result = NEW(CallNew(result, args, last));
        break;
      }
      default:
        return result;
    }
  }
}


DebuggerStatement* Parser::ParseDebuggerStatement(bool* ok) {
  // In ECMA-262 'debugger' is defined as a reserved keyword. In some browser
  // contexts this is used as a statement which invokes the debugger as i a
  // break point is present.
  // DebuggerStatement ::
  //   'debugger' ';'

  Expect(Token::DEBUGGER, CHECK_OK);
  ExpectSemicolon(CHECK_OK);
  return NEW(DebuggerStatement());
}


void Parser::ReportUnexpectedToken(Token::Value token) {
  // We don't report stack overflows here, to avoid increasing the
  // stack depth even further.  Instead we report it after parsing is
  // over, in ParseProgram.
  if (token == Token::ILLEGAL && scanner().stack_overflow())
    return;
  // Four of the tokens are treated specially
  switch (token) {
  case Token::EOS:
    return ReportMessage("unexpected_eos", Vector<const char*>::empty());
  case Token::NUMBER:
    return ReportMessage("unexpected_token_number",
                         Vector<const char*>::empty());
  case Token::STRING:
    return ReportMessage("unexpected_token_string",
                         Vector<const char*>::empty());
  case Token::IDENTIFIER:
    return ReportMessage("unexpected_token_identifier",
                         Vector<const char*>::empty());
  default:
    const char* name = Token::String(token);
    ASSERT(name != NULL);
    ReportMessage("unexpected_token", Vector<const char*>(&name, 1));
  }
}


Expression* Parser::ParsePrimaryExpression(bool* ok) {
  // PrimaryExpression ::
  //   'this'
  //   'null'
  //   'true'
  //   'false'
  //   Identifier
  //   Number
  //   String
  //   ArrayLiteral
  //   ObjectLiteral
  //   RegExpLiteral
  //   '(' Expression ')'

  Expression* result = NULL;
  switch (peek()) {
    case Token::THIS: {
      Consume(Token::THIS);
      if (is_pre_parsing_) {
        result = VariableProxySentinel::this_proxy();
      } else {
        VariableProxy* recv = top_scope_->receiver();
        recv->var_uses()->RecordRead(1);
        result = recv;
      }
      break;
    }

    case Token::NULL_LITERAL:
      Consume(Token::NULL_LITERAL);
      result = NEW(Literal(Factory::null_value()));
      break;

    case Token::TRUE_LITERAL:
      Consume(Token::TRUE_LITERAL);
      result = NEW(Literal(Factory::true_value()));
      break;

    case Token::FALSE_LITERAL:
      Consume(Token::FALSE_LITERAL);
      result = NEW(Literal(Factory::false_value()));
      break;

    case Token::IDENTIFIER: {
      Handle<String> name = ParseIdentifier(CHECK_OK);
      if (is_pre_parsing_) {
        result = VariableProxySentinel::identifier_proxy();
      } else {
        result = top_scope_->NewUnresolved(name, inside_with());
      }
      break;
    }

    case Token::NUMBER: {
      Consume(Token::NUMBER);
      double value =
        StringToDouble(scanner_.literal_string(), ALLOW_HEX | ALLOW_OCTALS);
      result = NewNumberLiteral(value);
      break;
    }

    case Token::STRING: {
      Consume(Token::STRING);
      Handle<String> symbol =
          factory()->LookupSymbol(scanner_.literal_string(),
                                scanner_.literal_length());
      result = NEW(Literal(symbol));
      break;
    }

    case Token::ASSIGN_DIV:
      result = ParseRegExpLiteral(true, CHECK_OK);
      break;

    case Token::DIV:
      result = ParseRegExpLiteral(false, CHECK_OK);
      break;

    case Token::LBRACK:
      result = ParseArrayLiteral(CHECK_OK);
      break;

    case Token::LBRACE:
      result = ParseObjectLiteral(CHECK_OK);
      break;

    case Token::LPAREN:
      Consume(Token::LPAREN);
      result = ParseExpression(true, CHECK_OK);
      Expect(Token::RPAREN, CHECK_OK);
      break;

    case Token::MOD:
      if (allow_natives_syntax_ || extension_ != NULL) {
        result = ParseV8Intrinsic(CHECK_OK);
        break;
      }
      // If we're not allowing special syntax we fall-through to the
      // default case.

    default: {
      Token::Value tok = peek();
      // Token::Peek returns the value of the next token but
      // location() gives info about the current token.
      // Therefore, we need to read ahead to the next token
      Next();
      ReportUnexpectedToken(tok);
      *ok = false;
      return NULL;
    }
  }

  return result;
}


Expression* Parser::ParseArrayLiteral(bool* ok) {
  // ArrayLiteral ::
  //   '[' Expression? (',' Expression?)* ']'

  ZoneListWrapper<Expression> values = factory()->NewList<Expression>(4);
  Expect(Token::LBRACK, CHECK_OK);
  while (peek() != Token::RBRACK) {
    Expression* elem;
    if (peek() == Token::COMMA) {
      elem = GetLiteralTheHole();
    } else {
      elem = ParseAssignmentExpression(true, CHECK_OK);
    }
    values.Add(elem);
    if (peek() != Token::RBRACK) {
      Expect(Token::COMMA, CHECK_OK);
    }
  }
  Expect(Token::RBRACK, CHECK_OK);

  // Update the scope information before the pre-parsing bailout.
  temp_scope_->set_contains_array_literal();

  if (is_pre_parsing_) return NULL;

  // Allocate a fixed array with all the literals.
  Handle<FixedArray> literals =
      Factory::NewFixedArray(values.length(), TENURED);

  // Fill in the literals.
  for (int i = 0; i < values.length(); i++) {
    Literal* literal = values.at(i)->AsLiteral();
    if (literal == NULL) {
      literals->set_the_hole(i);
    } else {
      literals->set(i, *literal->handle());
    }
  }

  return NEW(ArrayLiteral(literals, values.elements()));
}


Expression* Parser::ParseObjectLiteral(bool* ok) {
  // ObjectLiteral ::
  //   '{' (
  //       ((Identifier | String | Number) ':' AssignmentExpression)
  //     | (('get' | 'set') FunctionLiteral)
  //    )*[','] '}'

  ZoneListWrapper<ObjectLiteral::Property> properties =
      factory()->NewList<ObjectLiteral::Property>(4);
  int number_of_constant_properties = 0;

  Expect(Token::LBRACE, CHECK_OK);
  while (peek() != Token::RBRACE) {
    Literal* key = NULL;
    switch (peek()) {
      case Token::IDENTIFIER: {
        // Store identifier keys as literal symbols to avoid
        // resolving them when compiling code for the object
        // literal.
        bool is_getter = false;
        bool is_setter = false;
        Handle<String> id =
            ParseIdentifierOrGetOrSet(&is_getter, &is_setter, CHECK_OK);
        if (is_getter || is_setter) {
          // Special handling of getter and setter syntax.
          if (peek() == Token::IDENTIFIER) {
            Handle<String> name = ParseIdentifier(CHECK_OK);
            FunctionLiteral* value =
                ParseFunctionLiteral(name, kNoPosition, DECLARATION, CHECK_OK);
            ObjectLiteral::Property* property =
                NEW(ObjectLiteral::Property(is_getter, value));
            properties.Add(property);
            if (peek() != Token::RBRACE) Expect(Token::COMMA, CHECK_OK);
            continue;  // restart the while
          }
        }
        key = NEW(Literal(id));
        break;
      }

      case Token::STRING: {
        Consume(Token::STRING);
        Handle<String> string =
          factory()->LookupSymbol(scanner_.literal_string(),
                                  scanner_.literal_length());
        uint32_t index;
        if (!string.is_null() && string->AsArrayIndex(&index)) {
          key = NewNumberLiteral(index);
        } else {
          key = NEW(Literal(string));
        }
        break;
      }

      case Token::NUMBER: {
        Consume(Token::NUMBER);
        double value =
          StringToDouble(scanner_.literal_string(), ALLOW_HEX | ALLOW_OCTALS);
        key = NewNumberLiteral(value);
        break;
      }

      default:
        Expect(Token::RBRACE, CHECK_OK);
        break;
    }

    Expect(Token::COLON, CHECK_OK);
    Expression* value = ParseAssignmentExpression(true, CHECK_OK);

    ObjectLiteral::Property* property =
        NEW(ObjectLiteral::Property(key, value));
    if ((property != NULL) &&
        property->kind() == ObjectLiteral::Property::CONSTANT) {
      number_of_constant_properties++;
    }
    properties.Add(property);

    // TODO(1240767): Consider allowing trailing comma.
    if (peek() != Token::RBRACE) Expect(Token::COMMA, CHECK_OK);
  }
  Expect(Token::RBRACE, CHECK_OK);
  // Computation of literal_index must happen before pre parse bailout.
  int literal_index = temp_scope_->NextMaterializedLiteralIndex();
  if (is_pre_parsing_) return NULL;

  Handle<FixedArray> constant_properties =
      Factory::NewFixedArray(number_of_constant_properties * 2, TENURED);
  int position = 0;
  for (int i = 0; i < properties.length(); i++) {
    ObjectLiteral::Property* property = properties.at(i);
    if (property->kind() == ObjectLiteral::Property::CONSTANT) {
      Handle<Object> key = property->key()->handle();
      Literal* literal = property->value()->AsLiteral();
      // Add name, value pair to the fixed array.
      constant_properties->set(position++, *key);
      constant_properties->set(position++, *literal->handle());
    }
  }

  // Construct the expression for calling Runtime::CreateObjectLiteral
  // with the literal array as argument.
  ZoneList<Expression*>* arguments = new ZoneList<Expression*>(1);
  arguments->Add(new Literal(constant_properties));
  return new ObjectLiteral(constant_properties,
                           properties.elements(),
                           literal_index);
}


Expression* Parser::ParseRegExpLiteral(bool seen_equal, bool* ok) {
  if (!scanner_.ScanRegExpPattern(seen_equal)) {
    Next();
    ReportMessage("unterminated_regexp", Vector<const char*>::empty());
    *ok = false;
    return NULL;
  }

  int literal_index = temp_scope_->NextMaterializedLiteralIndex();

  if (is_pre_parsing_) {
    // If we're preparsing we just do all the parsing stuff without
    // building anything.
    scanner_.ScanRegExpFlags();
    Next();
    return NULL;
  }

  Handle<String> js_pattern =
      Factory::NewStringFromUtf8(scanner_.next_literal(), TENURED);
  scanner_.ScanRegExpFlags();
  Handle<String> js_flags =
      Factory::NewStringFromUtf8(scanner_.next_literal(), TENURED);
  Next();

  return new RegExpLiteral(js_pattern, js_flags, literal_index);
}


ZoneList<Expression*>* Parser::ParseArguments(bool* ok) {
  // Arguments ::
  //   '(' (AssignmentExpression)*[','] ')'

  ZoneListWrapper<Expression> result = factory()->NewList<Expression>(4);
  Expect(Token::LPAREN, CHECK_OK);
  bool done = (peek() == Token::RPAREN);
  while (!done) {
    Expression* argument = ParseAssignmentExpression(true, CHECK_OK);
    result.Add(argument);
    done = (peek() == Token::RPAREN);
    if (!done) Expect(Token::COMMA, CHECK_OK);
  }
  Expect(Token::RPAREN, CHECK_OK);
  return result.elements();
}


FunctionLiteral* Parser::ParseFunctionLiteral(Handle<String> var_name,
                                              int function_token_position,
                                              FunctionLiteralType type,
                                              bool* ok) {
  // Function ::
  //   '(' FormalParameterList? ')' '{' FunctionBody '}'

  bool is_named = !var_name.is_null();

  // The name associated with this function. If it's a function expression,
  // this is the actual function name, otherwise this is the name of the
  // variable declared and initialized with the function (expression). In
  // that case, we don't have a function name (it's empty).
  Handle<String> name = is_named ? var_name : factory()->EmptySymbol();
  // The function name, if any.
  Handle<String> function_name = factory()->EmptySymbol();
  if (is_named && (type == EXPRESSION || type == NESTED)) {
    function_name = name;
  }

  int num_parameters = 0;
  // Parse function body.
  { Scope::Type type = Scope::FUNCTION_SCOPE;
    Scope* scope = factory()->NewScope(top_scope_, type, inside_with());
    LexicalScope lexical_scope(this, scope);
    TemporaryScope temp_scope(this);
    top_scope_->SetScopeName(name);

    //  FormalParameterList ::
    //    '(' (Identifier)*[','] ')'
    Expect(Token::LPAREN, CHECK_OK);
    int start_pos = scanner_.location().beg_pos;
    bool done = (peek() == Token::RPAREN);
    while (!done) {
      Handle<String> param_name = ParseIdentifier(CHECK_OK);
      if (!is_pre_parsing_) {
        top_scope_->AddParameter(top_scope_->Declare(param_name,
                                                     Variable::VAR));
        num_parameters++;
      }
      done = (peek() == Token::RPAREN);
      if (!done) Expect(Token::COMMA, CHECK_OK);
    }
    Expect(Token::RPAREN, CHECK_OK);

    Expect(Token::LBRACE, CHECK_OK);
    ZoneListWrapper<Statement> body = factory()->NewList<Statement>(8);

    // If we have a named function expression, we add a local variable
    // declaration to the body of the function with the name of the
    // function and let it refer to the function itself (closure).
    // NOTE: We create a proxy and resolve it here so that in the
    // future we can change the AST to only refer to VariableProxies
    // instead of Variables and Proxis as is the case now.
    if (!function_name.is_null() && function_name->length() > 0) {
      Variable* fvar = top_scope_->DeclareFunctionVar(function_name);
      VariableProxy* fproxy =
          top_scope_->NewUnresolved(function_name, inside_with());
      fproxy->BindTo(fvar);
      body.Add(new ExpressionStatement(
                   new Assignment(Token::INIT_VAR, fproxy,
                                  NEW(ThisFunction()), kNoPosition)));
    }

    // Determine if the function will be lazily compiled. The mode can
    // only be PARSE_LAZILY if the --lazy flag is true.
    bool is_lazily_compiled =
        mode() == PARSE_LAZILY && top_scope_->HasTrivialOuterContext();

    int materialized_literal_count;
    int expected_property_count;
    bool contains_array_literal;
    if (is_lazily_compiled && pre_data() != NULL) {
      FunctionEntry entry = pre_data()->GetFunctionEnd(start_pos);
      int end_pos = entry.end_pos();
      Counters::total_preparse_skipped.Increment(end_pos - start_pos);
      scanner_.SeekForward(end_pos);
      materialized_literal_count = entry.literal_count();
      expected_property_count = entry.property_count();
      contains_array_literal = entry.contains_array_literal();
    } else {
      ParseSourceElements(&body, Token::RBRACE, CHECK_OK);
      materialized_literal_count = temp_scope.materialized_literal_count();
      expected_property_count = temp_scope.expected_property_count();
      contains_array_literal = temp_scope.contains_array_literal();
    }

    Expect(Token::RBRACE, CHECK_OK);
    int end_pos = scanner_.location().end_pos;

    FunctionEntry entry = log()->LogFunction(start_pos);
    if (entry.is_valid()) {
      entry.set_end_pos(end_pos);
      entry.set_literal_count(materialized_literal_count);
      entry.set_property_count(expected_property_count);
      entry.set_contains_array_literal(contains_array_literal);
    }

    FunctionLiteral *function_literal =
        NEW(FunctionLiteral(name, top_scope_,
                            body.elements(), materialized_literal_count,
                            contains_array_literal, expected_property_count,
                            num_parameters, start_pos, end_pos,
                            function_name->length() > 0));
    if (!is_pre_parsing_) {
      function_literal->set_function_token_position(function_token_position);
    }
    return function_literal;
  }
}


Expression* Parser::ParseV8Intrinsic(bool* ok) {
  // CallRuntime ::
  //   '%' Identifier Arguments

  Expect(Token::MOD, CHECK_OK);
  Handle<String> name = ParseIdentifier(CHECK_OK);
  Runtime::Function* function =
      Runtime::FunctionForName(scanner_.literal_string());
  ZoneList<Expression*>* args = ParseArguments(CHECK_OK);
  if (function == NULL && extension_ != NULL) {
    // The extension structures are only accessible while parsing the
    // very first time not when reparsing because of lazy compilation.
    top_scope_->ForceEagerCompilation();
  }

  // Check for built-in macros.
  if (!is_pre_parsing_) {
    if (function == Runtime::FunctionForId(Runtime::kIS_VAR)) {
      // %IS_VAR(x)
      //   evaluates to x if x is a variable,
      //   leads to a parse error otherwise
      if (args->length() == 1 && args->at(0)->AsVariableProxy() != NULL) {
        return args->at(0);
      }
      *ok = false;
    // Check here for other macros.
    // } else if (function == Runtime::FunctionForId(Runtime::kIS_VAR)) {
    //   ...
    }

    if (!*ok) {
      // We found a macro but it failed.
      ReportMessage("unable_to_parse", Vector<const char*>::empty());
      return NULL;
    }
  }

  // Otherwise we have a runtime call.
  return NEW(CallRuntime(name, function, args));
}


void Parser::Consume(Token::Value token) {
  Token::Value next = Next();
  USE(next);
  USE(token);
  ASSERT(next == token);
}


void Parser::Expect(Token::Value token, bool* ok) {
  Token::Value next = Next();
  if (next == token) return;
  ReportUnexpectedToken(next);
  *ok = false;
}


void Parser::ExpectSemicolon(bool* ok) {
  // Check for automatic semicolon insertion according to
  // the rules given in ECMA-262, section 7.9, page 21.
  Token::Value tok = peek();
  if (tok == Token::SEMICOLON) {
    Next();
    return;
  }
  if (scanner_.has_line_terminator_before_next() ||
      tok == Token::RBRACE ||
      tok == Token::EOS) {
    return;
  }
  Expect(Token::SEMICOLON, ok);
}


Literal* Parser::GetLiteralUndefined() {
  return NEW(Literal(Factory::undefined_value()));
}


Literal* Parser::GetLiteralTheHole() {
  return NEW(Literal(Factory::the_hole_value()));
}


Literal* Parser::GetLiteralNumber(double value) {
  return NewNumberLiteral(value);
}


Handle<String> Parser::ParseIdentifier(bool* ok) {
  Expect(Token::IDENTIFIER, ok);
  if (!*ok) return Handle<String>();
  return factory()->LookupSymbol(scanner_.literal_string(),
                               scanner_.literal_length());
}

// This function reads an identifier and determines whether or not it
// is 'get' or 'set'.  The reason for not using ParseIdentifier and
// checking on the output is that this involves heap allocation which
// we can't do during preparsing.
Handle<String> Parser::ParseIdentifierOrGetOrSet(bool* is_get,
                                                 bool* is_set,
                                                 bool* ok) {
  Expect(Token::IDENTIFIER, ok);
  if (!*ok) return Handle<String>();
  if (scanner_.literal_length() == 3) {
    const char* token = scanner_.literal_string();
    *is_get = strcmp(token, "get") == 0;
    *is_set = !*is_get && strcmp(token, "set") == 0;
  }
  return factory()->LookupSymbol(scanner_.literal_string(),
                                 scanner_.literal_length());
}


// ----------------------------------------------------------------------------
// Parser support


bool Parser::TargetStackContainsLabel(Handle<String> label) {
  for (int i = target_stack_->length(); i-- > 0;) {
    BreakableStatement* stat = target_stack_->at(i)->AsBreakableStatement();
    if (stat != NULL && ContainsLabel(stat->labels(), label))
      return true;
  }
  return false;
}


BreakableStatement* Parser::LookupBreakTarget(Handle<String> label, bool* ok) {
  bool anonymous = label.is_null();
  for (int i = target_stack_->length(); i-- > 0;) {
    BreakableStatement* stat = target_stack_->at(i)->AsBreakableStatement();
    if (stat == NULL) continue;

    if ((anonymous && stat->is_target_for_anonymous()) ||
        (!anonymous && ContainsLabel(stat->labels(), label))) {
      RegisterLabelUse(stat->break_target(), i);
      return stat;
    }
  }
  return NULL;
}


IterationStatement* Parser::LookupContinueTarget(Handle<String> label,
                                                 bool* ok) {
  bool anonymous = label.is_null();
  for (int i = target_stack_->length(); i-- > 0;) {
    IterationStatement* stat = target_stack_->at(i)->AsIterationStatement();
    if (stat == NULL) continue;

    ASSERT(stat->is_target_for_anonymous());
    if (anonymous || ContainsLabel(stat->labels(), label)) {
      RegisterLabelUse(stat->continue_target(), i);
      return stat;
    }
  }
  return NULL;
}


void Parser::RegisterLabelUse(Label* label, int index) {
  // Register that a label found at the given index in the target
  // stack has been used from the top of the target stack. Add the
  // label to any LabelCollectors passed on the stack.
  for (int i = target_stack_->length(); i-- > index;) {
    LabelCollector* collector = target_stack_->at(i)->AsLabelCollector();
    if (collector != NULL) collector->AddLabel(label);
  }
}


Literal* Parser::NewNumberLiteral(double number) {
  return NEW(Literal(Factory::NewNumber(number, TENURED)));
}


Expression* Parser::NewThrowReferenceError(Handle<String> type) {
  return NewThrowError(Factory::MakeReferenceError_symbol(),
                       type, HandleVector<Object>(NULL, 0));
}


Expression* Parser::NewThrowSyntaxError(Handle<String> type,
                                        Handle<Object> first) {
  int argc = first.is_null() ? 0 : 1;
  Vector< Handle<Object> > arguments = HandleVector<Object>(&first, argc);
  return NewThrowError(Factory::MakeSyntaxError_symbol(), type, arguments);
}


Expression* Parser::NewThrowTypeError(Handle<String> type,
                                      Handle<Object> first,
                                      Handle<Object> second) {
  ASSERT(!first.is_null() && !second.is_null());
  Handle<Object> elements[] = { first, second };
  Vector< Handle<Object> > arguments =
      HandleVector<Object>(elements, ARRAY_SIZE(elements));
  return NewThrowError(Factory::MakeTypeError_symbol(), type, arguments);
}


Expression* Parser::NewThrowError(Handle<String> constructor,
                                  Handle<String> type,
                                  Vector< Handle<Object> > arguments) {
  if (is_pre_parsing_) return NULL;

  int argc = arguments.length();
  Handle<JSArray> array = Factory::NewJSArray(argc, TENURED);
  ASSERT(array->IsJSArray() && array->HasFastElements());
  for (int i = 0; i < argc; i++) {
    Handle<Object> element = arguments[i];
    if (!element.is_null()) {
      array->SetFastElement(i, *element);
    }
  }
  ZoneList<Expression*>* args = new ZoneList<Expression*>(2);
  args->Add(new Literal(type));
  args->Add(new Literal(array));
  return new Throw(new CallRuntime(constructor, NULL, args),
                   scanner().location().beg_pos);
}


// ----------------------------------------------------------------------------
// The Parser interface.

// MakeAST() is just a wrapper for the corresponding Parser calls
// so we don't have to expose the entire Parser class in the .h file.

static bool always_allow_natives_syntax = false;


ParserMessage::~ParserMessage() {
  for (int i = 0; i < args().length(); i++)
    DeleteArray(args()[i]);
  DeleteArray(args().start());
}


ScriptDataImpl::~ScriptDataImpl() {
  store_.Dispose();
}


int ScriptDataImpl::Length() {
  return store_.length();
}


unsigned* ScriptDataImpl::Data() {
  return store_.start();
}


ScriptDataImpl* PreParse(unibrow::CharacterStream* stream,
                         v8::Extension* extension) {
  Handle<Script> no_script;
  bool allow_natives_syntax =
      always_allow_natives_syntax ||
      FLAG_allow_natives_syntax ||
      Bootstrapper::IsActive();
  PreParser parser(no_script, allow_natives_syntax, extension);
  if (!parser.PreParseProgram(stream)) return NULL;
  // The list owns the backing store so we need to clone the vector.
  // That way, the result will be exactly the right size rather than
  // the expected 50% too large.
  Vector<unsigned> store = parser.recorder()->store()->ToVector().Clone();
  return new ScriptDataImpl(store);
}


FunctionLiteral* MakeAST(bool compile_in_global_context,
                         Handle<Script> script,
                         v8::Extension* extension,
                         ScriptDataImpl* pre_data) {
  bool allow_natives_syntax =
      always_allow_natives_syntax ||
      FLAG_allow_natives_syntax ||
      Bootstrapper::IsActive();
  AstBuildingParser parser(script, allow_natives_syntax, extension, pre_data);
  if (pre_data != NULL && pre_data->has_error()) {
    Scanner::Location loc = pre_data->MessageLocation();
    const char* message = pre_data->BuildMessage();
    Vector<const char*> args = pre_data->BuildArgs();
    parser.ReportMessageAt(loc, message, args);
    DeleteArray(message);
    for (int i = 0; i < args.length(); i++)
      DeleteArray(args[i]);
    DeleteArray(args.start());
    return NULL;
  }
  Handle<String> source = Handle<String>(String::cast(script->source()));
  SafeStringInputBuffer input(source.location());
  FunctionLiteral* result = parser.ParseProgram(source,
      &input, compile_in_global_context);
  return result;
}


FunctionLiteral* MakeLazyAST(Handle<Script> script,
                             Handle<String> name,
                             int start_position,
                             int end_position,
                             bool is_expression) {
  bool allow_natives_syntax_before = always_allow_natives_syntax;
  always_allow_natives_syntax = true;
  AstBuildingParser parser(script, true, NULL, NULL);  // always allow
  always_allow_natives_syntax = allow_natives_syntax_before;
  // Parse the function by pulling the function source from the script source.
  Handle<String> script_source(String::cast(script->source()));
  FunctionLiteral* result =
      parser.ParseLazy(SubString(script_source, start_position, end_position),
                       name,
                       start_position,
                       is_expression);
  return result;
}


#undef NEW


} }  // namespace v8::internal
