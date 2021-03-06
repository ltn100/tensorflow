/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "tensorflow/core/common_runtime/eager/execute_node.h"

#include "tensorflow/core/lib/core/errors.h"

namespace tensorflow {
Status ExecuteNodeArgs::Init(
    EagerContext* ctx, const gtl::InlinedVector<TensorHandle*, 4>& op_inputs) {
  // If there are multiple references to a TensorHandle in 'op_inputs' we must
  // increment the reference count of the corresponding Tensor or risk it being
  // overwritten during kernel execution. The reference count is incremented
  // below when we insert a copy of the Tensor into protected_tensors, and will
  // be decremented once execution is complete.
  int first_index_that_needs_protecting = -1;
  for (int i = 0; i < op_inputs.size(); ++i) {
    TensorHandle* in = op_inputs[i];
    if (!in->IsRemote()) {
      TF_RETURN_IF_ERROR(in->TensorValue(&tensor_args_[i]));
      if (first_index_that_needs_protecting < 0 && !in->RefCountIsOne()) {
        first_index_that_needs_protecting = i;
      }
    } else {
      if (!has_remote_inputs_) {
        has_remote_inputs_ = true;
      }
    }
  }

  if (first_index_that_needs_protecting >= 0) {
    for (int i = first_index_that_needs_protecting; i < op_inputs.size(); ++i) {
      TensorHandle* in = op_inputs[i];
      if (!in->IsRemote() && !in->RefCountIsOne()) {
        const Tensor* input_tensor = nullptr;
        TF_RETURN_IF_ERROR(op_inputs[i]->Tensor(&input_tensor));
        protected_tensors_.emplace_back(TensorReference(*input_tensor));
      }
    }
  }

  if (has_remote_inputs_) {
#if defined(IS_MOBILE_PLATFORM)
    return errors::Unimplemented(
        "Eager's function execution with remote inputs is not available on "
        "mobile devices.");
#else   // !IS_MOBILE_PLATFORM
    serialize_remote_handle_ =
        [ctx, &op_inputs](const int i,
                          eager::RemoteTensorHandle* handle) -> Status {
      return ctx->RemoteMgr()->SerializeRemoteTensorHandle(
          op_inputs[i], handle, op_inputs[i]->device(),
          op_inputs[i]->device()->name());
    };
#endif  // !IS_MOBILE_PLATFORM
  }
  return Status::OK();
}

ExecuteNodeArgs::~ExecuteNodeArgs() {
  for (const auto& tensor_ref : protected_tensors_) {
    tensor_ref.Unref();
  }
}
}  // namespace tensorflow
