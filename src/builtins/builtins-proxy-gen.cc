// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"

#include "src/counters.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
using compiler::Node;
using compiler::CodeAssembler;

// ES6 section 26.2.1.1 Proxy ( target, handler ) for the [[Call]] case.
TF_BUILTIN(ProxyConstructor, CodeStubAssembler) {
  Node* context = Parameter(Descriptor::kContext);
  ThrowTypeError(context, MessageTemplate::kConstructorNotFunction, "Proxy");
}

class ProxiesCodeStubAssembler : public CodeStubAssembler {
 public:
  explicit ProxiesCodeStubAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  Node* IsProxyRevoked(Node* proxy) {
    CSA_ASSERT(this, IsJSProxy(proxy));

    Node* handler = LoadObjectField(proxy, JSProxy::kHandlerOffset);
    CSA_ASSERT(this, Word32Or(IsJSReceiver(handler), IsNull(handler)));

    return IsNull(handler);
  }

  void GotoIfProxyRevoked(Node* object, Label* if_proxy_revoked) {
    Label continue_checks(this);
    GotoIfNot(IsJSProxy(object), &continue_checks);
    GotoIf(IsProxyRevoked(object), if_proxy_revoked);
    Goto(&continue_checks);
    BIND(&continue_checks);
  }

  Node* AllocateProxy(Node* target, Node* handler, Node* context) {
    VARIABLE(map, MachineRepresentation::kTagged);

    Label callable_target(this), constructor_target(this), none_target(this),
        create_proxy(this);

    Node* nativeContext = LoadNativeContext(context);

    GotoIf(IsCallable(target), &callable_target);
    Goto(&none_target);

    BIND(&callable_target);
    {
      // Every object that is a constructor is implicitly callable
      // so it's okay to nest this check here
      GotoIf(IsConstructor(target), &constructor_target);
      map.Bind(
          LoadContextElement(nativeContext, Context::PROXY_CALLABLE_MAP_INDEX));
      Goto(&create_proxy);
    }
    BIND(&constructor_target);
    {
      map.Bind(LoadContextElement(nativeContext,
                                  Context::PROXY_CONSTRUCTOR_MAP_INDEX));
      Goto(&create_proxy);
    }
    BIND(&none_target);
    {
      map.Bind(LoadContextElement(nativeContext, Context::PROXY_MAP_INDEX));
      Goto(&create_proxy);
    }

    BIND(&create_proxy);
    Node* proxy = Allocate(JSProxy::kSize);
    StoreMapNoWriteBarrier(proxy, map.value());
    StoreObjectFieldRoot(proxy, JSProxy::kPropertiesOffset,
                         Heap::kEmptyPropertiesDictionaryRootIndex);
    StoreObjectFieldNoWriteBarrier(proxy, JSProxy::kTargetOffset, target);
    StoreObjectFieldNoWriteBarrier(proxy, JSProxy::kHandlerOffset, handler);
    StoreObjectFieldNoWriteBarrier(proxy, JSProxy::kHashOffset,
                                   UndefinedConstant());

    return proxy;
  }
};

// ES6 section 26.2.1.1 Proxy ( target, handler ) for the [[Construct]] case.
TF_BUILTIN(ProxyConstructor_ConstructStub, ProxiesCodeStubAssembler) {
  int const kTargetArg = 0;
  int const kHandlerArg = 1;

  Node* argc =
      ChangeInt32ToIntPtr(Parameter(BuiltinDescriptor::kArgumentsCount));
  CodeStubArguments args(this, argc);

  Node* target = args.GetOptionalArgumentValue(kTargetArg);
  Node* handler = args.GetOptionalArgumentValue(kHandlerArg);
  Node* context = Parameter(BuiltinDescriptor::kContext);

  Label throw_proxy_non_object(this, Label::kDeferred),
      throw_proxy_handler_or_target_revoked(this, Label::kDeferred),
      return_create_proxy(this);

  GotoIf(TaggedIsSmi(target), &throw_proxy_non_object);
  GotoIfNot(IsJSReceiver(target), &throw_proxy_non_object);
  GotoIfProxyRevoked(target, &throw_proxy_handler_or_target_revoked);

  GotoIf(TaggedIsSmi(handler), &throw_proxy_non_object);
  GotoIfNot(IsJSReceiver(handler), &throw_proxy_non_object);
  GotoIfProxyRevoked(handler, &throw_proxy_handler_or_target_revoked);

  args.PopAndReturn(AllocateProxy(target, handler, context));

  BIND(&throw_proxy_non_object);
  ThrowTypeError(context, MessageTemplate::kProxyNonObject);

  BIND(&throw_proxy_handler_or_target_revoked);
  ThrowTypeError(context, MessageTemplate::kProxyHandlerOrTargetRevoked);
}

}  // namespace internal
}  // namespace v8
