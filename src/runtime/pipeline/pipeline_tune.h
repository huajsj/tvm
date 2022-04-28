/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#ifndef TVM_RUNTIME_PIPELINE_PIPELINE_TUNE_H_
#define TVM_RUNTIME_PIPELINE_PIPELINE_TUNE_H_
#include <tvm/runtime/module.h>
#include <tvm/runtime/packed_func.h>
#include <tvm/runtime/registry.h>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "pipeline_struct.h"
namespace tvm {
namespace runtime {
/*!
 * \brief The class that executes the pipeline logic,it is used to initialize the thread pool,
    execute and schedule pipeline tasks, allocate and manage memory, etc.
 */
class PipelineTune {
 public:
  std::string PipelineDataMoveTune(
      std::string json, std::vector<std::shared_ptr<BackendRuntime>>  runtimes
  );
  void Load(dmlc::JSONReader* reader) {
    reader->BeginArray();
    while (reader->NextArrayItem()) {
      std::string key;
      reader->BeginObject();
      std::string op_name, shape, dtype;
      // Whether the output binding is global.
      std::vector<std::string> data;
      while (reader->NextObjectItem(&key)) {
        if (key == "op_name") {
          reader->Read(&op_name);
        } else if (key == "shape") {
          reader->Read(&shape);
          data.push_back(shape);
        } else if (key == "dtype") {
          reader->Read(&dtype);
          data.push_back(dtype);
          // There should be only one global binding.
        } else {
          LOG(FATAL) << "do not support key " << key;
        }
      }
      datas_.push_back(data);
    }
  }
 private:
  std::vector<std::vector<std::string>> datas_;
};
}  // namespace runtime
}  // namespace tvm
#endif  // TVM_RUNTIME_PIPELINE_PIPELINE_TUNE_H_
