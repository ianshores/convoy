#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Version-pinned hvcc extension. Registration lives only in this process.

This uses private hvcc APIs deliberately: fail closed on untested versions.
The installed compiler is never edited. IRs are frozen from saved Pd arrays.
"""
import argparse
import importlib.metadata
import json
import math
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
TYPE = "__heavyverb~f"


def register():
    if importlib.metadata.version("hvcc") != "0.17.2":
        raise RuntimeError("This adapter requires hvcc==0.17.2")
    from hvcc.types.IR import IRNode
    from hvcc.interpreters.pd2hv.HeavyObject import HeavyObject as PdHeavyObject
    from hvcc.core.hv2ir.HeavyIrObject import HeavyIrObject
    from hvcc.core.hv2ir.HeavyParser import LANG_CLASS_DICT
    from hvcc.generators.ir2c.HeavyObject import HeavyObject as CObject
    from hvcc.generators.ir2c.ir2c import ir2c
    definition = IRNode.model_validate({
        "inlets": ["~f>", "-->"], "outlets": ["~f>"],
        "ir": {"control": True, "signal": True, "init": True},
        "args": [
            {"name": "table", "value_type": "string", "default": "", "required": True},
            {"name": "partition", "value_type": "int", "default": 256, "required": False},
            {"name": "channel", "value_type": "int", "default": 0, "required": False}],
        "perf": {"avx": 0, "sse": 0}})
    PdHeavyObject._HeavyObject__HEAVY_IR_OBJS[TYPE] = definition
    HeavyIrObject._HeavyIrObject__HEAVY_OBJS_IR_DICT[TYPE] = definition

    class VerbIR(HeavyIrObject):
        def __init__(self, obj_type, args, graph, annotations=None):
            super().__init__(obj_type, args, graph, annotations=annotations)

        def reduce(self):
            table = self.graph.resolve_object_for_name(self.args["table"], ["table", "__table"])
            b = self.args["partition"]
            if self.args["channel"] not in (0, 1):
                self.add_error("heavyverb~ channel must be 0 or 1")
                return ({self}, [])
            if b < 16 or b > 8192 or b & (b - 1):
                self.add_error("heavyverb~ partition must be a power of two from 16 to 8192")
                return ({self}, [])
            values = table.args.get("values", []) if table else []
            if not values or not all(math.isfinite(float(v)) and abs(float(v)) <= 3.4028234663852886e38 for v in values):
                self.add_error("heavyverb~ requires a nonempty saved finite IR array (not soundfiler/loadbang)")
            elif len(values) > 16777216:
                self.add_error("heavyverb~ IR exceeds maximum length")
            else:
                self.args["coefficients"] = list(values)
            return ({self}, [])

    class VerbC(CObject):
        c_struct = "std::unique_ptr<heavyverb::Convolver>"
        preamble = "hvVerb"

        @classmethod
        def get_C_header_set(cls):
            return {"heavyverb_ir_override.hpp"}

        @classmethod
        def get_C_init(cls, obj_type, obj_id, args):
            values = ", ".join(float(v).hex() + "f" for v in args["coefficients"])
            return [f"([&]() -> hv_size_t {{ static const float ir[] = {{{values}}}; "
                    f"hvVerb_{obj_id}.reset(heavyverb::make_convolver(ir, {len(args['coefficients'])}, {args['partition']}, {args['channel']})); return static_cast<hv_size_t>(hvVerb_{obj_id}->memory_bytes()); }})();"]

        @classmethod
        def get_C_free(cls, obj_type, obj_id, args):
            return [f"hvVerb_{obj_id}.reset();"]

        @classmethod
        def get_C_onMessage(cls, obj_type, obj_id, inlet_index, args):
            if inlet_index != 1:
                return []
            return [f"if (msg_isFloat(m, 0)) Context(_c)->hvVerb_{obj_id}->set_tail_cutoff_db(msg_getFloat(m, 0));"]

        @classmethod
        def get_C_process(cls, process_dict, obj_type, obj_id, args):
            source = cls._c_buffer(process_dict.inputBuffers[0])
            dest = cls._c_buffer(process_dict.outputBuffers[0])
            return ["{", "static_assert(HV_N_SIMD == 1, \"heavyverb export requires scalar Heavy\");",
                    f"const float in = {source}; float out;",
                    f"hvVerb_{obj_id}->process(&in, &out, 1);", f"{dest} = out;", "}"]

    LANG_CLASS_DICT[TYPE] = VerbIR
    ir2c._ir2c__OBJECT_CLASS_DICT[TYPE] = VerbC
    # hvcc's static cycle estimator cannot model a variable length FFT FIR.
    # Exclude this operator from that estimate, and record an explicit warning.
    from hvcc.generators.ir2c.ir2c_perf import ir2c_perf
    original_perf = ir2c_perf.perf

    def perf(cls, ir, **kwargs):
        estimate = ir.model_copy(deep=True)
        estimate.signal.processOrder = [op for op in estimate.signal.processOrder if ir.objects[op.id].type != TYPE]
        return original_perf(estimate, **kwargs)
    ir2c_perf.perf = classmethod(perf)
    original = ir2c.compile

    def compile_with_header(cls, *args, **kwargs):
        result = original(*args, **kwargs)
        if not result.notifs.has_error:
            output = Path(kwargs["output_dir"])
            for header in (ROOT / "include").glob("*.hpp"):
                shutil.copy2(header, output / header.name)
            shutil.copy2(ROOT / "LICENSE", output / "HEAVYVERB-LICENSE.txt")
        return result
    ir2c.compile = classmethod(compile_with_header)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("patch", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--name", default="HeavyVerbDemo")
    parser.add_argument("--generators", nargs="+", default=["c"], choices=["c", "dpf", "wwise"])
    parser.add_argument("-p", "--search-path", action="append", type=Path, default=[])
    args = parser.parse_args()
    register()
    from hvcc.compiler import compile_dataflow
    args.output.mkdir(parents=True, exist_ok=True)
    # Clean obsolete generated FFT files when reusing an older export directory.
    obsolete = {"heavyverb_pffft.hpp", "pffft.c", "pffft_common.c", "pffft.h",
                "pffft_priv_impl.h", "PFFFT-LICENSE.txt", "PFFFT-UPSTREAM.md", "PFFFT-manifest.json",
                "pf_altivec_float.h", "pf_float.h", "pf_fma_x86.h", "pf_neon_float.h",
                "pf_scalar_float.h", "pf_sse1_float.h", "pf_wasm_float.h"}
    for path in args.output.rglob("*"):
        if path.is_file() and path.name in obsolete:
            path.unlink()
    # Remove our optional demo host before generating a fresh generic wrapper.
    for name in ("HeavyVerbPlugin.cpp", "HeavyVerbUI.cpp", "graph_runtime.hpp", "wav_loader.hpp"):
        (args.output / "plugin/source" / name).unlink(missing_ok=True)
    meta = args.output / "metadata.json"
    meta.write_text(json.dumps({"nosimd": True, "dpf": {"maker": "Ian Shores", "license": "MIT (heavyverb); see component notices", "homepage": "https://github.com/ianshores/heavyverb", "plugin_formats": ["clap", "vst3"], "plugin_uri": "https://github.com/ianshores/heavyverb", "plugin_clap_id": "org.heavyverb.demo"}}))
    results = compile_dataflow(in_path=args.patch.resolve(), out_dir=args.output.resolve(),
        patch_name=args.name, patch_meta_file=meta.resolve(),
        search_paths=[ROOT / "pd/export", *args.search_path], generators=args.generators)
    (args.output / "results.json").write_text(json.dumps(results.model_dump(), default=str, indent=2))
    failed = False
    for result in results.root.values():
        for warning in result.notifs.warnings:
            print(f"WARNING {result.stage}: {warning.message}")
        if result.notifs.has_error:
            failed = True
            for error in result.notifs.errors:
                print(f"ERROR {result.stage}: {error.message}", file=sys.stderr)
            if result.notifs.exception:
                print(result.notifs.exception, file=sys.stderr)
    if not failed:
        from patch_heavy_runtime import patch_light_pipe
        for pipe in args.output.rglob("HvLightPipe.c"):
            patch_light_pipe(pipe)
    makefile = args.output / "plugin/source/Makefile"
    if "dpf" in args.generators and not failed and makefile.exists():
        with makefile.open("a") as stream:
            stream.write("\n# heavyverb requires C++17\nBUILD_CXX_FLAGS += -std=c++17\n")
    print("Convolution CPU is excluded from hvcc static estimates; run the benchmark.")
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
