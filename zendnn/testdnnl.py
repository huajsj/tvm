import tvm,os
from tvm import relay
from tvm.relay import testing
from tvm.contrib import utils as util
from tvm.contrib import graph_runtime
import numpy as np

data=relay.var("data", relay.TensorType((1,1), "float32"))
const=relay.const(1, "float32")
net=relay.add(data, const)
f=relay.Function(relay.analysis.free_vars(net),net)
m, p=testing.create_workload(f)

byoc = "dnnl"
@tvm.ir.register_op_attr("adds", "target."+byoc)
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

    # Setup the gcc flag to compile DNNL code.
    kwargs = {}
    kwargs["options"] = ["-O2", "-std=c++17", "-I" + contrib_path]
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

idata =np.full((1,1),1.0, "float32")
mod.set_input("data", idata)
mod.set_input(**params)
mod.run()
out=mod.get_output(0)
print(out)
