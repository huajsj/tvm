#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "zendnn.hpp"
using namespace zendnn;


using namespace zendnn;

inline void read_from_dnnl_memory(void* handle, const memory& mem) {
  size_t bytes = mem.get_desc().get_size();

  uint8_t* src = static_cast<uint8_t*>(mem.get_data_handle());
  std::copy(src, src + bytes, reinterpret_cast<uint8_t*>(handle));
}

int main() {
    // create the memory descriptors for the input tensors
    memory::desc src1_md, src2_md;
		algorithm algo = algorithm::binary_add;


		memory::dims a_shape = {1,1};
		memory::dims a_strides = {1,1};
	  memory::desc data_md(a_shape, memory::data_type::f32, a_strides);
		
		engine eng(engine::kind::cpu, 0);
    auto add_desc = binary::desc(algo, data_md, data_md, data_md);
    auto add_prim_desc = binary::primitive_desc(add_desc, eng);
    assert(data_md == add_prim_desc.dst_desc());
    auto add = binary(add_prim_desc);
	
		float data = 10, weight=1, out = 0;
		auto data_memory = memory(data_md, eng, &data);
		auto weight_memory = memory(data_md, eng, &weight);
		auto dst_memory = memory(data_md, eng);
		stream s(eng);
    add.execute(
      s,
      {{ZENDNN_ARG_SRC, data_memory}, {ZENDNN_ARG_SRC_1, weight_memory}, {ZENDNN_ARG_DST, dst_memory}});
 		 s.wait();
		 read_from_dnnl_memory(&out, dst_memory);
		 printf("out=%f\n",out);
}
