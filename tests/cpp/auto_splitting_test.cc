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

#include <dmlc/logging.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <list>
#include <unordered_map>
#include <ostream>
//./cpptest --gtest_filter=AutoSplitting.AutoSplitting
enum DevType {
    CPU=0,
    GPU,
    FPGA,
    DEV_MAX,
};
typedef struct DPItem_ {
    DPItem_() {};
    DPItem_(int s, int e, DevType d):start(s), end(e), dev_type(d){};
    int start;
    int end;
    float perf = std::numeric_limits<float>::max();
    DevType dev_type = DEV_MAX;
    std::string GetType() {
      std::string ret;
      switch(dev_type) {
        case CPU:
          ret = "CPU";
          break;
        case GPU:
          ret = "GPU";
          break;
        case FPGA:
          ret = "FPGA";
          break;
      }
      return ret;
    }
    friend std::ostream& operator <<(std::ostream& os, struct DPItem_ di) {
        os << " "<<  di.start << "-" << di.end << " type:" << di.GetType();
        return os;
    }
}DPItem;

struct {
 bool operator()(std::pair<float, std::list<DPItem>>& l,
            std::pair<float, std::list<DPItem>>& r) const {
              return l.first < r.first;
       }
}comp;

class AutoTune {
    using PERF=std::unordered_map<DevType, float>;
 public:
    AutoTune() {srand(time(NULL));}
    float RandPerf(int pos, DevType dtype) {
        auto weight = range_[0] + rand()% (int)(range_[1]-range_[0]);
        auto dev_weight = dev_[dtype];
        return weight * perf_assume_[pos] * dev_weight;
    }
    void GenerateGraph(int num) {
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
        backend_map = std::vector<std::vector<DPItem>>(dev_.size(), std::vector<DPItem>(num, DPItem()));
    }
    static bool cmp(std::vector<int>&l, std::vector<int>& r) {
        return l[0] < r[0];
    }
    static void GenerateBalance(int current_pipeline_index, 
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
    bool BalanceVerify() {
        //int layer_end = layers_.size();
        std::vector<std::vector<int>> sub_graph_list;
        for (size_t i = 0; i < split_conf.size(); i++) {
            auto sub_graph = split_conf[i].first;
        }
        return true;
    }
    void ShowBest() {
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
    std::unordered_map<int, PERF> layer_perf_;
    /*device weight*/
    PERF dev_={{CPU,1.0}, {GPU,0.20}, {FPGA,0.3}};
    std::vector<std::pair<float, std::list<DPItem>>> perf_list;
 private:
    static float GetPerSum(DPItem di, std::unordered_map<int, PERF> lperf) {
        DevType  dtype = di.dev_type;
        float ret =0 ;
        for (int i = di.start; i <= di.end; i++) {
            ret += lperf[i][dtype];
        }
        return ret;
    }
    
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
