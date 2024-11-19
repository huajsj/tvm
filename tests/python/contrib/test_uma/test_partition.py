# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

import pytest
import numpy as np
import tvm
import tvm.relay as relay
from tvm.relay.backend.contrib.uma import uma_available
from tvm.relay.backend.contrib.uma.api import UMAPartitioner
from tvm.relay.op.contrib.register import get_pattern_table
from tvm.relay.testing import mlp, resnet

from tests.python.contrib.test_uma.test_uma_vanilla_accelerator import VanillaAcceleratorBackend
from collections import OrderedDict

from tvm.testing.aot import (
    AOTTestModel as AOTModel,
    AOTTestRunner as AOTRunner,
    generate_ref_data,
    compile_and_run,
    compile_models,
)
from tvm.micro.testing.aot_test_utils import AOT_DEFAULT_RUNNER

pytestmark = pytest.mark.skipif(not uma_available(), reason="UMA not available")


def test_partition_table():
    partitioner = UMAPartitioner("test_partition")
    assert get_pattern_table("test_partition") is None

    partitioner.register()

    assert get_pattern_table("test_partition") is not None

def test_partition_constant():
    weight_data = np.random.rand(3, 3, 3, 3).astype("float32") 
    weights = relay.const(weight_data, dtype="float32")
    input_shape = (1, 3, 224, 224)
    input_var = relay.var("input", relay.TensorType(input_shape, "float32"))

	# Use the constant in a conv2d operation
    conv2d_op = relay.nn.conv2d(
    	input_var,          # Input data
    	weights,            # Constant weights
    	strides=(1, 1),
    	padding=(1, 1),
    	channels=3,        # Number of output channels
    	kernel_size=(3, 3)
    )

	# Create a Relay function
    func = relay.Function([input_var], conv2d_op)
    mod = tvm.IRModule.from_expr(func)
    uma_backend = VanillaAcceleratorBackend()
    uma_backend.register()
    mod = uma_backend.partition(mod)

    uma_backend.partition(mod)
    target = tvm.target.Target("vanilla_accelerator", host=tvm.target.Target("c"))
    target_c = tvm.target.Target("c")
    target = [target_c, target]
    print(mod)
    export_directory = tvm.contrib.utils.tempdir(keep_for_debug=True).path
    print(f"Generated files are in {export_directory}")
	
    if 1:
        input_data = np.random.rand(1, 3, 224, 224).astype("float32") 
        inputs = OrderedDict([('input', input_data)])
        output_list = generate_ref_data(mod, inputs)
        testrunner = AOT_DEFAULT_RUNNER
        runner = AOTRunner(
					makefile=testrunner.makefile,
					prologue=testrunner.prologue,
					epilogue=testrunner.epilogue,
					includes=testrunner.includes,
					parameters=testrunner.parameters,
					pass_config=pass_config,
		)

        compile_and_run(
                AOTModel(module=mod, inputs=inputs, outputs=output_list),
                runner,
                interface_api="c",
                use_unpacked_api=True,
                target=target,
                test_dir=str("./tmp/"),
        )

@pytest.mark.parametrize(
    "workload,backend,merge",
    [
        ("resnet", "dnnl", False),
        ("resnet", "dnnl", True),
        ("mlp", "dnnl", False),
        ("mlp", "dnnl", True),
        ("resnet", "cutlass", False),
        ("resnet", "cutlass", True),
        ("mlp", "cutlass", False),
        ("mlp", "cutlass", True),
    ],
)
def test_existing_pattern_tables(workload, backend, merge):
    """Tests that uma partitioner creates the same partitions than default BYOC partitioning"""
    if 1:
        return
    partitioner = UMAPartitioner(backend, merge, False)
    pattern_table = get_pattern_table(backend)

    for entry in pattern_table:
        partitioner.add_pattern(*entry)

    if workload == "resnet":
        net = resnet.get_net(1, 10)
    elif workload == "mlp":
        net = mlp.get_net(1, 10)
    else:
        assert False, f"don't know how to find workload for {workload}"

    mod = tvm.ir.IRModule()
    mod["main"] = net

    partitioner.register()
    partitioned_mod = partitioner.partition(mod)

    def partition_default(mod):
        """partitions using default BYOC flow"""

        sequence = [
            relay.transform.MergeComposite(pattern_table),
            relay.transform.AnnotateTarget(backend),
        ]

        if merge:
            sequence.append(relay.transform.MergeCompilerRegions())

        sequence.append(relay.transform.PartitionGraph())
        sequential = tvm.transform.Sequential(sequence)

        return sequential(mod)

    default_partitioned_mod = partition_default(mod)

    assert len(partitioned_mod.functions) == len(default_partitioned_mod.functions)


if __name__ == "__main__":
    tvm.testing.main()
