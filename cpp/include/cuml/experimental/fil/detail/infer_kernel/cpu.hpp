/*
 * Copyright (c) 2023, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once
#include <cstddef>
#include <iostream>
#include <new>
#include <numeric>
#include <vector>
#include <cuml/experimental/fil/detail/cpu_introspection.hpp>
#include <cuml/experimental/fil/detail/evaluate_tree.hpp>
#include <cuml/experimental/fil/detail/index_type.hpp>
#include <cuml/experimental/fil/detail/postprocessor.hpp>
#include <cuml/experimental/fil/detail/raft_proto/ceildiv.hpp>

namespace ML {
namespace experimental {
namespace fil {
namespace detail {

/**
 * The CPU "kernel" used to actually perform forest inference
 *
 * @tparam has_categorical_nodes Whether or not this kernel should be
 * compiled to operate on trees with categorical nodes.
 * @tparam forest_t The type of the forest object which will be used for
 * inference.
 * @tparam vector_output_t If non-nullptr_t, this indicates the type we expect
 * for outputs from vector leaves.
 * @tparam categorical_data_t If non-nullptr_t, this indicates the type we
 * expect for non-local categorical data storage.
 * @param forest The forest used to perform inference
 * @param postproc The postprocessor object used to store all necessary
 * data for postprocessing
 * @param output Pointer to the host-accessible buffer where output
 * should be written
 * @param input Pointer to the host-accessible buffer where input should be
 * read from
 * @param row_count The number of rows in the input
 * @param col_count The number of columns per row in the input
 * @param num_outputs The expected number of output elements per row
 * @param chunk_size The number of rows for each thread to process with its
 * assigned trees before fetching a new set of trees/rows.
 * @param grove_size The number of trees to assign to a thread for each chunk
 * of rows it processes.
 * @param vector_output_p If non-nullptr, a pointer to the stored leaf
 * vector outputs for all leaf nodes
 * @param categorical_data If non-nullptr, a pointer to where non-local
 * data on categorical splits are stored.
 */
template<
  bool has_categorical_nodes,
  typename forest_t,
  typename vector_output_t=std::nullptr_t,
  typename categorical_data_t=std::nullptr_t
>
void infer_kernel_cpu(
    forest_t const& forest,
    postprocessor<typename forest_t::io_type> const& postproc,
    typename forest_t::io_type* output,
    typename forest_t::io_type const* input,
    index_type row_count,
    index_type col_count,
    index_type num_outputs,
    index_type chunk_size=hardware_constructive_interference_size,
    index_type grove_size=hardware_constructive_interference_size,
    vector_output_t vector_output_p=nullptr,
    categorical_data_t categorical_data=nullptr
) {
  auto constexpr has_vector_leaves = !std::is_same_v<vector_output_t, std::nullptr_t>;
  auto constexpr has_nonlocal_categories = !std::is_same_v<categorical_data_t, std::nullptr_t>;
  
  using node_t = typename forest_t::node_type;

  using output_t = std::conditional_t<
    has_vector_leaves,
    std::remove_pointer_t<vector_output_t>,
    typename node_t::threshold_type
  >;

  auto const num_tree = forest.tree_count();
  auto const num_grove = raft_proto::ceildiv(num_tree, grove_size);
  auto const num_chunk = raft_proto::ceildiv(row_count, chunk_size);

  auto output_workspace = std::vector<output_t>(
    row_count * num_outputs * num_grove,
    output_t{}
  );
  auto const task_count = num_grove * num_chunk;

  // Infer on each grove and chunk
#pragma omp parallel for
  for(auto task_index = index_type{}; task_index < task_count; ++task_index) {
    auto const grove_index = task_index / num_chunk;
    auto const chunk_index = task_index % num_chunk;
    auto const start_row = chunk_index * chunk_size;
    auto const end_row = std::min(start_row + chunk_size, row_count);
    auto const start_tree = grove_index * grove_size;
    auto const end_tree = std::min(start_tree + grove_size, num_tree);

    for (auto row_index = start_row; row_index < end_row; ++row_index){
      for (auto tree_index = start_tree; tree_index < end_tree; ++tree_index) {
        auto tree_output = std::conditional_t<
          has_vector_leaves, typename node_t::index_type, typename node_t::threshold_type
        >{};
        if constexpr (has_nonlocal_categories) {
          tree_output = evaluate_tree<has_vector_leaves>(
            forest.get_tree_root(tree_index),
            input + row_index * col_count,
            categorical_data
          );
        } else {
          tree_output = evaluate_tree<has_vector_leaves, has_categorical_nodes>(
            forest.get_tree_root(tree_index),
            input + row_index * col_count
          );
        }
        if constexpr (has_vector_leaves) {
          for (
            auto class_index=index_type{};
            class_index < num_outputs;
            ++class_index
          ) {
            output_workspace[
              row_index * num_outputs * num_grove
              + class_index * num_grove
              + grove_index
            ] += vector_output_p[
              tree_output * num_outputs + class_index
            ];
          }
        } else {
          output_workspace[
            row_index * num_outputs * num_grove
            + (tree_index % num_outputs) * num_grove
            + grove_index
          ] += tree_output;
        }
      }  // Trees
    }  // Rows
  }  // Tasks

  // Sum over grove and postprocess
#pragma omp parallel for
  for (auto row_index=index_type{}; row_index < row_count; ++row_index) {
    for (
      auto class_index = index_type{};
      class_index < num_outputs;
      ++class_index
    ) {
      auto grove_offset = (
        row_index * num_outputs * num_grove + class_index * num_grove
      );

      output_workspace[grove_offset] = std::accumulate(
        std::begin(output_workspace) + grove_offset,
        std::begin(output_workspace) + grove_offset + num_grove,
        output_t{}
      );
    }
    postproc(
      output_workspace.data() + row_index * num_outputs * num_grove,
      num_outputs,
      output + row_index * num_outputs,
      num_grove
    );
  }
}

}
}
}
}
