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
#include <optional>
#include <type_traits>
#include <cuml/experimental/fil/detail/index_type.hpp>
#include <cuml/experimental/fil/detail/infer/cpu.hpp>
#ifdef CUML_ENABLE_GPU
#include <cuml/experimental/fil/detail/infer/gpu.hpp>
#endif
#include <cuml/experimental/fil/detail/postprocessor.hpp>
#include <cuml/experimental/fil/exceptions.hpp>
#include <cuml/experimental/fil/detail/raft_proto/cuda_stream.hpp>
#include <cuml/experimental/fil/detail/raft_proto/device_id.hpp>
#include <cuml/experimental/fil/detail/raft_proto/device_type.hpp>
namespace ML {
namespace experimental {
namespace fil {
namespace detail {

/*
 * Perform inference based on the given forest and input parameters
 *
 * @tparam D The device type (CPU/GPU) used to perform inference
 * @tparam forest_t The type of the forest
 * @param forest The forest to be evaluated
 * @param postproc The postprocessor object used to execute
 * postprocessing
 * @param output Pointer to where the output should be written
 * @param input Pointer to where the input data can be read from
 * @param row_count The number of rows in the input data
 * @param col_count The number of columns in the input data
 * @param output_count The number of outputs per row
 * @param has_categorical_nodes Whether or not any node within the forest has
 * a categorical split
 * @param vector_output Pointer to the beginning of storage for vector
 * outputs of leaves (nullptr for no vector output)
 * @param categorical_data Pointer to external categorical data storage if
 * required
 * @param specified_chunk_size If non-nullopt, the size of "mini-batches"
 * used for distributing work across threads
 * @param device The device on which to execute evaluation
 * @param stream Optionally, the CUDA stream to use
 */
template<raft_proto::device_type D, typename forest_t>
void infer(
  forest_t const& forest,
  postprocessor<typename forest_t::io_type> const& postproc,
  typename forest_t::io_type* output,
  typename forest_t::io_type* input,
  index_type row_count,
  index_type col_count,
  index_type output_count,
  bool has_categorical_nodes,
  typename forest_t::io_type* vector_output=nullptr,
  typename forest_t::node_type::index_type* categorical_data=nullptr,
  std::optional<index_type> specified_chunk_size=std::nullopt,
  raft_proto::device_id<D> device=raft_proto::device_id<D>{},
  raft_proto::cuda_stream stream=raft_proto::cuda_stream{}
) {
  if (vector_output == nullptr) {
    if (categorical_data == nullptr) {
      if (!has_categorical_nodes) {
        inference::infer<D, false, forest_t, std::nullptr_t, std::nullptr_t> (
          forest,
          postproc,
          output,
          input,
          row_count,
          col_count,
          output_count,
          nullptr,
          nullptr,
          specified_chunk_size,
          device,
          stream
        );
      } else {
        inference::infer<D, true, forest_t, std::nullptr_t, std::nullptr_t> (
          forest,
          postproc,
          output,
          input,
          row_count,
          col_count,
          output_count,
          nullptr,
          nullptr,
          specified_chunk_size,
          device,
          stream
        );
      }
    } else {
      inference::infer<D, true, forest_t> (
        forest,
        postproc,
        output,
        input,
        row_count,
        col_count,
        output_count,
        nullptr,
        categorical_data,
        specified_chunk_size,
        device,
        stream
      );
    }
  } else {
    if (categorical_data == nullptr) {
      if (!has_categorical_nodes) {
        inference::infer<D, false, forest_t> (
          forest,
          postproc,
          output,
          input,
          row_count,
          col_count,
          output_count,
          vector_output,
          nullptr,
          specified_chunk_size,
          device,
          stream
        );
      } else {
        inference::infer<D, true, forest_t> (
          forest,
          postproc,
          output,
          input,
          row_count,
          col_count,
          output_count,
          vector_output,
          nullptr,
          specified_chunk_size,
          device,
          stream
        );
      }
    } else {
      inference::infer<D, true, forest_t> (
        forest,
        postproc,
        output,
        input,
        row_count,
        col_count,
        output_count,
        vector_output,
        categorical_data,
        specified_chunk_size,
        device,
        stream
      );
    }
  }
}

}
}
}
}
