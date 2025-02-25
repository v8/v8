// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_DEOPT_FRAME_VISITOR_H_
#define V8_MAGLEV_MAGLEV_DEOPT_FRAME_VISITOR_H_

#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

// TODO(victorgomes): Do we need this namespace detail?
namespace detail {

enum class DeoptFrameVisitMode {
  kDefault,
  kRemoveIdentities,
  kOther,
};

template <DeoptFrameVisitMode mode, typename T>
using const_if_default =
    std::conditional_t<mode == DeoptFrameVisitMode::kDefault, const T, T>;

template <DeoptFrameVisitMode mode>
using ValueNodeT = std::conditional_t<mode == DeoptFrameVisitMode::kDefault,
                                      ValueNode*, ValueNode*&>;

template <DeoptFrameVisitMode mode, typename Function>
void DeepForEachInputSingleFrameImpl(
    const_if_default<mode, DeoptFrame>& frame, InputLocation*& input_location,
    Function&& f,
    std::function<bool(interpreter::Register)> is_result_register) {
  switch (frame.type()) {
    case DeoptFrame::FrameType::kInterpretedFrame:
      f(frame.as_interpreted().closure(), input_location);
      frame.as_interpreted().frame_state()->ForEachValue(
          frame.as_interpreted().unit(),
          [&](ValueNodeT<mode> node, interpreter::Register reg) {
            // Skip over the result location for lazy deopts, since it is
            // irrelevant for lazy deopts (unoptimized code will recreate the
            // result).
            if (is_result_register(reg)) return;
            f(node, input_location);
          });
      break;
    case DeoptFrame::FrameType::kInlinedArgumentsFrame: {
      // The inlined arguments frame can never be the top frame.
      f(frame.as_inlined_arguments().closure(), input_location);
      for (ValueNodeT<mode> node : frame.as_inlined_arguments().arguments()) {
        f(node, input_location);
      }
      break;
    }
    case DeoptFrame::FrameType::kConstructInvokeStubFrame: {
      f(frame.as_construct_stub().receiver(), input_location);
      f(frame.as_construct_stub().context(), input_location);
      break;
    }
    case DeoptFrame::FrameType::kBuiltinContinuationFrame:
      for (ValueNodeT<mode> node :
           frame.as_builtin_continuation().parameters()) {
        f(node, input_location);
      }
      f(frame.as_builtin_continuation().context(), input_location);
      break;
  }
}

template <DeoptFrameVisitMode mode, typename Function>
void DeepForVirtualObject(VirtualObject* vobject,
                          InputLocation*& input_location,
                          const VirtualObject::List& virtual_objects,
                          Function&& f) {
  vobject->ForEachDeoptInputLocation([&](ValueNode* value, ValueNode*& input) {
    if (IsConstantNode(value->opcode())) {
      // No location assigned to constants.
      return;
    }
    if constexpr (mode == DeoptFrameVisitMode::kRemoveIdentities) {
      if (value->Is<Identity>()) {
        value = value->input(0).node();
        input = value;
      }
    }
    // Special nodes.
    switch (value->opcode()) {
      case Opcode::kArgumentsElements:
      case Opcode::kArgumentsLength:
      case Opcode::kRestLength:
        // No location assigned to these opcodes.
        break;
      case Opcode::kVirtualObject:
        UNREACHABLE();
      case Opcode::kInlinedAllocation: {
        InlinedAllocation* alloc = value->Cast<InlinedAllocation>();
        VirtualObject* inner_vobject = virtual_objects.FindAllocatedWith(alloc);
        CHECK_NOT_NULL(inner_vobject);
        // Check if it has escaped.
        if (alloc->HasBeenAnalysed() && alloc->HasBeenElided()) {
          input_location++;  // Reserved for the inlined allocation.
          DeepForVirtualObject<mode>(inner_vobject, input_location,
                                     virtual_objects, f);
        } else {
          f(value, input_location);
          input_location +=
              inner_vobject->InputLocationSizeNeeded(virtual_objects) + 1;
        }
        break;
      }
      default:
        f(value, input_location);
        input_location++;
        break;
    }
  });
}

template <DeoptFrameVisitMode mode, typename Function>
void DeepForEachInputAndVirtualObject(
    const_if_default<mode, DeoptFrame>& frame, InputLocation*& input_location,
    const VirtualObject::List& virtual_objects, Function&& f,
    std::function<bool(interpreter::Register)> is_result_register =
        [](interpreter::Register) { return false; }) {
  auto update_node = [&f, &virtual_objects](ValueNodeT<mode> node,
                                            InputLocation*& input_location) {
    DCHECK(!node->template Is<VirtualObject>());
    if constexpr (mode == DeoptFrameVisitMode::kRemoveIdentities) {
      if (node->template Is<Identity>()) {
        node = node->input(0).node();
      }
    }
    if (auto alloc = node->template TryCast<InlinedAllocation>()) {
      VirtualObject* vobject = virtual_objects.FindAllocatedWith(alloc);
      if (vobject) {
        if (alloc->HasBeenAnalysed() && alloc->HasBeenElided()) {
          input_location++;  // Reserved for the inlined allocation.
          return DeepForVirtualObject<mode>(vobject, input_location,
                                            virtual_objects, f);
        } else {
          f(node, input_location);
          input_location +=
              vobject->InputLocationSizeNeeded(virtual_objects) + 1;
        }
        return;
      }
      // If the allocation isn't in the virtual object list, it's the
      // return value from an (non-eager) inlined call. The value is escaping,
      // as we don't have enough information for object materialization during
      // deoptimization.
      // TODO(victorgomes): Support eliding VOs returned by a non-eager
      // inlined call.
      DCHECK_NULL(vobject);
      DCHECK(v8_flags.maglev_non_eager_inlining);
      DCHECK((alloc->HasBeenAnalysed() && alloc->HasEscaped()) ||
             alloc->IsEscaping());
      DCHECK(alloc->is_returned_value_from_inline_call());
    }
    f(node, input_location);
    input_location++;
  };
  DeepForEachInputSingleFrameImpl<mode>(frame, input_location, update_node,
                                        is_result_register);
}

template <DeoptFrameVisitMode mode, typename Function>
void DeepForEachInputImpl(const_if_default<mode, DeoptFrame>& frame,
                          InputLocation*& input_location,
                          const VirtualObject::List& virtual_objects,
                          Function&& f) {
  if (frame.parent()) {
    DeepForEachInputImpl<mode>(*frame.parent(), input_location, virtual_objects,
                               f);
  }
  DeepForEachInputAndVirtualObject<mode>(frame, input_location, virtual_objects,
                                         f);
}

template <DeoptFrameVisitMode mode, typename Function>
void DeepForEachInputForEager(
    const_if_default<mode, EagerDeoptInfo>* deopt_info, Function&& f) {
  InputLocation* input_location = deopt_info->input_locations();
  const VirtualObject::List& virtual_objects =
      GetVirtualObjects(deopt_info->top_frame());
  DeepForEachInputImpl<mode>(deopt_info->top_frame(), input_location,
                             virtual_objects, std::forward<Function>(f));
}

template <DeoptFrameVisitMode mode, typename Function>
void DeepForEachInputForLazy(const_if_default<mode, LazyDeoptInfo>* deopt_info,
                             Function&& f) {
  InputLocation* input_location = deopt_info->input_locations();
  auto& top_frame = deopt_info->top_frame();
  const VirtualObject::List& virtual_objects = GetVirtualObjects(top_frame);
  if (top_frame.parent()) {
    DeepForEachInputImpl<mode>(*top_frame.parent(), input_location,
                               virtual_objects, f);
  }
  DeepForEachInputAndVirtualObject<mode>(
      top_frame, input_location, virtual_objects, f,
      [deopt_info](interpreter::Register reg) {
        return deopt_info->IsResultRegister(reg);
      });
}

template <typename Function>
void DeepForEachInput(const EagerDeoptInfo* deopt_info, Function&& f) {
  return DeepForEachInputForEager<DeoptFrameVisitMode::kDefault>(deopt_info, f);
}

template <typename Function>
void DeepForEachInput(const LazyDeoptInfo* deopt_info, Function&& f) {
  return DeepForEachInputForLazy<DeoptFrameVisitMode::kDefault>(deopt_info, f);
}

template <typename Function>
void DeepForEachInputRemovingIdentities(EagerDeoptInfo* deopt_info,
                                        Function&& f) {
  return DeepForEachInputForEager<DeoptFrameVisitMode::kRemoveIdentities>(
      deopt_info, f);
}

template <typename Function>
void DeepForEachInputRemovingIdentities(LazyDeoptInfo* deopt_info,
                                        Function&& f) {
  return DeepForEachInputForLazy<DeoptFrameVisitMode::kRemoveIdentities>(
      deopt_info, f);
}

}  // namespace detail

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_DEOPT_FRAME_VISITOR_H_
