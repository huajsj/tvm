import tvm
import subprocess
from tvm import te
from tvm.runtime.module import load_module as _load_module
from tvm.contrib import cc as _cc, tar as _tar, utils as _utils, clang

def load_module_helper(path):
    # High level handling for .o and .tar file.
    # We support this to be consistent with RPC module load.
    if path.endswith(".o"):
        # Extra dependencies during runtime.
        from tvm.contrib import cc as _cc

        _cc.create_shared(path + ".so", path)
        path += ".so"
    elif path.endswith(".tar"):
        # Extra dependencies during runtime.
        from tvm.contrib import cc as _cc, utils as _utils, tar as _tar

        tar_temp = _utils.tempdir(custom_path=path.replace(".tar", ""))
        _tar.untar(path, tar_temp.temp_dir)
        files = [tar_temp.relpath(x) for x in tar_temp.listdir()]
        _cc.create_shared(path + ".so", files)
        path += ".so"
    # Redirect to the load API
    return path

def create_shared(output, objects, options=None):
    cl = clang.find_clang(False)
    compiler = "/scratch/llvm/bin/clang"
    cmd = [compiler]
    cmd += ["-o", output]

    if isinstance(objects, str):
        cmd += [objects]
    else:
        cmd += objects

    options = options if options else ["-shared", "-fPIC", "-lm", "-target", "aarch64-linux-gnu"]
    cmd += options

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out, _) = proc.communicate()

    if proc.returncode != 0:
        msg = "Compilation error:\n"
        msg += py_str(out)
        raise RuntimeError(msg)
    return


target = "llvm -mtriple=aarch64-linux-gnu"
n = tvm.runtime.convert(1024)
a = te.placeholder((n,), name = "a")
b = te.compute((n,), lambda i: a[0] + 1.0, name = "b")
s = te.create_schedule(b.op)
lib = tvm.build(s, [a, b], target = target, name = "add")
#lib.export_library("./test.tar")
specialized_create = _cc.cross_compiler("/scratch/llvm/bin/clang",["-target", "aarch64-linux-gnu")
lib.export_library("./test2.so", fcompile=specialized_create)#create_shared)
#lib.export_library("./test2.so", fcompile=create_shared)
#m = _load_module("./lib0.o")

