#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <cmath>
#include <algorithm>
#include "zendnn.hpp"
using namespace zendnn;
using tag = memory::format_tag;
using dt = memory::data_type;
inline void read_from_dnnl_memory(void* handle, const memory& mem) {
  size_t bytes = mem.get_desc().get_size();

  uint8_t* src = static_cast<uint8_t*>(mem.get_data_handle());
  std::copy(src, src + bytes, reinterpret_cast<uint8_t*>(handle));
}

// Read from memory, write to handle
inline void read_from_zendnn_memory(void *handle, zendnn::memory &mem) {
    zendnn::engine eng = mem.get_engine();
    size_t bytes = mem.get_desc().get_size();

    if (eng.get_kind() == zendnn::engine::kind::cpu) {
        uint8_t *src = static_cast<uint8_t *>(mem.get_data_handle());
        for (size_t i = 0; i < bytes; ++i) {
            ((uint8_t *)handle)[i] = src[i];
        }
    }
#if ZENDNN_GPU_RUNTIME == ZENDNN_RUNTIME_OCL
    else if (eng.get_kind() == zendnn::engine::kind::gpu) {
        zendnn::stream s(eng);
        cl_command_queue q = s.get_ocl_command_queue();
        cl_mem m = mem.get_ocl_mem_object();

        cl_int ret = clEnqueueReadBuffer(
                         q, m, CL_TRUE, 0, bytes, handle, 0, NULL, NULL);
        if (ret != CL_SUCCESS)
            throw std::runtime_error("clEnqueueReadBuffer failed. Status Code: "
                                     + std::to_string(ret) + "\n");
    }
#endif
}

// Read from handle, write to memory
inline void write_to_zendnn_memory(void *handle, zendnn::memory &mem) {
    zendnn::engine eng = mem.get_engine();
    size_t bytes = mem.get_desc().get_size();

    if (eng.get_kind() == zendnn::engine::kind::cpu) {
        uint8_t *dst = static_cast<uint8_t *>(mem.get_data_handle());
        for (size_t i = 0; i < bytes; ++i) {
            dst[i] = ((uint8_t *)handle)[i];
        }
    }
#if ZENDNN_GPU_RUNTIME == ZENDNN_RUNTIME_OCL
    else if (eng.get_kind() == zendnn::engine::kind::gpu) {
        zendnn::stream s(eng);
        cl_command_queue q = s.get_ocl_command_queue();
        cl_mem m = mem.get_ocl_mem_object();
        size_t bytes = mem.get_desc().get_size();

        cl_int ret = clEnqueueWriteBuffer(
                         q, m, CL_TRUE, 0, bytes, handle, 0, NULL, NULL);
        if (ret != CL_SUCCESS)
            throw std::runtime_error(
                "clEnqueueWriteBuffer failed. Status Code: "
                + std::to_string(ret) + "\n");
    }
#endif
}

void add_test() {
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

void mat_test() {
	// Tensor dimensions.
	  zendnn::engine eng(engine::kind::cpu, 0);
	  zendnn::stream engine_stream(eng);
    const memory::dim MB = 3, // batch size
                      M = 128, K = 256, N = 512;
		// Source (src), weights, bias, and destination (dst) tensors dimensions.
    memory::dims src_dims = {M, K};
    memory::dims weights_dims = {K, N};
    memory::dims bias_dims = {1, N};
    memory::dims dst_dims = {M, N};
    // Allocate buffers.
    std::vector<float> src_data(M * K);
    std::vector<float> weights_data(K * N);
    std::vector<float> bias_data(1 * N);
    std::vector<float> dst_data(M * N);
    // Initialize src, weights, bias.
    std::generate(src_data.begin(), src_data.end(), []() {
        static int i = 0;
        return std::cos(i++ / 10.f);
    });
    std::generate(weights_data.begin(), weights_data.end(), []() {
        static int i = 0;
        return std::sin(i++ * 2.f);
    });
    std::generate(bias_data.begin(), bias_data.end(), []() {
        static int i = 0;
        return std::tanh(i++);
    });
    // Create memory descriptors and memory objects for src, weights, bias, and
    // dst.
    auto src_md = memory::desc(src_dims, dt::f32, tag::ab);
    auto weights_md = memory::desc(weights_dims, dt::f32, tag::ab);
    auto bias_md = memory::desc(bias_dims, dt::f32, tag::ab);
    auto dst_md = memory::desc(dst_dims, dt::f32, tag::ab);
    auto src_mem = memory(src_md, eng);
    auto weights_mem = memory(weights_md, eng);
    auto bias_mem = memory(bias_md, eng);
    auto dst_mem = memory(dst_md, eng);
    // Write data to memory object's handles.
    write_to_zendnn_memory(src_data.data(), src_mem);
    write_to_zendnn_memory(weights_data.data(), weights_mem);
    write_to_zendnn_memory(bias_data.data(), bias_mem);
    // Create operation descriptor
    auto matmul_d = matmul::desc(src_md, weights_md, bias_md, dst_md);
    // Create primitive post-ops (ReLU).
    const float scale = 1.0f;
    const float alpha = 0.f;
    const float beta = 0.f;
    post_ops matmul_ops;
    matmul_ops.append_eltwise(scale, algorithm::eltwise_relu, alpha, beta);
    primitive_attr matmul_attr;
    matmul_attr.set_post_ops(matmul_ops);
    // Create primitive descriptor.
    auto matmul_pd = matmul::primitive_desc(matmul_d, matmul_attr, eng);
    // Create the primitive.
    auto matmul_prim = matmul(matmul_pd);
    // Primitive arguments.
    std::unordered_map<int, memory> matmul_args;
    matmul_args.insert({ZENDNN_ARG_SRC, src_mem});
    matmul_args.insert({ZENDNN_ARG_WEIGHTS, weights_mem});
    matmul_args.insert({ZENDNN_ARG_BIAS, bias_mem});
    matmul_args.insert({ZENDNN_ARG_DST, dst_mem});
    // Primitive execution: matrix multiplication with ReLU.
    matmul_prim.execute(engine_stream, matmul_args);
    // Wait for the computation to finalize.
    engine_stream.wait();
    // Read data from memory object's handle.
    read_from_dnnl_memory(dst_data.data(), dst_mem);
		int i = 0;
		for (auto x:dst_data) {
			std::cout << x;
		  if ((i + 1)%8 == 0) std::cout << std::endl;
			i++;
		}
}

int main() {
		add_test();
		mat_test();
		return 0;
}
