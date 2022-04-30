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
#include "pipeline_tune.h"

#include <unordered_map>
#include <utility>
#include <vector>
namespace tvm {
namespace runtime {
std::string PipelineTune::PipelineDataMoveTune(
    std::string json, std::vector<std::shared_ptr<BackendRuntime>>  runtimes
) {
  std::istringstream is(json);
  dmlc::JSONReader reader(&is);
  this->Load(&reader);
  int len = runtimes.size();
  for (int i  = 0; i < len - 1; i++) {
    swap(runtimes[0], runtimes[i]);
    for (int j = 1; j < len; j++) {
      auto source_dtype = runtimes[0]->GetDeviceType();
      auto dst_dtype = runtimes[j]->GetDeviceType();
      int dst = (j == i) ? 0 : j;
      std::cout << "from " << " i  to " << j <<std::endl;
      for (auto data:datas_) {
        //Get data copy performance.
        auto shape = GetShapeFromString(data[0]);
        auto type = data[1];

        auto&& data_source = runtimes[0]->CreateTuneData(shape, type);
        auto perf = runtimes[j]->CopyPerfMeasure(data_source);
      }
    }
    swap(runtimes[i], runtimes[0]);
  }

  return "tune";
}
}  // namespace runtime
}  // namespace tvm
