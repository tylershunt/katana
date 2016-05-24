from gg.ast import *
from gg.lib.graph import Graph
from gg.lib.wl import Worklist
from gg.ast.params import GraphParam
import cgen
G = Graph("graph")
WL = Worklist()
ast = Module([
CBlock([cgen.Include("kernels/reduce.cuh", system = False)], parse = False),
CBlock([cgen.Include("gen_cuda.cuh", system = False)], parse = False),
CDeclGlobal([("unsigned int *", "P_DIST_CURRENT", "")]),
Kernel("InitializeGraph", [G.param(), ('int ', 'nowned') , ('unsigned int', 'local_infinity'), ('unsigned int', 'local_src_node'), ('unsigned int *', 'p_dist_current')],
[
ForAll("src", G.nodes(None, "nowned"),
[
CBlock(["p_dist_current[src] = (graph.node_data[src] == local_src_node) ? 0 : local_infinity"]),
]),
]),
Kernel("SSSP", [G.param(), ('int ', 'nowned') , ('unsigned int *', 'p_dist_current'), ('Any', 'any_retval')],
[
ForAll("src", G.nodes(None, "nowned"),
[
CDecl([("unsigned int", "current_min", "")]),
CBlock(["current_min = p_dist_current[src]"]),
ClosureHint(
ForAll("jj", G.edges("src"),
[
CDecl([("index_type", "dst", "")]),
CBlock(["dst = graph.getAbsDestination(jj)"]),
CDecl([("unsigned int", "new_dist", "")]),
CBlock(["new_dist = p_dist_current[dst] + graph.getAbsWeight(jj)"]),
If("current_min > new_dist",
[
CBlock(["current_min = new_dist"]),
]),
]),
),
If("p_dist_current[src] > current_min",
[
CBlock(["p_dist_current[src] = current_min"]),
CBlock(["any_retval.return_( 1)"]),
]),
]),
]),
Kernel("InitializeGraph_cuda", [('unsigned int', 'local_src_node'), ('unsigned int', 'local_infinity'), ('struct CUDA_Context *', 'ctx')],
[
CDecl([("dim3", "blocks", "")]),
CDecl([("dim3", "threads", "")]),
CBlock(["kernel_sizing(ctx->gg, blocks, threads)"]),
Invoke("InitializeGraph", ("ctx->gg", "ctx->nowned", "local_infinity", "local_src_node", "ctx->dist_current.gpu_wr_ptr()")),
CBlock(["check_cuda_kernel"], parse = False),
], host = True),
Kernel("SSSP_cuda", [('int &', '__retval'), ('struct CUDA_Context *', 'ctx')],
[
CDecl([("dim3", "blocks", "")]),
CDecl([("dim3", "threads", "")]),
CBlock(["kernel_sizing(ctx->gg, blocks, threads)"]),
CBlock(["*(ctx->p_retval.cpu_wr_ptr()) = __retval"]),
CBlock(["ctx->any_retval.rv = ctx->p_retval.gpu_wr_ptr()"]),
Invoke("SSSP", ("ctx->gg", "ctx->nowned", "ctx->dist_current.gpu_wr_ptr()", "ctx->any_retval")),
CBlock(["check_cuda_kernel"], parse = False),
CBlock(["__retval = *(ctx->p_retval.cpu_rd_ptr())"]),
], host = True),
])
