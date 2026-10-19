#!/usr/bin/env python3
"""The interchange corpus (ROADMAP Phase 3): AAF files and Media Composer bins
made by other programs, for the readers in src/interop to be tested on.

The files belong to pyaaf2 and pyavb (both MIT, by Mark Reid) and come from
their repositories at pinned commits, checked by SHA-256. They are not
committed here: `fetch` downloads them into a cache folder.

    interop-corpus.py fetch [--dir DIR]
        Download the files (default DIR: $CUTTHULHU_INTEROP_CORPUS, else the
        user's cache folder). Exits 77 when they cannot be downloaded.
    interop-corpus.py manifest [--dir DIR]
        Rewrite tests/data/interop_corpus.json: for each AAF file, how many
        storages and streams olefile (another compound file reader) finds,
        and a digest of its listing (every path, and each stream's size and
        SHA-256; listing_digest). tst_cfb compares its own reading with this.
        Also a digest of each one's dump as pyaaf2 reads it
        (scripts/aaf-dump.py --canonical), which tst_aaf compares with its
        own. Needs olefile and pyaaf2.
    interop-corpus.py check-aaf --app build/ci/bin/cutthulhu [--dir DIR]
        Compare `cutthulhu --dump-aaf` of every AAF file with pyaaf2's
        reading (scripts/aaf-dump.py --compare). Exits 77 without the corpus
        or pyaaf2 (pip install pyaaf2).
    interop-corpus.py check-written --test build/ci/bin/tst_cfb [--dir DIR]
        Run tst_cfb's forOlefile, which writes compound files with
        Cutthulhu's writer, and read them back with olefile. Exits 77 without
        olefile (pip install olefile).
"""

import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "tests", "data", "interop_corpus.json")

SOURCES = {
    "pyaaf2": ("https://raw.githubusercontent.com/markreidvfx/pyaaf2/08dcc3dfe823ea5781db1cb657d9c2606557fcdc/tests/test_files/", {
        "empty.aaf": "0a83da0fc7e2de89391c101c4b668dac6f857fbd721504e6d8f6b408b6e84a05",
        "sector_size_512.aaf": "e4991b3e1272a8d23efb16a6fa11d560184e4024ddee41cbe32277345ce9af3b",
        "test_file_01.aaf": "be513199eb84f554421da2523b202183a43c8a68e9703f9d0e8541f6e831b9e9",
        "retimes/bezier01.aaf": "d3bad62742fc46a8d59e142d29971c736ea7258c233667aebe965a66fd2fcc90",
        "retimes/bezier02.aaf": "bc203fc62dd31f5082508b78bd441b41924805afaaec5624d2b208f61c846fd5",
        "retimes/bezier03.aaf": "3391496abedfc701528a45162eda26ec975edfd0bbce660f929100b8bf6bf94e",
        "retimes/linear01.aaf": "36f411847256e0964df4297dc78558554867a4653c5468e86634aba88dff7f37",
        "retimes/linear02.aaf": "053d05d3349d40a97387b3452bc63f06700365ad8c7a7372db2132f674155ec0",
        "retimes/linear03.aaf": "544b31bbf474ab99372d46218d38d6e5d96199e5aee192da60636e5894e3ff7b",
        "retimes/spline01.aaf": "50851c8e133f9062ae3c3a29e0bbc4766c1f5e54a8fd33c1006c7d74845eae83",
        "retimes/spline02.aaf": "c8c584979c84ce971a019d6c9accf2ff983d432b6aea2535b99abc0c6a9b7a16",
        "retimes/spline03.aaf": "dcd2931856c0f98ca13433dfe8f99036e85340a38a740e968104f691e1a65150",
        "retimes/step01.aaf": "00b9464c710c15fef3b284c7c4cd4807b9890b2aa4b9f4091b8d6c5f7c363575",
        "retimes/step02.aaf": "a86afbd3c6679c3e943aa88e70ee120fe364958902f5ff69fceda1b51bba7605",
        "retimes/step03.aaf": "8d5c9e9770e3d879ffae6ef4d78e7328e8d358486ec1dc8893c661a5b528221b",
    }),
    "pyavb": ("https://raw.githubusercontent.com/markreidvfx/pyavb/ff64e06b60077ecabe5db3fef239898b08363898/tests/test_files/", {
        "test_file_01.avb": "f58ecf7e95f85df46909dff423193e8b7de54fe18b909a61cfe844fe58262606",
        "retimes/bezier01.avb": "f4c2ff95e01d14bb01f360c137f2207f68c86cb3be9ee4146bd0cb0d73657f9e",
        "retimes/bezier02.avb": "f7f07818bd6631a314c936186a29cbc52071790f8b8fe2b70beaa741a24c1374",
        "retimes/bezier03.avb": "cf3dfc73aaba6973e32a076600c71d2ab7a9061b744dc069c8269f1a547a61d4",
        "retimes/linear01.avb": "6c7a1982c4196d43d205a9145fa78d1cea4a125d537a5c213464ea3c6b8de0a9",
        "retimes/linear02.avb": "86fecfe49fe826cf5baca2780778806263306abb0ca918abb427dc513d98a34f",
        "retimes/linear03.avb": "86fe9b9ae5679db0bf3f940e412da2af6ae9b7b488898cbd6d842d635f31ea5f",
        "retimes/spline01.avb": "5dff0e9faf6d35ba41e8b0b15b7f3af69a4884ee5c8554558a9b3b23e28defac",
        "retimes/spline02.avb": "fe8183d266020dfa47e4cdd421dc9a5e83d960307467de2aea97222d99321386",
        "retimes/spline03.avb": "70c53d53dc9f4a30e8ea4f3a221ff0ad7c2014ebb32f7da4301f99c6c1cd096f",
        "retimes/spline04.avb": "4b421c65092c1da1b8de5cebf34ba30374038b48e95050b312f0c464751d008d",
        "retimes/step01.avb": "8f3080dee606242ea157d8ee162965cee9d7d45fbc2944150c0a90d549dd918c",
        "retimes/step02.avb": "9a8ffb81b387797da1d04db94a5c2c37aee0a14b319a1f400aeef0766e1644e8",
        "retimes/step03.avb": "9f89928379677fc305234146c3f2f9f8bb635a28d10c08612046ca7433a33865",
    }),
}


def default_dir():
    folder = os.environ.get("CUTTHULHU_INTEROP_CORPUS")
    if folder:
        return folder
    cache = os.environ.get("LOCALAPPDATA") if os.name == "nt" else None
    if not cache:
        cache = os.path.join(os.path.expanduser("~"), "Library", "Caches") if sys.platform == "darwin" \
            else os.environ.get("XDG_CACHE_HOME", os.path.join(os.path.expanduser("~"), ".cache"))
    return os.path.join(cache, "cutthulhu", "interop-corpus")


def files():
    """(name in the corpus, URL, SHA-256)."""
    for source, (base, entries) in SOURCES.items():
        for name, digest in entries.items():
            yield f"{source}/{name}", base + name, digest


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def fetch(folder):
    fetched = 0
    for name, url, digest in files():
        path = os.path.join(folder, name)
        if os.path.exists(path) and sha256(path) == digest:
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read()
        except OSError as e:
            print(f"interop-corpus: cannot download {url}: {e}", file=sys.stderr)
            return 77
        if hashlib.sha256(data).hexdigest() != digest:
            print(f"interop-corpus: {url} is not the file expected (SHA-256)", file=sys.stderr)
            return 1
        with open(path, "wb") as f:
            f.write(data)
        fetched += 1
    print(f"interop-corpus: {sum(1 for _ in files())} files in {folder} ({fetched} downloaded)")
    return 0


def olefile_or_skip():
    try:
        import olefile  # noqa: F401
    except ImportError:
        print("interop-corpus: olefile is not installed (pip install olefile)", file=sys.stderr)
        sys.exit(77)
    return sys.modules["olefile"]


def listing(olefile, path):
    """Every storage and stream: {"/a/b": {"type", "size", "sha256"}}."""
    out = {}
    with olefile.OleFileIO(path) as ole:
        for parts in ole.listdir(streams=True, storages=True):
            key = "/" + "/".join(parts)
            if ole.get_type(parts) == olefile.STGTY_STREAM:
                data = ole.openstream(parts).read()
                out[key] = {"type": "stream", "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            else:
                out[key] = {"type": "storage"}
    return out


def listing_digest(entries):
    """A SHA-256 of a listing: its lines "path<TAB>storage" and
    "path<TAB>stream<TAB>size<TAB>sha256", sorted, joined by newlines."""
    lines = []
    for key, e in entries.items():
        if e["type"] == "stream":
            lines.append(f"{key}\tstream\t{e['size']}\t{e['sha256']}")
        else:
            lines.append(f"{key}\tstorage")
    return hashlib.sha256("\n".join(sorted(lines)).encode("utf-8")).hexdigest()


def manifest(folder):
    olefile = olefile_or_skip()
    cfb = {}
    for name, _, _ in files():
        if name.endswith(".aaf"):
            entries = listing(olefile, os.path.join(folder, name))
            streams = sum(1 for e in entries.values() if e["type"] == "stream")
            cfb[name] = {"storages": len(entries) - streams, "streams": streams, "listing": listing_digest(entries)}
    # The AAF files as pyaaf2 reads them (scripts/aaf-dump.py), for tst_aaf.
    spec = importlib.util.spec_from_file_location("aaf_dump", os.path.join(ROOT, "scripts", "aaf-dump.py"))
    aaf_dump = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(aaf_dump)
    aaf = {}
    for name in cfb:
        text = aaf_dump.canonical(aaf_dump.dump(os.path.join(folder, name)))
        aaf[name] = {"dump": hashlib.sha256(text.encode("utf-8")).hexdigest(), "objects": text.count('"class":')}
    import aaf2
    doc = {"comment": f"Made by scripts/interop-corpus.py manifest with olefile {olefile.__version__} "
                      f"and pyaaf2 {aaf2.__version__}", "cfb": cfb, "aaf": aaf}
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1, sort_keys=True)
        f.write("\n")
    print(f"interop-corpus: wrote {MANIFEST}")
    return 0


def check_aaf(app, folder):
    paths = [os.path.join(folder, name) for name, _, _ in files() if name.endswith(".aaf")]
    paths = [p for p in paths if os.path.exists(p)]
    if not paths:
        print(f"interop-corpus: no corpus in {folder} (interop-corpus.py fetch)", file=sys.stderr)
        return 77
    script = os.path.join(ROOT, "scripts", "aaf-dump.py")
    return subprocess.run([sys.executable, script, "--compare", "--app", app] + paths).returncode


def check_written(test, folder):
    olefile = olefile_or_skip()
    with tempfile.TemporaryDirectory() as tmp:
        out = folder or tmp
        env = dict(os.environ, CUTTHULHU_CFB_OUT=out, QT_QPA_PLATFORM="offscreen")
        run = subprocess.run([test, "forOlefile"], env=env, capture_output=True, text=True)
        if run.returncode != 0:
            print(run.stdout + run.stderr, file=sys.stderr)
            return 1
        with open(os.path.join(out, "written.json"), encoding="utf-8") as f:
            expected = json.load(f)["cfb"]
        problems = []
        for name, want in expected.items():
            path = os.path.join(out, name)
            if not olefile.isOleFile(path):
                problems.append(f"{name}: olefile does not take it for a compound file")
                continue
            got = listing(olefile, path)
            for key in sorted(want["entries"].keys() | got.keys()):
                if want["entries"].get(key) != got.get(key):
                    problems.append(f"{name}: {key}: wrote {want['entries'].get(key)}, olefile read {got.get(key)}")
        for line in problems:
            print(line)
        if problems:
            return 1
        print(f"interop-corpus: olefile reads the {len(expected)} files Cutthulhu wrote")
        return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["fetch", "manifest", "check-aaf", "check-written"])
    ap.add_argument("--dir", help="the corpus folder (check-written: where to keep the files written)")
    ap.add_argument("--test", help="tst_cfb, for check-written")
    ap.add_argument("--app", help="the cutthulhu executable, for check-aaf")
    args = ap.parse_args()
    if args.command == "fetch":
        return fetch(args.dir or default_dir())
    if args.command == "manifest":
        return manifest(args.dir or default_dir())
    if args.command == "check-aaf":
        if not args.app:
            ap.error("check-aaf needs --app")
        return check_aaf(args.app, args.dir or default_dir())
    if not args.test:
        ap.error("check-written needs --test")
    return check_written(args.test, args.dir)


if __name__ == "__main__":
    sys.exit(main())
