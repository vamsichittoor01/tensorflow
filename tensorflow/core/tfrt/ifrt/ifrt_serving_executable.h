/* Copyright 2023 The TensorFlow Authors. All Rights Reserved.

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

#ifndef TENSORFLOW_CORE_TFRT_IFRT_IFRT_SERVING_EXECUTABLE_H_
#define TENSORFLOW_CORE_TFRT_IFRT_IFRT_SERVING_EXECUTABLE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "mlir/IR/BuiltinOps.h"  // from @llvm-project
#include "mlir/IR/MLIRContext.h"  // from @llvm-project
#include "mlir/IR/OwningOpRef.h"  // from @llvm-project
#include "tensorflow/compiler/mlir/tfrt/transforms/ifrt/tf2hlo.h"
#include "tensorflow/compiler/tf2xla/xla_helpers.h"
#include "xla/python/ifrt/array.h"
#include "xla/python/ifrt/client.h"
#include "xla/python/ifrt/executable.h"
#include "xla/python/ifrt/future.h"
#include "xla/python/ifrt/shape.h"
#include "xla/python/ifrt/sharding.h"
#include "xla/xla_data.pb.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "tensorflow/core/protobuf/tpu/compile_metadata.pb.h"
#include "tensorflow/core/tfrt/ifrt/ifrt_loaded_variable_registry.h"
#include "tsl/concurrency/ref_count.h"

namespace tensorflow {
namespace ifrt_serving {

class IfrtServingExecutable {
 public:
  IfrtServingExecutable(
      absl::string_view model_name, absl::string_view signature_name,
      mlir::OwningOpRef<mlir::ModuleOp> module,
      std::shared_ptr<xla::ifrt::Client> client,
      const Eigen::ThreadPoolDevice* thread_pool_device,
      const IfrtLoadedVariableRegistry* ifrt_loaded_variable_registry,
      tensorflow::XlaHelpers::ShapeRepresentationFn shape_representation_fn)
      : model_name_(std::string(model_name)),
        signature_name_(std::string(signature_name)),
        module_(std::move(module)),
        ifrt_client_(std::move(client)),
        thread_pool_device_(*thread_pool_device),
        ifrt_loaded_variable_registry_(*ifrt_loaded_variable_registry),
        shape_representation_fn_(std::move(shape_representation_fn)) {}

  // Movable but not copyable.
  IfrtServingExecutable(IfrtServingExecutable&& other) = default;
  IfrtServingExecutable& operator=(IfrtServingExecutable&& other) = default;
  IfrtServingExecutable(const IfrtServingExecutable& other) = delete;
  IfrtServingExecutable& operator=(const IfrtServingExecutable& other) = delete;

  absl::string_view model_name() const { return model_name_; }
  absl::string_view signature_name() const { return signature_name_; }

  // Executes the computation.
  // variable_arg_indices are in sorted order.
  absl::StatusOr<std::vector<tensorflow::Tensor>> Execute(
      absl::Span<const tensorflow::Tensor> inputs,
      absl::Span<const std::string> variable_names,
      absl::Span<const int> variable_arg_indices);

  int num_executables() const {
    absl::MutexLock lock(&mutex_);
    return executable_bundles_.size();
  }

 private:
  // In memory cache key.
  struct Key {
    std::vector<tensorflow::TensorShape> input_shapes;
    template <typename H>
    friend H AbslHashValue(H h, const Key& key) {
      for (const auto& shape : key.input_shapes) {
        for (auto size : shape.dim_sizes()) {
          h = H::combine(std::move(h), size);
        }
      }
      return h;
    }

    friend bool operator==(const Key& x, const Key& y) {
      return x.input_shapes == y.input_shapes;
    }
  };

  struct CachedExecutableBundle {
    std::shared_ptr<xla::ifrt::LoadedExecutable> ifrt_executable;
    tensorflow::tpu::TPUCompileMetadataProto compile_metadata;
  };

  std::string model_name_;
  std::string signature_name_;

  std::unique_ptr<mlir::MLIRContext> context_;
  mlir::OwningOpRef<mlir::ModuleOp> module_;

  std::shared_ptr<xla::ifrt::Client> ifrt_client_;
  const Eigen::ThreadPoolDevice& thread_pool_device_;

  const IfrtLoadedVariableRegistry& ifrt_loaded_variable_registry_;
  tensorflow::XlaHelpers::ShapeRepresentationFn shape_representation_fn_;

  mutable absl::Mutex mutex_;
  absl::flat_hash_map<Key,
                      xla::ifrt::Future<absl::StatusOr<CachedExecutableBundle>>>
      executable_bundles_ ABSL_GUARDED_BY(mutex_);

  absl::StatusOr<tsl::RCReference<xla::ifrt::Array>> ConvertTensorToArray(
      const tensorflow::Tensor& tensor,
      const xla::ifrt::DeviceList& device_list,
      const xla::OpSharding& sharding);

  xla::ifrt::Future<absl::StatusOr<CachedExecutableBundle>>
  LookUpOrCreateExecutable(absl::Span<const DtypeAndShape> dtypes_and_shapes);
  absl::StatusOr<IfrtServingExecutable::CachedExecutableBundle>
  CreateExecutableSynchronously(
      absl::Span<const DtypeAndShape> dtypes_and_shapes);

  absl::StatusOr<std::unique_ptr<xla::ifrt::Sharding>> CreateSharding(
      int num_devices, const xla::ifrt::Shape& arg_xla_shape,
      const xla::ifrt::Shape& sharded_shapes);

  std::vector<xla::ifrt::Shape> GetArgShape(
      int arg_index, const CachedExecutableBundle& entry);
};

}  // namespace ifrt_serving
}  // namespace tensorflow

#endif  // TENSORFLOW_CORE_TFRT_IFRT_IFRT_SERVING_EXECUTABLE_H_
