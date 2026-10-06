#!/usr/bin/env python3
"""Run offline P25 regressions against a completed Unix Makefiles CMake build.

Configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON, build TR, then run:
  python3 tests/run_p25_vocoder_test.py /path/to/build
No SDR, production config, or network peer is used.
"""
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

build = Path(sys.argv[1]).resolve()
commands = json.loads((build / "compile_commands.json").read_text())
main = next(c for c in commands if c["file"].endswith("trunk-recorder/main.cc"))
source = Path(main["file"]).parents[1]
test = Path(__file__).with_name("p25_vocoder_test.cc").resolve()
with tempfile.TemporaryDirectory(prefix="tr-p25-vocoder-") as temporary:
    work = Path(temporary)
    obj = work / "test.o"
    binary = work / "test"
    compile_args = shlex.split(main["command"])
    compile_args[compile_args.index("-o") + 1] = str(obj)
    compile_args[compile_args.index("-c") + 1] = str(test)
    # Access internal state only in this harness; do not change production visibility.
    compile_args += ["-UNDEBUG", "-fno-access-control", "-I" + str(source)]
    subprocess.run(compile_args, cwd=main["directory"], check=True)

    link = shlex.split((build / "CMakeFiles/trunk-recorder.dir/link.txt").read_text())
    link[link.index("-o") + 1] = str(binary)
    main_obj = next(a for a in link if a.endswith("main.cc.o"))
    link[link.index(main_obj)] = str(obj)
    # Internal OP25 methods are hidden in the shared library. Link the same built
    # objects into this test so we exercise real decoder code, not a copied model.
    op25_dir = build / "lib/op25_repeater/lib"
    op25_link = shlex.split((op25_dir / "CMakeFiles/gnuradio-op25_repeater.dir/link.txt").read_text())
    decoder_inputs = [str((op25_dir / a).resolve()) for a in op25_link if a.endswith((".o", ".a"))]
    library = next(a for a in link if a.endswith("libgnuradio-op25_repeater.so"))
    index = link.index(library)
    link[index:index + 1] = decoder_inputs
    subprocess.run(link, cwd=build, check=True)
    subprocess.run([str(binary), str(work)], cwd=work, check=True)
