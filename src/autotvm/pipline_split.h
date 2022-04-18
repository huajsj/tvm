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
 * \file touch_extractor.h
 * \brief Extract feature of touch pattern of axes in lowered IR
 */

#ifndef TVM_AUTOTVM_TOUCH_EXTRACTOR_H_
#define TVM_AUTOTVM_TOUCH_EXTRACTOR_H_

#include <dmlc/json.h>
#include <tvm/runtime/registry.h>
#include <tvm/tir/expr.h>
#include <tvm/tir/expr_functor.h>

#include <deque>
#include <map>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>
#include <cctype>

namespace tvm {
namespace autotvm {
enum DevType { CPU=0, GPU, VTA, DEV_MAX};
typedef struct DPItem_ {
    DPItem_() {};
    DPItem_(int s, int e, DevType d):start(s), end(e), dev_type(d){};
    int start;
    int end;
    float perf = std::numeric_limits<float>::max();
    DevType dev_type = DEV_MAX;
    static DevType GetType(std::string dev) {
      DevType dtype = DEV_MAX;
      std::transform(dev.begin(), dev.end(), dev.begin(),
                     [](const unsigned char letter){return std::toupper(letter);});
      if (dev == "CPU") {
        dtype = CPU;
      } else if (dev == "GPU") {
        dtype = GPU;
      } else if (dev == "VTA") {
        dtype = VTA;
      }
      return dtype;
    }
    static std::string GetTypeString(DevType dtype) {
      std::string ret;
      switch(dtype) {
        case CPU:
          ret = "CPU";
          break;
        case GPU:
          ret = "GPU";
          break;
        case VTA:
          ret = "VTA";
          break;
      }
      return ret;
    }
    friend std::ostream& operator <<(std::ostream& os, struct DPItem_ di) {
        os << " "<<  di.start << "-" << di.end << " type:" << DPItem_::GetTypeString(di.dev_type);
        return os;
    }
}DPItem;

struct {
 bool operator()(std::pair<float, std::list<DPItem>>& l,
            std::pair<float, std::list<DPItem>>& r) const {
              return l.first < r.first;
       }
}comp;

struct LayerInfo {
  LayerInfo(std::string name, int op_idx, int network_idx):op_name(name), op_index(op_idx),
     network_index(network_idx){;}
  LayerInfo(){}
  std::string op_name;
  int op_index;
  int network_index;
};

class AutoTune {
    using PERF=std::unordered_map<DevType, float>;
 public:
    AutoTune(dmlc::JSONReader& reader) {
      srand(time(NULL));
      this->LoadConfig(&reader);
    }
    int GetNetDepth() {return layer_map_.size();}
    void LoadConfig(dmlc::JSONReader* reader);
    void GenerateGraph(int num);
    static void GenerateBalance(int current_pipeline_index, 
                         std::unordered_map<int, PERF> lperf,
                         int dev_num, 
                         int prev_pipeline_end_bound, 
                         std::vector<DevType> available_dev, 
                         int network_depth,
                         std::list<DPItem> &sub_list,
                         std::vector<std::pair<float, std::list<DPItem>>>& perf_list);
    void ShowBest();
    std::unordered_map<int, PERF> layer_perf_;
    std::unordered_map<int, LayerInfo> layer_map_;
    /*device weight*/
    PERF dev_={{CPU,1.0}, {GPU,0.20}, {VTA,0.3}};
    std::vector<std::pair<float, std::list<DPItem>>> perf_list;
 private:
    static float GetPerSum(DPItem di, std::unordered_map<int, PERF> lperf);
    
    std::vector<std::string> layers_;
    /*generate perf*/
    std::vector<float> perf_assume_ = {1.2, 3.4, 2.5};
    /*perf weight*/
    std::vector<float> range_ = {1.0, 2.0};
    /*auto tune conf*/
    std::vector<std::pair<std::vector<int>, DevType>> split_conf;
    /**/
    std::vector<std::vector<DPItem>> backend_map;
};
}  // namespace autotvm
}  // namespace tvm

#endif  // TVM_AUTOTVM_TOUCH_EXTRACTOR_H_
