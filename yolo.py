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
"""
Deploy Pretrained Vision Detection Model from Darknet on VTA
============================================================
**Author**: `Hua Jiang <https://github.com/huajsj>`_

This tutorial provides an end-to-end demo, on how to run Darknet YoloV3-tiny
inference onto the VTA accelerator design to perform Image detection tasks.
It showcases Relay as a front end compiler that can perform quantization (VTA
only supports int8/32 inference) as well as graph packing (in order to enable
tensorization in the core) to massage the compute graph for the hardware target.
"""

######################################################################
# Install dependencies
# --------------------
# To use the autotvm package in tvm, we need to install some extra dependencies.
# (change "3" to "2" if you use python2):
#
# .. code-block:: bash
#
#   pip3 install "Pillow<7"
#
# YOLO-V3-tiny Model with Darknet parsing have dependancy with CFFI and CV2 library,
# we need to install CFFI and CV2 before executing this script.
#
# .. code-block:: bash
#
#   pip3 install cffi
#   pip3 install opencv-python
#
# Now return to the python code. Import packages.

from __future__ import absolute_import, print_function

import sys
import os
import time
import matplotlib.pyplot as plt
import numpy as np
import tvm
import vta
from tvm import rpc, autotvm, relay
from tvm.relay.testing import yolo_detection, darknet
from tvm.relay.testing.darknet import __darknetffi__
from tvm.contrib import graph_executor, utils
from tvm.contrib.download import download_testdata
from vta.testing import simulator
from vta.top import graph_pack
from tvm.relay.analysis import parse_network, parse_layer_perf, pipeline_graph
from tvm.contrib import graph_executor, pipeline_executor
import json
# Make sure that TVM was compiled with RPC=1
assert tvm.runtime.enabled("rpc")
cpu_log = "./yolov3-tiny-arm-cpu.log"
vta_log = "./yolov3-tiny.log"
env = vta.get_env()

def vta_build(mod, target, params=None, target_host=None, mod_name="default"):
    target = env.target
    with autotvm.apply_history_best(vta_log):
        with relay.build_config(opt_level=3, disabled_pass={"AlterOpLayout"}):
            with vta.build_config(debug_flag=0):
                #libs = relay.build(mod, target=target, params=params, 
                 #         target_host="llvm -mtriple=aarch64-linux-gnu", mod_name= "default")
                libs = relay.build(
                    mod, target=tvm.target.Target(target, host=env.target_host), params=params
                )
    return libs

def arm_cpu_build(mod, target, params=None, target_host=None, mod_name="default"):
    with autotvm.apply_history_best(cpu_log):
        with relay.build_config(opt_level=3):
            libs = relay.build(mod, target=target, params=params,
                               target_host=target_host, mod_name= mod_name)
    return libs

def GetModule():
    ##############################################################################
    # Download yolo net configure file, weight file, darknet library file based on
    # Model Name
    # ----------------------------------------------------------------------------
    MODEL_NAME = "yolov3-tiny"
    REPO_URL = "https://github.com/dmlc/web-data/blob/main/darknet/"

    cfg_path = download_testdata(
        "https://github.com/pjreddie/darknet/blob/master/cfg/" + MODEL_NAME + ".cfg" + "?raw=true",
        MODEL_NAME + ".cfg",
        module="darknet",
    )
    weights_path = download_testdata(
        "https://pjreddie.com/media/files/" + MODEL_NAME + ".weights" + "?raw=true",
        MODEL_NAME + ".weights",
        module="darknet",
    )

    if sys.platform in ["linux", "linux2"]:
        darknet_lib_path = download_testdata(
            REPO_URL + "lib/" + "libdarknet2.0.so" + "?raw=true", "libdarknet2.0.so", module="darknet"
        )
    elif sys.platform == "darwin":
        darknet_lib_path = download_testdata(
            REPO_URL + "lib_osx/" + "libdarknet_mac2.0.so" + "?raw=true",
            "libdarknet_mac2.0.so",
            module="darknet",
        )
    else:
        raise NotImplementedError("Darknet lib is not supported on {} platform".format(sys.platform))

    ##################################################
    # Download yolo categories and illustration front.
    # ------------------------------------------------
    coco_path = download_testdata(
        REPO_URL + "data/" + "coco.names" + "?raw=true", "coco.names", module="data"
    )
    font_path = download_testdata(
        REPO_URL + "data/" + "arial.ttf" + "?raw=true", "arial.ttf", module="data"
    )
    with open(coco_path) as f:
        content = f.readlines()
    names = [x.strip() for x in content]

    pack_dict = {
        "yolov3-tiny": ["nn.max_pool2d", "cast", 4, 186],
    }

    # Name of Darknet model to compile
    # The ``start_pack`` and ``stop_pack`` labels indicate where
    # to start and end the graph packing relay pass: in other words
    # where to start and finish offloading to VTA.
    # the number 4 indicate the the ``start_pack`` index is 4, the
    # number 186 indicate the ``stop_pack index`` is 186, by using
    # name and index number, here we can located to correct place
    # where to start/end when there are multiple ``nn.max_pool2d``
    # or ``cast``, print(mod.astext(show_meta_data=False)) can help
    # to find operator name and index information.
    assert MODEL_NAME in pack_dict

    # Load pre-configured AutoTVM schedules
    #with autotvm.tophub.context(target):
    net = __darknetffi__.dlopen(darknet_lib_path).load_network(
        cfg_path.encode("utf-8"), weights_path.encode("utf-8"), 0
    )
    dshape = (1, net.c, net.h, net.w)
    dtype = "float32"

    # Measure build start time
    build_start = time.time()

    # Start front end compilation
    mod, params = relay.frontend.from_darknet(net, dtype=dtype, shape=dshape)

    # Perform quantization in Relay
    # Note: We set opt_level to 3 in order to fold batch norm
    with tvm.transform.PassContext(opt_level=3):
        with relay.quantize.qconfig(
            global_scale=23.0,
            skip_conv_layers=[0],
            store_lowbit_output=True,
            round_for_shift=True,
        ):
            mod = relay.quantize.quantize(mod, params=params)
        # Perform graph packing and constant folding for VTA target
        mod = graph_pack(
            mod["main"],
            env.BATCH,
            env.BLOCK_OUT,
            env.WGT_WIDTH,
            start_name=pack_dict[MODEL_NAME][0],
            stop_name=pack_dict[MODEL_NAME][1],
            start_name_idx=pack_dict[MODEL_NAME][2],
            stop_name_idx=pack_dict[MODEL_NAME][3],
        )
    #vta_build(mod, env.target)
    return mod

def GraphSplit(conf):
    #f = open("./output.json")
    #config  = json.load(f)
    config  = json.loads(conf)
    split_conf = []
    for conf in config[0]:
        c_conf = {}
        c_conf["op_name"] = conf['layer_info']["end"]["op_name"]
        c_conf["op_index"] = conf['layer_info']["end"]["op_index"]
        split_conf.append(c_conf)
    return split_conf[:len(split_conf)-1]

def SplitConf():
    config = {'cpu':cpu_log, 'vta':vta_log}
    mod = GetModule()
    # Get the operator performance information.
    net_conf, data_list = parse_network(mod, config)
    print(data_list)
    # Get top N best split solution
    get_split = tvm._ffi.get_global_func("autotvm.feature.GetSplitConfig", allow_missing=False)
    conf = get_split(str(net_conf))
    # Create the split configuration
    indices = GraphSplit(conf)
    # Get the subgraph
    subs = pipeline_graph(mod, indices)
    return subs

def Compile(mods):
    for sub in mods:
        print(sub)
    target = "llvm -mtriple=aarch64-linux-gnu"
    libs = relay.build(mods[0], target= target)
    pipe_config = pipeline_executor.PipelineConfig()
    pipe_config[mods[0]].target = "llvm -keys=arm_cpu,cpu -device=arm_cpu -link-params=0 \
                                   -mattr=+neon -model=ultra96 -mtriple=aarch64-linux-gnu"

    pipe_config[mods[0]].dev = tvm.cpu(0)
    pipe_config[mods[0]].cpu_affinity = "0"
    pipe_config[mods[0]].build_func = arm_cpu_build

    target = "ext_dev -keys=vta,cpu -device=vta -model=ultra96_1x16_i8w8a32_15_15_18_17"
    host = "llvm -mtriple=aarch64-linux-gnu"
    pipe_config[mods[1]].target = tvm.target.Target(target, host = host)
    pipe_config[mods[1]].dev = tvm.ext_dev(0)
    pipe_config[mods[1]].cpu_affinity = "0"
    pipe_config[mods[1]].build_func = vta_build

    pipe_config["input"]["data"].connect(pipe_config[mods[0]]["input"]["data"])
    m2_input_name = "x_1546"
    pipe_config[mods[0]]["output"][0].connect(pipe_config[mods[1]]["input"][m2_input_name])

    libs = pipeline_executor.build(pipe_config)
    for i in range(0, len(libs)):
        libs[i]["lib"].lib.export_library(f"./{i}.tar")
    '''
    directory_path = tvm.contrib.utils.tempdir().temp_dir
    # If the directory does not exist, create it.
    if not os.path.exists(directory_path):
        os.makedirs(directory_path)
    config_file_name = pipeline_mod_factory.export_library(directory_path)
    '''

mods = SplitConf()
Compile(mods)
