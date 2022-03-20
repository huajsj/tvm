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
.. _tutorial-pipeline-executor:

Pipeline executing runtime.
====================
**Author**: `hua jiang`

"""
# some standard imports
import mxnet as mx
import torch
import tvm
from tvm import relay, autotvm
import numpy as np
import threading
from tvm._ffi import get_global_func
from tvm.relay.analysis import pipeline_graph
from tvm.contrib import utils
from tvm import rpc

config_threadpool = get_global_func('runtime.config_threadpool')
######################################################################
# Download Resnet18 model from Gluon Model Zoo
# ---------------------------------------------
# In this section, we download a pretrained imagenet model and classify an image.
from tvm.contrib.download import download_testdata
from mxnet.gluon.model_zoo.vision import get_model
from PIL import Image
from matplotlib import pyplot as plt

from tvm.contrib import graph_executor, pipeline_executor
import time
block = get_model("resnet18_v1", pretrained=True)
shuffle = torch.hub.load('pytorch/vision:v0.10.0', 'shufflenet_v2_x1_0', pretrained=True)
loop = 1
pipeline = True
def get_image():
    img_url = "https://github.com/dmlc/mxnet.js/blob/main/data/cat.png?raw=true"
    img_name = "cat.png"
    synset_url = "".join(
        [
            "https://gist.githubusercontent.com/zhreshold/",
            "4d0b62f3d01426887599d4f7ede23ee5/raw/",
            "596b27d23537e5a1b5751d2b0481ef172f58b539/",
            "imagenet1000_clsid_to_human.txt",
        ]
    )
    synset_name = "imagenet1000_clsid_to_human.txt"
    img_path = download_testdata(img_url, "cat.png", module="data")
    synset_path = download_testdata(synset_url, synset_name, module="data")
    with open(synset_path) as f:
        synset = eval(f.read())
    image = Image.open(img_path).resize((224, 224))
    #plt.imshow(image)
    #plt.show()


    def transform_image(image):
        image = np.array(image) - np.array([123.0, 117.0, 104.0])
        image /= np.array([58.395, 57.12, 57.375])
        image = image.transpose((2, 0, 1))
        image = image[np.newaxis, :]
        return image


    x = transform_image(image)
    return x, synset


def get_network(x):
    ######################################################################
    # Compile the Graph
    # -----------------
    # Now we would like to port the Gluon model to a portable computational graph.
    # It's as easy as several lines.
    # We support MXNet static graph(symbol) and HybridBlock in mxnet.gluon
    shape_dict = {"data": x.shape}
    mod, params = relay.frontend.from_mxnet(block, shape_dict)
    #'''
    #func = relay.build_module.bind_params_by_name(mod["main"], params)
    #mod = tvm.IRModule()
    #mod["main"] = func
    #with relay.quantize.qconfig(global_scale=8.0, skip_conv_layers=[0]):
    #	mod = relay.quantize.quantize(mod, params=params)
    #'''
    func = mod["main"]
    func = relay.Function(func.params, relay.nn.softmax(func.body), None, func.type_params,
                                                        func.attrs)
    pl = [43]
    mods = pipeline_graph(func, pl, params)
    return func, mods, params

local_demo = False
if local_demo:
    remote = rpc.LocalSession()
else:
    # The following is my environment, change this to the IP address of your target device
    host = "172.19.1.141"
    port = 9090
    remote = rpc.connect(host, port)

def remote_build(mod, target, params=None, target_host=None, mod_name="default"):
    build_func = relay.build
    lib = build_func(mod, target=target, params=params, target_host=target_host, mod_name= mod_name)

    temp = utils.tempdir()
    path = temp.relpath("lib.tar")
    lib.export_library(path)
    remote.upload(path)
    lib = remote.load_module("lib.tar")
    return lib

def pipe_test(mods, img):
    mod1, mod2 = mods[0], mods[1]
    pipe_config = pipeline_executor.PipelineConfig()
    pipe_config[mod1].target = "llvm"#"cuda" #"llvm"
    pipe_config[mod1].dev = tvm.cpu(0)#tvm.cuda(0)#tvm.cpu(0)
    pipe_config[mod1].cpu_affinity = "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15"

    #remote = rpc.LocalSession()
    pipe_config[mod2].target = "llvm"
    pipe_config[mod2].dev = remote.cpu(0)#tvm.cpu(0)
    pipe_config[mod2].cpu_affinity = "8,9,10,11,12,13,14,15"
    pipe_config[mod2].build_func = remote_build


    pipe_config["input"]["data"].connect(pipe_config[mod1]["input"]["data"])
    pipe_config[mod1]["output"][0].connect(pipe_config[mod2]["input"]["x_93"])
    pipe_config[mod2]["output"]["0"].connect(pipe_config["output"][0])
    mconfig = pipe_config.get_config()
    with tvm.transform.PassContext(opt_level=3):
        pipeline_mod_factory = pipeline_executor.build(pipe_config)
    pipeline_module = pipeline_executor.PipelineModule(pipeline_mod_factory)
    t1 = time.time()
    if not pipeline:
        cpu_list = ['0','1','2','3','4','5','6','7']
        config_threadpool(-3, 8, cpu_list)

    for i in range(0, loop):
        if pipeline:
            pipeline_module.set_input("data", img)
            pipeline_module.run(0)
        else:
            pipeline_module.set_input("data", img)
            pipeline_module.set_input("data", img)
            pipeline_module.set_input("data", img)
            pipeline_module.run(1)
            outputs = pipeline_module.get_output()
    
    if pipeline:
        num = 0
        while num < loop:
            while len(outputs := pipeline_module.get_output()) == 0:
                time.sleep(0.001)
            num = num + 1

    t2 = time.time()
    print("pipe test spend time is %s", t2 - t1)
    top1 = np.argmax(outputs[0].numpy())
    print("TVM prediction top-1:", top1, synset[top1])


def normal_test(func, x):
    ## we want a probability so add a softmax operator
    #func = mod["main"]
    #func = relay.Function(func.params, relay.nn.softmax(func.body), None, func.type_params,
    #                                                    func.attrs)

    ######################################################################
    # now compile the graph
    do_remote = True
    #target = "cuda"
    #dev = tvm.cuda(0)
    target = "llvm"
    dev = tvm.cpu(0)
    log_file = "/scratch/hj/tvm-auto-ml/tvm-automl/mxnet_graph_opt.log.16"
    with autotvm.apply_history_best(log_file):
        with tvm.transform.PassContext(opt_level=3):
            #lib = relay.build(func, target, params=params)
            lib = remote_build(func, target, params=params)
            '''
            #export lib
            '''
            if do_remote:
                temp = utils.tempdir()
                path = temp.relpath("lib.tar")
                lib.export_library(path)
                remote = rpc.LocalSession()
                remote.upload(path)
                lib = remote.load_module("lib.tar")
                dev = remote.cpu(0)

    ######################################################################
    # Execute the portable graph on TVM
    # ---------------------------------
    # Now, we would like to reproduce the same forward computation using TVM.
    from tvm.contrib import graph_executor

    dtype = "float32"

    #cpu_list = ['0','16','2','17', '3', '18', '4', '19', '5','21','16','22','7','23','8','24']
    #cpu_list = ['0','1','2','3', '4', '5', '6', '7','8','9','10','11','12','13','14','15']
    #cpu_list = ['0', '1']
    #cpu_list = ['0','1','2','3','4','5','6','7']
    #config_threadpool(-3, 2, cpu_list)
    m = graph_executor.GraphModule(lib["default"](dev))
    t1 = time.time()
    # set inputs
    # execute
    #timer = m.module.time_evaluator("run", dev, number=10, repeat=10)
    for i in range(0, loop):
        m.set_input("data", tvm.nd.array(x.astype(dtype)))
        m.run()
        tvm_output = m.get_output(0)
    t2 = time.time()
    print("normaltest time is %s", t2 - t1)
    #tcost = timer()
    #std = np.std(tcost.results) * 1000
    #mean = tcost.mean * 1000
    #print("\nPerformed inference in %.2fms (std = %.2f)" % (mean, std))
    # get outputs
    top1 = np.argmax(tvm_output.numpy()[0])
    print("TVM prediction top-1:", top1, synset[top1])

x, synset = get_image()
mod, mods, params = get_network(x)
print("x", x.shape)
#normal_test(mod, x)
pipe_test(mods, x)
