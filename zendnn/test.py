import tvm
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

lib=relay.build(mod, "llvm")
