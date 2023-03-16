import tvm,os
from tvm import relay
from tvm.relay import testing

data=relay.var("data", relay.TensorType((1,), "float32"))
const=relay.const(1, "float32")
net=relay.add(data, const)
f=relay.Function(relay.analysis.free_vars(net),net)
m, p=testing.create_workload(f)

byoc = "zendnn"
@tvm.ir.register_op_attr("add", "target."+byoc)
def _support(attr):
    return True

pm=relay.transform.AnnotateTarget(byoc)(m)
mod=relay.transform.PartitionGraph()(pm)

kwargs={}
kwargs["options"] = ["-DLIBM_ENABLE=1", "-std=c++14", "O3", "-fPIC", "-fopenmp",
                     "-DBIAS_ENABLED=1", "-DZENDNN_ENABLE=1",
                     "-I"]

compile_flag="-DLIBM_ENABLE=1 -std=c++14 -O3 -fPIC -fopenmp -DBIAS_ENABLED=1 -DZENDNN_ENABLE=1 -Werror -Wreturn-type -fconcepts -DZENDNN_X64=1 -march=znver2 -I/scratch/staff/huaj/tvm/zendnn/ZenDNN/inc -I/scratch/staff/huaj/tvm/zendnn/ZenDNN/aocl-linux-gcc-4.0/amd-blis//include -I/scratch/staff/huaj/tvm/zendnn/ZenDNN/aocl-linux-gcc-4.0/amd-libm//include -L/scratch/staff/huaj/tvm/zendnn/ZenDNN/_out/lib -lamdZenDNN -L/scratch/staff/huaj/tvm/zendnn/ZenDNN/aocl-linux-gcc-4.0/amd-blis//lib/ -lblis-mt -L/scratch/staff/huaj/tvm/zendnn/ZenDNN/aocl-linux-gcc-4.0/amd-libm//lib -lalm"
lib=relay.build(mod, "llvm")

for m in lib.lib.imported_modules:
    print(m.get_source())

