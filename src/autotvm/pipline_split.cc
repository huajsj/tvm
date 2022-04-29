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
#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <list>
#include <ostream>

namespace tvm {
namespace autotvm {

void AutoTune::LoadDataMoveConfig(dmlc::JSONReader* reader) {
    int index = 0;
    reader->BeginArray();
    while (reader->NextArrayItem()) {
      reader->BeginObject();
      std::string key, type;
      std::vector<int> shape;
      //std::unordered_map<std::string, float> perf_map;
      std::unordered_map<std::string, float> perf_map;
      while (reader->NextObjectItem(&key)) {
        if (key == "shape") {
          reader->Read(&shape);
        } else if (key == "type") {
          reader->Read(&type);
        } else if (key == "perf") {
          reader->BeginArray();
          while (reader->NextArrayItem()) {
            reader->BeginObject();
            std::string dev;
            float perf;
            while (reader->NextObjectItem(&key)) {
                if (key == "dev_from_to") {
                    reader->Read(&dev);
                } else if (key == "perf") {
                    reader->Read(&perf);
                }
            }
            perf_map[dev] = perf;
            std::cout << "shape:" << "shape" << " type:" << type << " perf:" << dev << ":"<< perf;
            std::cout << std::endl;
          }
        } else {
          LOG(FATAL) << "do not support key " << key;
        }
      }
      comu_cost[ShapeToString(shape, type)] = perf_map; 
    }
    return;
}

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

std::string AutoTune::ShapeToString(std::vector<int>& shape, std::string dtype) {
    std::ostringstream ostr;
    for (auto x:shape) {
      ostr << x << ",";
    }
    ostr << ":" << dtype;
    return ostr.str();
}

float AutoTune::GetCommuCost(DPItem& cur, DPItem& next) {
    DevType cur_dev_type = cur.dev_type, next_dev_type = next.dev_type;
    auto shape = cur.end_layer.shape;
    auto dtype = cur.end_layer.data_type;
    auto device_from_to =
      DPItem_::GetTypeString(cur_dev_type) + ":" + DPItem_::GetTypeString(next_dev_type);

    auto shape_info = ShapeToString(shape, dtype);
    auto data_move_map = comu_cost.find(shape_info);
    if (data_move_map == comu_cost.end()) {
      LOG(WARNING) << "not find the data" << shape_info;
      return 0;
    }
    auto data_map = data_move_map->second;
    auto perf = data_map.find(device_from_to);
    if (perf == data_map.end()) {
      LOG(WARNING) << "not find the data for the device pair" << device_from_to;
      return 0;
    }
    
    return perf->second;
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
            int len = sub_list.size();
            for (auto cur = sub_list.begin(); cur != sub_list.end(); cur++) {
                //std::cout << x ;
                auto next = std::next(cur);
                auto commu_cost = 0;
                if (next != sub_list.end()) {
                  commu_cost = GetCommuCost(*cur, *next);
                }
                auto cur_perf =  GetPerSum(*cur, lperf) + commu_cost;
                cur->perf = cur_perf;
                perf = std::max(perf, cur_perf);
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

std::string AutoTune::FormatBest(size_t list_max_num) {
      std::sort(perf_list.begin(), perf_list.end(), comp);
      std::ostringstream os;
      dmlc::JSONWriter writer(&os);
      std::cout << "The top " << list_max_num << "  best split configure are: " << std::endl;
      writer.BeginArray();
      for (int i = 0; i < std::min(perf_list.size(), list_max_num); i++) {
        std::string str_format;
        auto item = perf_list[i];
        int index = 0;
        std::vector<SubgraphItem> list;
        for (auto x:item.second) {
          x.SetLayerInfo(layer_map_);
          x.update_perf(layer_perf_);
          SubgraphItem item(x, index);
          list.push_back(item);
          std::cout << x << " ";
          /*[[{"start":{},"end":{}}, {"start":{},"end":{}}], []]
              */
          index ++;
        }
        SubgraphSplit split(list);
        writer.WriteArrayItem(split);
        std::cout << " perf is " << item.first << std::endl;
      }
      writer.EndArray();
      //std::cout << os.str() << std::endl;
      return os.str();
    }
float AutoTune::GetPerSum(DPItem di, std::unordered_map<int, PERF> lperf) {
        DevType  dtype = di.dev_type;
        float ret =0 ;
        for (int i = di.start; i <= di.end; i++) {
            ret += lperf[i][dtype];
        }
        return ret;
    }

String GetSplitConfig(const std::string& layer_json, const std::string& data_comu_json) {
  std::istringstream is(layer_json), is_data_comu(data_comu_json);
  dmlc::JSONReader reader(&is), reader_data(&is_data_comu);
  std::list<DPItem> list;
  AutoTune at(reader, reader_data);
  int net_depth = at.GetNetDepth();
  std::vector<DevType> available_dev {CPU, VTA};
  at.GenerateBalance(0, at.layer_perf_,available_dev.size(), -1,
                              {CPU, VTA}, net_depth, list, at.perf_list);
  return at.FormatBest();
}
TVM_REGISTER_GLOBAL("autotvm.feature.GetSplitConfig")
    .set_body([](TVMArgs args, TVMRetValue* ret) {
        *ret = GetSplitConfig(args[0], args[1]);
    });
}  // namespace autotvm
}  // namespace tvm
