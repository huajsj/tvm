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

/*!
 * \file touch_extractor.cc
 * \brief Extract feature of touch pattern of axes in lowered IR
 */

#include "pipline_split.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <list>
#include <ostream>

namespace tvm {
namespace autotvm {

void AutoTune::LoadConfig(dmlc::JSONReader* reader) {
    int index = 0;
    reader->BeginArray();
    while (reader->NextArrayItem()) {
      reader->BeginObject();
      std::string key, op;
      //std::unordered_map<std::string, float> perf_map;
      int op_index, network_index;
      PERF perf_map;
      while (reader->NextObjectItem(&key)) {
        if (key == "op") {
          reader->Read(&op);
        } else if (key == "op_index") {
          reader->Read(&op_index);
        } else if (key == "network_index") {
          reader->Read(&network_index);
        } else if (key == "perf") {
          reader->BeginArray();
          while (reader->NextArrayItem()) {
            reader->BeginObject();
            std::string dev;
            float perf;
            while (reader->NextObjectItem(&key)) {
                if (key == "dev") {
                    reader->Read(&dev);
                } else if (key == "perf") {
                    reader->Read(&perf);
                }
            }
            perf_map[DPItem_::GetType(dev)] = perf;
            std::cout << "op:" << op << " op_index:" << op_index << " perf:" << dev << ":"<< perf;
            std::cout << std::endl;
          }
        } else {
          LOG(FATAL) << "do not support key " << key;
        }
      }
      layer_perf_[index] = perf_map; 
      layer_map_[index] = LayerInfo(op, op_index, network_index);
      if (index > 0) {
          auto prev = layer_map_[index - 1];
          assert(prev.op_index <= op_index && prev.network_index <= network_index);
      }
      index++;
    }
    return;
}

void AutoTune::GenerateGraph(int num) {
        /*
        std::cout << "          CPU       GPU      FPGA" << std::endl;
        for (int i = 0 ; i < num ; i++) {
            int pos = rand()%perf_assume_.size();
            PERF& perf = layer_perf_[i];
            std::ostringstream ostr;
            ostr << "conv2d_" << i;
            layers_.push_back(ostr.str());
            std::cout << ostr.str() << " ";
            for (int j = CPU; j <=FPGA; j++){
                float dperf = RandPerf(pos, static_cast<DevType>(j));
                perf[static_cast<DevType>(j)] = dperf;
                std::cout << std::setprecision(10) << std::to_string(dperf) << " ";
            }
            std::cout<<std::endl;
        }
        // create a list backend configuration list.
        backend_map = std::vector<std::vector<DPItem>>(dev_.size(), std::vector<DPItem>(num, DPItem()));*/
    }

void AutoTune::GenerateBalance(int current_pipeline_index, 
                         std::unordered_map<int, PERF> lperf,
                         int dev_num, 
                         int prev_pipeline_end_bound, 
                         std::vector<DevType> available_dev, 
                         int network_depth,
                         std::list<DPItem> &sub_list,
                         std::vector<std::pair<float, std::list<DPItem>>>& perf_list) {
        if (current_pipeline_index >= dev_num || prev_pipeline_end_bound >= network_depth - 1
            || available_dev.size() == 0)  {
            float perf = std::numeric_limits<float>::min();
            for (auto x:sub_list) {
                //std::cout << x ;
                perf = std::max(perf, GetPerSum(x, lperf));
            }
            //std::cout << "  average perf is " << perf << std::endl;
            perf_list.push_back(std::make_pair(perf, sub_list));
            return;
        }
         
        for (size_t k = 0; k < available_dev.size(); k++) {
            auto dev_copy = available_dev;
            //remove current dev;
            dev_copy.erase(dev_copy.begin() + k);
            if (dev_copy.size() > 0) {
              for (int i = prev_pipeline_end_bound + 1; i < network_depth; i++) {
                  DPItem di(prev_pipeline_end_bound + 1, i, available_dev[k]);
                  sub_list.push_back(di);
                  GenerateBalance(current_pipeline_index + 1,lperf, dev_num, i,  
                                  dev_copy, network_depth, sub_list, perf_list);
                  sub_list.pop_back();
              }
            } else {
                DPItem di(prev_pipeline_end_bound + 1, network_depth - 1, available_dev[k]);
                sub_list.push_back(di);
                GenerateBalance(current_pipeline_index + 1, lperf, dev_num, network_depth - 1, 
                                dev_copy, network_depth, sub_list, perf_list);
                sub_list.pop_back();
            }
       }
    }

void AutoTune::ShowBest() {
      std::sort(perf_list.begin(), perf_list.end(), comp);
      size_t max_list = 10;
      std::cout << "The top " << max_list << "  best split configure are: " << std::endl;
      for (int i = 0; i < std::min(perf_list.size(), max_list); i++) {
          auto item = perf_list[i];
          for (auto x:item.second) {
             std::cout << x << " ";
          }
          std::cout << " perf is " << item.first << std::endl;
      }
    }
float AutoTune::GetPerSum(DPItem di, std::unordered_map<int, PERF> lperf) {
        DevType  dtype = di.dev_type;
        float ret =0 ;
        for (int i = di.start; i <= di.end; i++) {
            ret += lperf[i][dtype];
        }
        return ret;
    }
/*
TEST(AutoSplitting, TVMAutoSplitting) {
    std::cout << "Perf Data !\n";
    int net_depth = 15;
    std::list<DPItem> list;
    AutoTune at;
    at.GenerateGraph(net_depth);
    AutoTune::GenerateBalance(0, at.layer_perf_,at.dev_.size(), -1,
                              {CPU, GPU, FPGA}, net_depth, list, at.perf_list);
    at.ShowBest();
}
*/
Array<String> GetSplitConfig(const std::string& json) {
  std::istringstream is(json);
  dmlc::JSONReader reader(&is);
  std::list<DPItem> list;
  AutoTune at(reader);
  int net_depth = at.GetNetDepth();
  std::vector<DevType> available_dev {CPU, VTA};
  AutoTune::GenerateBalance(0, at.layer_perf_,available_dev.size(), -1,
                              {CPU, VTA}, net_depth, list, at.perf_list);
  at.ShowBest();
  return {"1","2", "3"};
}
TVM_REGISTER_GLOBAL("autotvm.feature.GetSplitConfig")
    .set_body([](TVMArgs args, TVMRetValue* ret) {
        *ret = GetSplitConfig(args[0]);
    });
}  // namespace autotvm
}  // namespace tvm
