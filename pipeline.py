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
loop = 1000
do_pipeline_runtime = True
pipeline_sequence = False
sequence_use_8 = False
do_cuda = False
do_remote = False
local_demo = True
model_name = 'resnet18_v1'
#model_name = 'vgg19'
#block = get_model("resnet18_v1", pretrained=True)
#block = get_model("resnet152_v1", pretrained=True)
#block = get_model("vgg19", pretrained=True)
block = get_model(model_name, pretrained=True)
model_info = {"resnet18_v1":{'split_pos':38, 'input_name':'x_74',},
#model_info = {"resnet18_v1":{'split_pos':43, 'input_name':'x_93',},
              "vgg19":{'split_pos':22, 'input_name':'x_36'},}
split_info = model_info[model_name]
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
    pl = [split_info['split_pos']]
    mods = pipeline_graph(func, pl, params)
    return func, mods, params

if local_demo:
    remote = rpc.LocalSession()
else:
    # The following is my environment, change this to the IP address of your target device
    host = "172.19.1.141"
    port = 9091
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
    pipe_config[mod1].target = "cuda" if do_cuda else "llvm"
    pipe_config[mod1].dev = tvm.cuda(0) if do_cuda else tvm.cpu(0)
    if not do_remote:
        pipe_config[mod1].cpu_affinity = "0,1,2,3,4,5,6,7"
    else:
        pipe_config[mod1].cpu_affinity = "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14"
    pipe_config[mod2].target = "llvm"
    pipe_config[mod2].dev = remote.cpu(0) if do_remote else tvm.cpu(0)
    if not do_remote:
        pipe_config[mod2].cpu_affinity = "8,9,10,11,12,13,14,15"
    else:
        pipe_config[mod2].cpu_affinity = "15,31"
    pipe_config[mod2].build_func = remote_build if do_remote else None


    pipe_config["input"]["data"].connect(pipe_config[mod1]["input"]["data"])
    pipe_config["input"]["x_83"].connect(pipe_config[mod2]["input"]["x_83"])
    pipe_config[mod1]["output"][0].connect(pipe_config[mod2]["input"][split_info['input_name']])
    pipe_config[mod2]["output"]["0"].connect(pipe_config["output"][0])
    mconfig = pipe_config.get_config()
    log_file = "/scratch/hj/tvm-auto-ml/tvm-automl/mxnet_graph_opt.log.8"
    with autotvm.apply_history_best(log_file):
        with tvm.transform.PassContext(opt_level=3):
            pipeline_mod_factory = pipeline_executor.build(pipe_config)
    pipeline_module = pipeline_executor.PipelineModule(pipeline_mod_factory)
    if pipeline_sequence:
        if sequence_use_8:
            cpu_list = ['0','1','2','3','4','5','6','7']
            config_threadpool(-3, 8, cpu_list)
        else:
            cpu_list = ['16','0','1','2','3','4','5','6','7','8','9','10','11','12','13','14','15']
            config_threadpool(-3, 17, cpu_list)
    first_data = \
    [tvm.nd.array(np.random.uniform(size=(1, 3, 224, 224)).astype('float32'), tvm.cpu(0)) \
    for i in range(0,15)]
    second_data = \
    [tvm.nd.array(np.random.uniform(size=(1, 128, 28, 28)).astype('float32'), tvm.cpu(0)) \
    for i in range(0,15)]
    t1 = time.time()
    
    for i in range(0, loop):
        if not pipeline_sequence:
            pipeline_module.set_input("data", first_data[i%15])#img)
            pipeline_module.set_input("x_83", second_data[i%15])
            pipeline_module.run(0)
        else:
            #pipeline_module.set_input("data", img)
            pipeline_module.set_input("data", img)
            pipeline_module.set_input("data", img)
            pipeline_module.run(1)
            outputs = pipeline_module.get_output()
    
    if not pipeline_sequence:
        num = 0
        while num < loop:
            while len(outputs := pipeline_module.get_output()) == 0:
                time.sleep(0.011)
            num = num + 1

    t2 = time.time()
    print("pipe test spend time is %s", t2 - t1)
    top1 = np.argmax(outputs[0].numpy())
    print("TVM prediction top-1:", top1, synset[top1])

def local_run(func, name, x, remote_do = False):
    ## we want a probability so add a softmax operator
    #func = mod["main"]
    #func = relay.Function(func.params, relay.nn.softmax(func.body), None, func.type_params,
    #                                                    func.attrs)

    ######################################################################
    # now compile the graph
    #target = "cuda"
    #dev = tvm.cuda(0)
    target = "cuda" if do_cuda else "llvm"
    dev = tvm.cuda(0) if do_cuda else tvm.cpu(0)
    log_file = "/scratch/hj/tvm-auto-ml/tvm-automl/mxnet_graph_opt.log.16"
    with autotvm.apply_history_best(log_file):
        with tvm.transform.PassContext(opt_level=3):
            if remote_do:
                lib = remote_build(func, target, params=params)
                dev = remote.cpu(0)
            else:
                lib = relay.build(func, target, params=params)

    from tvm.contrib import graph_executor

    dtype = "float32"

    #cpu_list = ['0','16','2','17', '3', '18', '4', '19', '5','21','16','22','7','23','8','24']
    #cpu_list = ['0','1','2','3', '4', '5', '6', '7','8','9','10','11','12','13','14','15']
    #cpu_list = ['0', '1']
    if sequence_use_8:
        cpu_list = ['0','1','2','3','4','5','6','7']
        config_threadpool(-3, 8, cpu_list)
    else:
        cpu_list = ['0','1','2','3','4','5','6','7','8','9','10','11','12','13','14','15']
        config_threadpool(-3, 16, cpu_list)

    m = graph_executor.GraphModule(lib["default"](dev))
    t1 = time.time()
    for i in range(0, loop):
        m.set_input(name, x)
        m.run()
        tvm_output = m.get_output(0)
    t2 = time.time()
    print("{} run time is {}".format(t2 - t1,"remote" if remote_do else "local"))
    return tvm_output

def normal_test(func, mods, x):
    dtype = "float32"
    '''
    tvm_output = local_run(mods[0], 'data', tvm.nd.array(x.astype(dtype)))
    tvm_output = local_run(mods[1], split_info['input_name'], tvm_output)

    tvm_output = local_run(mods[0], 'data', tvm.nd.array(x.astype(dtype)))
    tvm_output = local_run(mods[1], split_info['input_name'], tvm_output, True)
    top1 = np.argmax(tvm_output.numpy()[0])
    print("TVM Pipeline Graph prediction top-1:", top1, synset[top1])
    '''
    tvm_output = local_run(func, 'data', x)
    #tvm_output = local_run(func, 'data', x, True)
    top1 = np.argmax(tvm_output.numpy()[0])
    print("TVM Single Full Graph prediction top-1:", top1, synset[top1])


x, synset = get_image()
mod, mods, params = get_network(x)
print("x", x.shape)
if do_pipeline_runtime:
    pipe_test(mods, x)
else:
    normal_test(mod, mods, x)
