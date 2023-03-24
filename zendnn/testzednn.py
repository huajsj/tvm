import tvm,os
from tvm import relay
from tvm.relay import testing
from tvm.contrib import utils as util
from tvm.contrib import graph_runtime
import numpy as np

batch = 2
idim = 10
odim = 20
dshape=(batch,idim)
wshape=(idim, odim)
bshape=(odim,)
const_data=np.full(dshape,1.0,"float32")
data=relay.var("data", relay.TensorType(dshape, "float32"))
weight=relay.var("weight", relay.TensorType(wshape, "float32"))
bias=relay.var("bias", relay.TensorType(bshape, "float32"))

const=relay.const(const_data, "float32")
net=relay.add(data, const)
net=relay.nn.dense(net,weight)
f=relay.Function(relay.analysis.free_vars(net),net)
m, p=testing.create_workload(f)

byoc = "zendnn"
@tvm.ir.register_op_attr("add", "target."+byoc)
def _support(attr):
    return True

@tvm.ir.register_op_attr("nn.dense", "target."+byoc)
def _support(attr):
    return True

pm=relay.transform.AnnotateTarget(byoc)(m)
mod=relay.transform.PartitionGraph()(pm)

graph, lib, params =relay.build(mod, "llvm")

for m in lib.imported_modules:
    print(m.get_source())

def update_lib(lib):
    test_dir = os.path.dirname(os.path.realpath(os.path.expanduser(__file__)))
    source_dir = os.path.join(test_dir, ".." )
    contrib_path = os.path.join(source_dir, "src", "runtime", "contrib")
    print(contrib_path)
    kwargs={}
    # Setup the gcc flag to compile ZEDNN code.
    #kwargs["options"] = ["-DLIBM_ENABLE=1", "-std=c++17", "-O3", "-fPIC", "-fopenmp",
    #                         "-DBIAS_ENABLED=1", "-DZENDNN_ENABLE=1",
    #                         "-I/scratch/staff/huaj/tvm/zendnn/ZenDNN/inc",
    #                         "-I/scratch/staff/huaj/tvm/zendnn/ZenDNN/aocl-linux-gcc-4.0/amd-blis/include",
    #                         "-I/scratch/staff/huaj/tvm/zendnn/ZenDNN/aocl-linux-gcc-4.0/amd-libm/include",
    #                         "-L/scratch/staff/huaj/tvm/zendnn/ZenDNN/_out/lib",
    #                         "-lamdZenDNN",
    #                         "-L/scratch/staff/huaj/tvm/zendnn/ZenDNN/aocl-linux-gcc-4.0/amd-blis/lib",
    #                         "-lblis-mt",
    #                         "-L/scratch/staff/huaj/tvm/zendnn/ZenDNN/aocl-linux-gcc-4.0/amd-libm/lib",
    #                         "-lalm",
    kwargs["options"] = [ "-std=c++17", "-O3","-I" + contrib_path,
                         "-I/scratch/staff/huaj/tvm/zendnn/ZenDNN/inc"]

    tmp_path = util.tempdir()
    lib_name = 'lib.so'
    lib_path = tmp_path.relpath(lib_name)

    # The generated C code with DNNL APIs is compiled to a binary lib.so.
    lib.export_library(lib_path, fcompile=False, **kwargs)

    # Load the lib.so back to a runtime module.
    lib = tvm.runtime.load_module(lib_path)
    return lib

lib = update_lib(lib)
mod = graph_runtime.create(graph, lib, tvm.cpu(0))

idata =np.full(dshape,1.0, "float32")
wdata =np.full(wshape,2.0, "float32")
mod.set_input("data", idata)
mod.set_input("weight", wdata)
mod.set_input(**params)
mod.run()
out=mod.get_output(0)
print(out)
