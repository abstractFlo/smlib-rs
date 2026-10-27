"""The writable globals of SMLib and SM_API, against the list of those known to be safe.

SMLib runs in several threads of one process only while every global it writes is per thread
(thread_local) or written once before the threads start. A new writable global, from an NVIDIA
merge or a change of ours, fails this check until it is either made thread_local or added to
allowed.txt with the reason it is safe (names there may use * and ?).

    python3 cadler/threads/writable_globals.py <lib dir>            # check
    python3 cadler/threads/writable_globals.py <lib dir> --list     # print what the libraries have

Reads `objdump -t -C`: objects in .data and .bss (thread_local ones are in .tdata/.tbss, and
constants in .rodata/.data.rel.ro, so neither shows up). Linux builds only.
"""

from __future__ import annotations

import fnmatch
import re
import subprocess
import sys
from pathlib import Path

LIBRARIES = ["libSMLib.so", "libSM_API.so"]
WRITABLE_SECTIONS = {".data", ".bss"}
ALLOWED = Path(__file__).with_name("allowed.txt")

# objdump -t: address, 7 flag characters, section, size, name (the flags can contain spaces)
SYMBOL_LINE = re.compile(r"^[0-9a-f]+ (.{7}) (\S+)\s+[0-9a-f]+\s+(?:\.hidden\s+)?(.+)$")
# compiler and runtime objects, not program state (std::__ioinit: <iostream>'s init object, gcc < 13;
# __aarch64_have_lse_atomics: libgcc's atomics switch, set once when the library loads)
IGNORED = re.compile(r"^(DW\.ref\.|guard variable for |__dso_handle$|_GLOBAL_OFFSET_TABLE_$|__TMC_END__$|completed\.\d+$|std::__ioinit$|__aarch64_have_lse_atomics$|typeinfo |vtable |VTT )")


def writable_globals(library: Path) -> set[str]:
    listing = subprocess.run(["objdump", "-t", "-C", str(library)], capture_output=True, text=True, check=True).stdout
    names = set()
    for line in listing.splitlines():
        match = SYMBOL_LINE.match(line)
        if not match:
            continue
        flags, section, name = match.groups()
        if "O" in flags and section in WRITABLE_SECTIONS and not IGNORED.match(name):
            names.add(f"{library.name}: {name.strip()}")
    return names


def allowed() -> set[str]:
    lines = ALLOWED.read_text(encoding="utf-8").splitlines()
    return {line.split("  #", 1)[0].strip() for line in lines if line.strip() and not line.startswith("#")}


def main(arguments: list[str]) -> int:
    directory = Path(arguments[0])
    found = set().union(*(writable_globals(directory / library) for library in LIBRARIES))
    if "--list" in arguments:
        print("\n".join(sorted(found)))
        return 0
    patterns = allowed()
    new = sorted(name for name in found if not any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns))
    gone = sorted(pattern for pattern in patterns if not any(fnmatch.fnmatchcase(name, pattern) for name in found))
    for name in new:
        print(f"new writable global: {name}")
    for pattern in gone:
        print(f"no longer in the libraries (remove it from allowed.txt): {pattern}")
    if not new and not gone:
        print(f"{len(found)} writable globals, all known to be safe for threads")
    return 1 if new or gone else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
