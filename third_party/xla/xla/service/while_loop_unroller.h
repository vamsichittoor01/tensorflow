/* Copyright 2023 The OpenXLA Authors.

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

#ifndef XLA_SERVICE_WHILE_LOOP_UNROLLER_H_
#define XLA_SERVICE_WHILE_LOOP_UNROLLER_H_

#include <cstdint>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "xla/hlo/ir/hlo_instruction.h"
#include "xla/hlo/ir/hlo_module.h"
#include "xla/service/hlo_pass_interface.h"
#include "xla/statusor.h"

namespace xla {

// Config for unrollable while loops.
struct WhileLoopConfig {
  // The initial value of the induction variable of the while loop.
  int64_t init;
  // The number of iterations the loop executes.
  int64_t trip_count;
  // The index of the induction variable in the input tuple of the while loop.
  int64_t induction_var_idx;
};

// Returns the list of unrollable loops in the given module
absl::flat_hash_map<HloInstruction*, WhileLoopConfig> GetUnrollableLoops(
    HloModule* module,
    const absl::flat_hash_set<absl::string_view>& execution_threads);

// Unrolls the given while loop with the given unrolling factor.
StatusOr<bool> Unroll(HloInstruction* while_op, int64_t unroll_factor);

// This pass unrolls while loops with the given unrolling factor. The value of
// unroll_factor = -1 will fully unroll the loop.
//
// TODO(b/288130138): Currently, we `only` support full unrolling.
//
// The trip count for loops is calculated based on
// `MatchTrivialLoopTripCount` function in
// tensorflow/compiler/xla/service/while_loop_analysis.h`
//
// TODO(b/301472793): Add utility functions to unroll specific loops.
class WhileLoopUnroller : public HloModulePass {
 public:
  ~WhileLoopUnroller() override = default;

  // Default unroll_factor of -1 indicates full unrolling
  explicit WhileLoopUnroller(int64_t unroll_factor = -1)
      : unroll_factor_(unroll_factor) {}

  absl::string_view name() const override { return "while_loop_unroller"; }

  using HloPassInterface::Run;
  StatusOr<bool> Run(
      HloModule* module,
      const absl::flat_hash_set<absl::string_view>& execution_threads) override;

 private:
  int64_t unroll_factor_;
};

}  // namespace xla

#endif  // XLA_SERVICE_WHILE_LOOP_UNROLLER_H_
