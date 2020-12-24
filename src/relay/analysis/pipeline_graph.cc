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
 * \file extract_fused_functions.cc
 * \brief Apply fusion and extract fused primitive functions from an IRModule
 */
#include <tvm/node/structural_hash.h>
#include <tvm/relay/analysis.h>
#include <tvm/relay/expr.h>
#include <tvm/relay/expr_functor.h>
#include <tvm/relay/transform.h>
#include <iostream>
using namespace std;
namespace tvm {
namespace relay {

class PipelineGraphWrapper : private ExprVisitor {
 public:
  explicit PipelineGraphWrapper(const IRModule& mod) : mod_(mod) {}

  Expr Recursion(Expr anf) {
      if (anf.as<FunctionNode>()){
          auto a = anf.as<FunctionNode>();
          Recursion(a->body);
          //func = relay::Function(relay::FreeVars(expr), expr, Type(),\
             relay::FreeTypeVars(expr, mod), {})
          std::cout << " function " <<std::endl;
      }
      auto let = anf.as<LetNode>();
      if (let){
          auto call = let->value.as<CallNode>();
          if (call) {
              if (call->op.as<OpNode>()) {
                std::cout << " call " <<std::endl;
                auto op_node = call->op.as<OpNode>();
                std::cout << "op_node2    ---   " 
                    << op_node->name 
                    << " index is "
                    << indx
                    << " " 
                    << std::endl;
                indx ++;
              }

          }
          Recursion(let->body);
          std::cout << " let " <<std::endl;
      }
      std::cout << "Recursion " << std::endl;
      return anf;
  }

  IRModule Extract() {
    //VisitExpr(this->mod_->Lookup("main"));
    Recursion(this->mod_->Lookup("main"));
      /*

    auto functions = Map<GlobalVar, BaseFunc>();
    for (auto pair : this->functions) {
      functions.Set(GlobalVar(pair.first), pair.second);
_operator_idx_inc    }

    this->mod_->functions = functions;
    */
    return this->mod_;
  }

 private:
  const IRModule mod_;
  int   indx = 0;
  /*
  // This is not simply Map<GlobalVar, Function> because GlobalVar doesn't
  // have the desired equals property
  //Map<std::string, Function> functions;

  void VisitExpr_(const FunctionNode* n) final {
    if (n->HasNonzeroAttr(attr::kPrimitive)) {
      // Add function to functions, keyed by function hash string
      Function func = Function(n->params, n->body, n->ret_type, n->type_params, n->attrs);
      size_t hash_ = tvm::StructuralHash()(func);
      //this->functions.Set(std::to_string(hash_), func);
    }

    ExprVisitor::VisitExpr_(n);
  }
  */

  void VisitExpr_(const CallNode* call) final {
      if (call->op.as<OpNode>()) {
          auto op_node = call->op.as<OpNode>();
          std::cout << "op_node    ---   " 
                    << op_node->name 
                    << " index is "
                    << indx
                    << " " 
                    << std::endl;
          indx ++;
      }else {
          printf("not opnode\n");
      }
      ExprVisitor::VisitExpr_(call);
  }
};

namespace transform {

Pass PipelineGraph(const Array<Integer>& indxList) {
  runtime::TypedPackedFunc<IRModule(IRModule, PassContext)> pass_func =
      [=](IRModule m, PassContext pc) {
		return PipelineGraphWrapper(m).Extract(); 
      };
  auto pipeline_graph_pass = CreateModulePass(pass_func, 1, "PipelineGraph", {});
  std::cout << "pLevel is " << indxList[0] << std::endl;
  return Sequential({SimplifyInference(),
                     ToANormalForm(),
                     pipeline_graph_pass,
                     ToGraphNormalForm()},
                    "PipelineGraph");
}

TVM_REGISTER_GLOBAL("relay.analysis.PipelineGraph").set_body_typed(PipelineGraph);

}  // namespace transform

}  // namespace relay
}  // namespace tvm
