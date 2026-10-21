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
        own. And a digest of each bin's dump as pyavb reads it
        (scripts/avb-dump.py --canonical), for tst_avb. Needs olefile, pyaaf2
        and pyavb.
    interop-corpus.py check-aaf --app build/ci/bin/cutthulhu [--dir DIR]
        Compare `cutthulhu --dump-aaf` of every AAF file with pyaaf2's
        reading (scripts/aaf-dump.py --compare). Exits 77 without the corpus
        or pyaaf2 (pip install pyaaf2).
    interop-corpus.py check-avb --app build/ci/bin/cutthulhu [--dir DIR]
        Compare `cutthulhu --dump-avb` of every bin with pyavb's reading
        (scripts/avb-dump.py --compare), byte for byte. Exits 77 without the
        corpus or pyavb (pip install pyavb).
    interop-corpus.py reference [--dir DIR]
        Rewrite tests/data/aaf_reference/: the structure dump of each
        Media Composer bin that has an AAF export of the same material, as
        pyavb reads the bin (scripts/avb-structure.py). Needs pyavb.
    interop-corpus.py check-import --app build/ci/bin/cutthulhu [--dir DIR]
        Import each of those AAF files (`cutthulhu --import-aaf`) and compare
        the result with the bin's reference dump (structdiff.py
        --no-effects): Phase 3 gate 1 on the corpus. Exits 77 without the
        corpus.
    interop-corpus.py check-avb-import --app build/ci/bin/cutthulhu [--dir DIR]
        Read each of those Media Composer bins (`cutthulhu --import-avb`) and
        compare the result with its reference dump (structdiff.py
        --no-effects): the bin reads as pyavb reads it. Exits 77 without the
        corpus.
    interop-corpus.py check-round-trip --app build/ci/bin/cutthulhu [--dir DIR]
        Import each AAF file, export it (`cutthulhu --export-aaf`) and import
        the export: the two imports must be the same (structdiff.py). With
        pyaaf2 installed, pyaaf2 must also read each export as Cutthulhu does
        (aaf-dump.py --compare). Exits 77 without the corpus.
    interop-corpus.py check-avb-rewrite --app build/ci/bin/cutthulhu [--dir DIR]
        Read and write back every bin (`cutthulhu --rewrite-avb`): each must
        be the same file, byte for byte (docs/spec/avb.md, "Writing").
        Exits 77 without the corpus.
    interop-corpus.py check-avb-write --test build/ci/bin/tst_avb [--dir DIR]
        Run tst_avb's forPyavb, which writes a bin built from scratch by
        Cutthulhu's writer (little- and big-endian), a bin exported from the
        model (avb_export, both orders) and every corpus bin rewritten
        big-endian, and read them with pyavb: the bin from scratch must dump
        as Cutthulhu reads it and hold its clip, the exported bin dump the
        same and read (avb-structure.py) as Cutthulhu reads it back, the
        big-endian bins as pyavb reads the originals. Exits 77 without pyavb or the
        corpus.
    interop-corpus.py check-avb-export --app build/ci/bin/cutthulhu [--dir DIR]
        Read each Media Composer bin that has a reference dump
        (`cutthulhu --import-avb`), write what it reads as a bin
        (`cutthulhu --export-avb`) and read that again: the two readings must
        be the same (structdiff.py --fields --sources, effects included).
        Exits 77 without the corpus.
    interop-corpus.py check-avb-export-pyavb --app build/ci/bin/cutthulhu [--dir DIR]
        Write the same bins (`cutthulhu --export-avb`) and read them with
        pyavb: it must read every object as Cutthulhu reads it
        (avb-dump.py --compare), and the bin it reads
        (scripts/avb-structure.py) must be the one exported, as Cutthulhu
        read it from the corpus (structdiff.py --no-effects: avb-structure.py
        does not name effects). Exits 77 without pyavb or the corpus.
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
REFERENCE = os.path.join(ROOT, "tests", "data", "aaf_reference")

# The same material twice from Media Composer: a bin and an AAF export of it.
RETIMES = ["bezier01", "bezier02", "bezier03", "linear01", "linear02", "linear03",
           "spline01", "spline02", "spline03", "step01", "step02", "step03"]
PAIRS = [("pyavb/test_file_01.avb", "pyaaf2/test_file_01.aaf")] + \
        [(f"pyavb/retimes/{n}.avb", f"pyaaf2/retimes/{n}.aaf") for n in RETIMES]

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


def load_script(name):
    """A script of this folder as a module (its file name has a hyphen)."""
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), os.path.join(ROOT, "scripts", name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def manifest(folder):
    olefile = olefile_or_skip()
    try:
        import avb  # noqa: F401
        import aaf2  # noqa: F401
    except ImportError:
        print("interop-corpus: manifest needs pyaaf2 and pyavb (pip install pyaaf2 pyavb)", file=sys.stderr)
        return 77
    cfb = {}
    for name, _, _ in files():
        if name.endswith(".aaf"):
            entries = listing(olefile, os.path.join(folder, name))
            streams = sum(1 for e in entries.values() if e["type"] == "stream")
            cfb[name] = {"storages": len(entries) - streams, "streams": streams, "listing": listing_digest(entries)}
    # The AAF files as pyaaf2 reads them (scripts/aaf-dump.py), for tst_aaf.
    aaf_dump = load_script("aaf-dump")
    aaf = {}
    for name in cfb:
        text = aaf_dump.canonical(aaf_dump.dump(os.path.join(folder, name)))
        aaf[name] = {"dump": hashlib.sha256(text.encode("utf-8")).hexdigest(), "objects": text.count('"class":')}
    # The bins as pyavb reads them (scripts/avb-dump.py), for tst_avb.
    avb_dump = load_script("avb-dump")
    bins = {}
    for name, _, _ in files():
        if name.endswith(".avb"):
            text = avb_dump.canonical(avb_dump.dump(os.path.join(folder, name)))
            bins[name] = {"dump": hashlib.sha256(text.encode("utf-8")).hexdigest(), "objects": text.count('"class":')}
    doc = {"comment": f"Made by scripts/interop-corpus.py manifest with olefile {olefile.__version__}, "
                      f"pyaaf2 {aaf2.__version__} and pyavb {avb.__version__}", "cfb": cfb, "aaf": aaf, "avb": bins}
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1, sort_keys=True)
        f.write("\n")
    print(f"interop-corpus: wrote {MANIFEST}")
    return 0


def reference(folder):
    script = os.path.join(ROOT, "scripts", "avb-structure.py")
    os.makedirs(REFERENCE, exist_ok=True)
    for avb, aaf in PAIRS:
        run = subprocess.run([sys.executable, script, os.path.join(folder, avb)], capture_output=True, text=True)
        if run.returncode != 0:
            print(run.stderr, file=sys.stderr)
            return run.returncode
        path = os.path.join(REFERENCE, os.path.splitext(os.path.basename(aaf))[0] + ".json")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(run.stdout)
    print(f"interop-corpus: wrote {len(PAIRS)} reference dumps in {REFERENCE}")
    return 0


def check_import(app, folder, bins=False):
    """Each corpus file of a pair read by Cutthulhu, compared with its bin's reference dump: the AAF
    exports (--import-aaf), or with `bins` the bins themselves (--import-avb)."""
    structdiff = os.path.join(ROOT, "scripts", "structdiff.py")
    option = "--import-avb" if bins else "--import-aaf"
    failed = 0
    checked = 0
    with tempfile.TemporaryDirectory() as tmp:
        for avb, aaf in PAIRS:
            name = avb if bins else aaf
            path = os.path.join(folder, name)
            if not os.path.exists(path):
                continue
            stem = os.path.splitext(os.path.basename(name))[0]
            ours = os.path.join(tmp, stem + ".json")
            run = subprocess.run([app, option, path], capture_output=True, text=True)
            if run.returncode != 0:
                print(f"{name}: cutthulhu failed: {run.stderr.strip()}")
                failed += 1
                continue
            with open(ours, "w", encoding="utf-8") as f:
                f.write(run.stdout)
            diff = subprocess.run([sys.executable, structdiff, os.path.join(REFERENCE, stem + ".json"), ours,
                                   "--no-effects"], capture_output=True, text=True)
            checked += 1
            if diff.returncode != 0:
                failed += 1
                print(f"{name}: differs from its bin" + (" as pyavb reads it" if bins else ""))
                print("  " + diff.stdout.strip().replace("\n", "\n  "))
            else:
                print(f"{name}: the same as its bin" + (" as pyavb reads it" if bins else ""))
    if not checked:
        print(f"interop-corpus: no corpus in {folder} (interop-corpus.py fetch)", file=sys.stderr)
        return 77
    return 1 if failed else 0


def check_round_trip(app, folder):
    structdiff = os.path.join(ROOT, "scripts", "structdiff.py")
    dump = os.path.join(ROOT, "scripts", "aaf-dump.py")
    try:
        import aaf2  # noqa: F401
        have_pyaaf2 = True
    except ImportError:
        have_pyaaf2 = False
    failed = 0
    checked = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, _, _ in files():
            if not name.endswith(".aaf") or not os.path.exists(os.path.join(folder, name)):
                continue
            source = os.path.join(folder, name)
            # The same file name, so that the bins have the same name.
            out_dir = os.path.join(tmp, str(checked))
            os.makedirs(out_dir)
            exported = os.path.join(out_dir, os.path.basename(name))
            checked += 1
            run = subprocess.run([app, "--export-aaf", source, "--to", exported], capture_output=True, text=True)
            if run.returncode != 0:
                print(f"{name}: export failed: {run.stderr.strip()}")
                failed += 1
                continue
            dumps = []
            for path in (source, exported):
                imp = subprocess.run([app, "--import-aaf", path], capture_output=True, text=True)
                target = os.path.join(out_dir, f"import{len(dumps)}.json")
                with open(target, "w", encoding="utf-8") as f:
                    f.write(imp.stdout)
                dumps.append(target)
            diff = subprocess.run([sys.executable, structdiff] + dumps, capture_output=True, text=True)
            problems = [] if diff.returncode == 0 else [diff.stdout.strip()]
            if have_pyaaf2:
                same = subprocess.run([sys.executable, dump, "--compare", "--app", app, exported],
                                      capture_output=True, text=True)
                if same.returncode != 0:
                    problems.append(same.stdout.strip())
            if problems:
                failed += 1
                print(f"{name}: the round trip differs")
                for p in problems:
                    print("  " + p.replace("\n", "\n  "))
            else:
                print(f"{name}: the same after a round trip" + ("" if have_pyaaf2 else " (pyaaf2 not installed)"))
    if not checked:
        print(f"interop-corpus: no corpus in {folder} (interop-corpus.py fetch)", file=sys.stderr)
        return 77
    return 1 if failed else 0


def check_aaf(app, folder):
    paths = [os.path.join(folder, name) for name, _, _ in files() if name.endswith(".aaf")]
    paths = [p for p in paths if os.path.exists(p)]
    if not paths:
        print(f"interop-corpus: no corpus in {folder} (interop-corpus.py fetch)", file=sys.stderr)
        return 77
    script = os.path.join(ROOT, "scripts", "aaf-dump.py")
    return subprocess.run([sys.executable, script, "--compare", "--app", app] + paths).returncode


def check_avb(app, folder):
    paths = [os.path.join(folder, name) for name, _, _ in files() if name.endswith(".avb")]
    paths = [p for p in paths if os.path.exists(p)]
    if not paths:
        print(f"interop-corpus: no corpus in {folder} (interop-corpus.py fetch)", file=sys.stderr)
        return 77
    script = os.path.join(ROOT, "scripts", "avb-dump.py")
    return subprocess.run([sys.executable, script, "--compare", "--app", app] + paths).returncode


def corpus_bins(folder):
    paths = [os.path.join(folder, name) for name, _, _ in files() if name.endswith(".avb")]
    return [p for p in paths if os.path.exists(p)]


def check_avb_rewrite(app, folder):
    paths = corpus_bins(folder)
    if not paths:
        print(f"interop-corpus: no corpus in {folder} (interop-corpus.py fetch)", file=sys.stderr)
        return 77
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "rewritten.avb")
        for path in paths:
            run = subprocess.run([app, "--rewrite-avb", path, out], capture_output=True, text=True)
            if run.returncode != 0:
                print(f"{path}: cutthulhu failed: {run.stderr.strip()}")
                failed += 1
                continue
            with open(path, "rb") as a, open(out, "rb") as b:
                original, written = a.read(), b.read()
            if original == written:
                print(f"{path}: the same")
                continue
            failed += 1
            at = next((i for i, (x, y) in enumerate(zip(original, written)) if x != y), min(len(original), len(written)))
            print(f"{path}: differs from byte {at} ({len(original)} bytes read, {len(written)} written)")
    return 1 if failed else 0


def check_avb_write(test, folder):
    try:
        import avb
    except ImportError:
        print("interop-corpus: pyavb is not installed (pip install pyavb)", file=sys.stderr)
        return 77
    if not corpus_bins(folder):
        print(f"interop-corpus: no corpus in {folder} (interop-corpus.py fetch)", file=sys.stderr)
        return 77
    avb_dump = load_script("avb-dump")
    problems = []
    with tempfile.TemporaryDirectory() as out:
        env = dict(os.environ, CUTTHULHU_AVB_OUT=out, CUTTHULHU_INTEROP_CORPUS=folder, QT_QPA_PLATFORM="offscreen")
        run = subprocess.run([test, "forPyavb"], env=env, capture_output=True, text=True)
        if run.returncode != 0 or "SKIP" in run.stdout:
            print(run.stdout + run.stderr, file=sys.stderr)
            return 1
        with open(os.path.join(out, "written.json"), encoding="utf-8") as f:
            expected = json.load(f)
        # The bin from scratch: pyavb reads every object, dumps it as
        # Cutthulhu reads it, and finds the master clip and its source.
        for name, ours in expected["scratch"].items():
            path = os.path.join(out, name)
            try:
                theirs = avb_dump.dump(path)
            except Exception as e:  # pyavb's assertions and errors
                problems.append(f"{name}: pyavb cannot read it: {e!r}")
                continue
            found = avb_dump.differences(theirs, json.loads(ours))
            if not found and avb_dump.canonical(theirs) != ours:
                found = ["the canonical texts differ, though their values are the same"]
            problems += [f"{name}: {line}" for line in found]
            with avb.open(path, use_ext=False) as f:
                mobs = {m.name: m for m in f.content.mobs}
                clip = mobs.get("Scratch clip")
                if clip is None or clip.mob_type != "MasterMob":
                    problems.append(f"{name}: pyavb finds no master clip \"Scratch clip\" ({sorted(mobs)})")
                    continue
                component = clip.tracks[0].component
                if type(component).__name__ != "SourceClip" or component.length != 100 or component.start_time != 10:
                    problems.append(f"{name}: the clip's track is not the source clip written")
                elif component.mob is None or component.mob.name != "clip.mov":
                    problems.append(f"{name}: the source clip does not name the source mob")
                if clip.attributes["_USER"]["Comments"] != "made from scratch":
                    problems.append(f"{name}: the clip's user columns are not read")
                print(f"{name}: pyavb reads {len(list(f.content.mobs))} mobs as written")
        # Bins exported from the model (tst_avb's exportsBin): pyavb reads every
        # object as Cutthulhu does, and the bin (avb-structure.py) as Cutthulhu
        # reads it back (structdiff.py --no-effects: avb-structure.py does not
        # name effects).
        structdiff = os.path.join(ROOT, "scripts", "structdiff.py")
        structure = os.path.join(ROOT, "scripts", "avb-structure.py")
        for name, ours in expected.get("exported", {}).items():
            path = os.path.join(out, name)
            try:
                theirs = avb_dump.dump(path)
            except Exception as e:  # pyavb's assertions and errors
                problems.append(f"{name}: pyavb cannot read it: {e!r}")
                continue
            found = avb_dump.differences(theirs, json.loads(ours["dump"]))
            problems += [f"{name}: {line}" for line in found]
            mine = os.path.join(out, name + ".structure.json")
            with open(mine, "w", encoding="utf-8") as f:
                json.dump(ours["structure"], f)
            run = subprocess.run([sys.executable, structure, path], capture_output=True, text=True)
            pyavbs = os.path.join(out, name + ".pyavb.json")
            with open(pyavbs, "w", encoding="utf-8") as f:
                f.write(run.stdout)
            diff = subprocess.run([sys.executable, structdiff, pyavbs, mine, "--no-effects"], capture_output=True, text=True)
            if run.returncode != 0 or diff.returncode != 0:
                problems.append(f"{name}: pyavb reads another bin: " + (diff.stdout.strip() or run.stderr.strip()))
            elif not found:
                print(f"{name}: pyavb reads the exported bin as Cutthulhu does")
        # The corpus written big-endian reads in pyavb as the originals do.
        for name, original in expected["big_endian"].items():
            try:
                theirs = avb_dump.canonical(avb_dump.dump(os.path.join(out, name)))
            except Exception as e:
                problems.append(f"{name}: pyavb cannot read it: {e!r}")
                continue
            if theirs != avb_dump.canonical(avb_dump.dump(original)):
                problems.append(f"{name}: pyavb reads it differently from {original}")
            else:
                print(f"{name}: pyavb reads it as the original")
    for line in problems:
        print(line)
    return 1 if problems else 0


def export_bins(app, folder, tmp):
    """Each corpus bin of a pair, read (--import-avb, the first reading) and written as a bin
    (--export-avb), under the same file name so that the bins have the same name. Yields (name,
    first reading, exported file), or (name, None, error) when Cutthulhu fails."""
    for avb, _ in PAIRS:
        path = os.path.join(folder, avb)
        if not os.path.exists(path):
            continue
        stem = os.path.splitext(os.path.basename(avb))[0]
        out_dir = os.path.join(tmp, stem)
        os.makedirs(out_dir)
        first = os.path.join(out_dir, "import0.json")
        exported = os.path.join(out_dir, os.path.basename(avb))
        imp = subprocess.run([app, "--import-avb", path], capture_output=True, text=True)
        run = subprocess.run([app, "--export-avb", path, exported], capture_output=True, text=True)
        if imp.returncode != 0 or run.returncode != 0:
            yield avb, None, (imp.stderr + run.stderr).strip()
            continue
        with open(first, "w", encoding="utf-8") as f:
            f.write(imp.stdout)
        yield avb, first, exported


def check_avb_export(app, folder):
    structdiff = os.path.join(ROOT, "scripts", "structdiff.py")
    failed = 0
    checked = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, first, exported in export_bins(app, folder, tmp):
            checked += 1
            if first is None:
                print(f"{name}: cutthulhu failed: {exported}")
                failed += 1
                continue
            imp = subprocess.run([app, "--import-avb", exported], capture_output=True, text=True)
            second = os.path.join(os.path.dirname(first), "import1.json")
            with open(second, "w", encoding="utf-8") as f:
                f.write(imp.stdout)
            diff = subprocess.run([sys.executable, structdiff, first, second, "--fields", "--sources"],
                                  capture_output=True, text=True)
            if imp.returncode != 0 or diff.returncode != 0:
                failed += 1
                print(f"{name}: the bin written reads back differently")
                print("  " + (diff.stdout.strip() or imp.stderr.strip()).replace("\n", "\n  "))
            else:
                print(f"{name}: the bin written reads back the same")
    if not checked:
        print(f"interop-corpus: no corpus in {folder} (interop-corpus.py fetch)", file=sys.stderr)
        return 77
    return 1 if failed else 0


def check_avb_export_pyavb(app, folder):
    try:
        import avb
    except ImportError:
        print("interop-corpus: pyavb is not installed (pip install pyavb)", file=sys.stderr)
        return 77
    structdiff = os.path.join(ROOT, "scripts", "structdiff.py")
    structure = os.path.join(ROOT, "scripts", "avb-structure.py")
    dump = os.path.join(ROOT, "scripts", "avb-dump.py")
    failed = 0
    checked = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, first, exported in export_bins(app, folder, tmp):
            checked += 1
            if first is None:
                print(f"{name}: cutthulhu failed: {exported}")
                failed += 1
                continue
            problems = []
            try:
                with avb.open(exported) as f:
                    mobs = len(list(f.content.mobs))
            except Exception as e:  # pyavb's assertions and errors
                problems.append(f"pyavb cannot open it: {e!r}")
                mobs = 0
            if not problems:
                same = subprocess.run([sys.executable, dump, "--compare", "--app", app, exported],
                                      capture_output=True, text=True)
                if same.returncode != 0:
                    problems.append(same.stdout.strip())
                theirs = os.path.join(os.path.dirname(first), "pyavb.json")
                run = subprocess.run([sys.executable, structure, exported], capture_output=True, text=True)
                with open(theirs, "w", encoding="utf-8") as f:
                    f.write(run.stdout)
                diff = subprocess.run([sys.executable, structdiff, theirs, first, "--no-effects"],
                                      capture_output=True, text=True)
                if run.returncode != 0 or diff.returncode != 0:
                    problems.append(diff.stdout.strip() or run.stderr.strip())
            if problems:
                failed += 1
                print(f"{name}: pyavb reads the bin written differently")
                for p in problems:
                    print("  " + p.replace("\n", "\n  "))
            else:
                print(f"{name}: pyavb reads the bin written ({mobs} mobs) as Cutthulhu exported it")
    if not checked:
        print(f"interop-corpus: no corpus in {folder} (interop-corpus.py fetch)", file=sys.stderr)
        return 77
    return 1 if failed else 0


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
    ap.add_argument("command", choices=["fetch", "manifest", "reference", "check-aaf", "check-avb", "check-import",
                                        "check-avb-import", "check-round-trip", "check-written", "check-avb-rewrite",
                                        "check-avb-write", "check-avb-export", "check-avb-export-pyavb"])
    ap.add_argument("--dir", help="the corpus folder (check-written: where to keep the files written)")
    ap.add_argument("--test", help="tst_cfb, for check-written; tst_avb, for check-avb-write")
    ap.add_argument("--app", help="the cutthulhu executable, for the check- commands")
    args = ap.parse_args()
    if args.command == "fetch":
        return fetch(args.dir or default_dir())
    if args.command == "manifest":
        return manifest(args.dir or default_dir())
    if args.command == "reference":
        return reference(args.dir or default_dir())
    if args.command == "check-import":
        if not args.app:
            ap.error("check-import needs --app")
        return check_import(args.app, args.dir or default_dir())
    if args.command == "check-avb-import":
        if not args.app:
            ap.error("check-avb-import needs --app")
        return check_import(args.app, args.dir or default_dir(), bins=True)
    if args.command == "check-round-trip":
        if not args.app:
            ap.error("check-round-trip needs --app")
        return check_round_trip(args.app, args.dir or default_dir())
    if args.command == "check-aaf":
        if not args.app:
            ap.error("check-aaf needs --app")
        return check_aaf(args.app, args.dir or default_dir())
    if args.command == "check-avb":
        if not args.app:
            ap.error("check-avb needs --app")
        return check_avb(args.app, args.dir or default_dir())
    if args.command == "check-avb-rewrite":
        if not args.app:
            ap.error("check-avb-rewrite needs --app")
        return check_avb_rewrite(args.app, args.dir or default_dir())
    if args.command in ("check-avb-export", "check-avb-export-pyavb"):
        if not args.app:
            ap.error(f"{args.command} needs --app")
        check = check_avb_export if args.command == "check-avb-export" else check_avb_export_pyavb
        return check(args.app, args.dir or default_dir())
    if args.command == "check-avb-write":
        if not args.test:
            ap.error("check-avb-write needs --test")
        return check_avb_write(args.test, args.dir or default_dir())
    if not args.test:
        ap.error("check-written needs --test")
    return check_written(args.test, args.dir)


if __name__ == "__main__":
    sys.exit(main())
