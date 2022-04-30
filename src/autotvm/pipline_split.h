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

#include <cassert>
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
using PERF=std::unordered_map<DevType, float>;
struct LayerInfo {
  LayerInfo(std::string name, int op_idx, int network_idx):op_name(name), op_index(op_idx),
     network_index(network_idx){ empty_ = false;}
  LayerInfo(){
  }
  bool empty() {return empty_;};
  bool empty_ = true;
  std::string op_name;
  int op_index;
  int network_index;
  void Save(dmlc::JSONWriter* writer) const {
     writer->BeginObject();
     writer->WriteObjectKeyValue("op_index", op_index);
     writer->WriteObjectKeyValue("network_index", network_index);
     writer->WriteObjectKeyValue("op_name", op_name);
     writer->EndObject();
  }
};
typedef struct DPItem_ {
    DPItem_() {};
    DPItem_(int s, int e, DevType d):start(s), end(e), dev_type(d){};
    int start;
    int end;
    float perf = std::numeric_limits<float>::max();
    DevType dev_type = DEV_MAX;
    LayerInfo start_layer, end_layer;
    std::unordered_map<int, LayerInfo> layer_map;
    void update_perf(std::unordered_map<int, PERF> lperf) {
        float ret =0 ;
        for (int i = start; i <= end; i++) {
            ret += lperf[i][dev_type];
        }
        perf = ret;
      
    }
    void Save(dmlc::JSONWriter* writer) const{
      assert(!start_layer.empty());
      assert(!end_layer.empty());
      writer->BeginObject();
      writer->WriteObjectKeyValue("start", start_layer);
      writer->WriteObjectKeyValue("end", end_layer);
      writer->WriteObjectKeyValue("device", DPItem_::GetTypeString(dev_type));
      writer->WriteObjectKeyValue("perf", perf);
      writer->EndObject();
    }
    void SetLayerInfo(std::unordered_map<int, LayerInfo>  info) {
      start_layer = info[start];
      end_layer = info[end];
    }
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

class SubgraphItem {
 public:
   explicit SubgraphItem(DPItem item, int index):dpitem_(item), subgraph_index_(index) {
   }
   void Save(dmlc::JSONWriter* writer) const{
      writer->BeginObject();
      writer->WriteObjectKeyValue("subgraph_index", subgraph_index_);
      writer->WriteObjectKeyValue("layer_info", dpitem_);
      writer->EndObject();
   }
 private:
  int subgraph_index_;
  DPItem dpitem_;
};

class SubgraphSplit{
 public:
   explicit SubgraphSplit(std::vector<SubgraphItem> slist):subgraph_list_(slist) {
   }
   void Save(dmlc::JSONWriter* writer) const{
     writer->BeginArray();
     for (auto x:subgraph_list_) {
      writer->WriteArrayItem(x);
     }
     writer->EndArray();
   }
 private:
   std::vector<SubgraphItem> subgraph_list_;
};

class AutoTune {
 public:
    AutoTune(dmlc::JSONReader& reader, dmlc::JSONReader& reader_data) {
      srand(time(NULL));
      this->LoadConfig(&reader);
      this->LoadDataMoveConfig(&reader_data);
    }
    std::string OperatorUnifyID(std::string op_name, int op_index);
    int GetNetDepth() {return layer_map_.size();}
    void LoadConfig(dmlc::JSONReader* reader);
    void LoadDataMoveConfig(dmlc::JSONReader* reader);
    float GetCommuCost(DPItem& cur, DPItem& next);
    void GenerateGraph(int num);
    void GenerateBalance(int current_pipeline_index, 
                         std::unordered_map<int, PERF> lperf,
                         int dev_num, 
                         int prev_pipeline_end_bound, 
                         std::vector<DevType> available_dev, 
                         int network_depth,
                         std::list<DPItem> &sub_list,
                         std::vector<std::pair<float, std::list<DPItem>>>& perf_list);
    std::string FormatBest(size_t list_max_num = 10);
    std::unordered_map<int, PERF> layer_perf_;
    std::unordered_map<int, LayerInfo> layer_map_;
    /*device weight*/
    PERF dev_={{CPU,1.0}, {GPU,0.20}, {VTA,0.3}};
    std::vector<std::pair<float, std::list<DPItem>>> perf_list;
    std::unordered_map<std::string, std::unordered_map<std::string, float>> comu_cost;
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
