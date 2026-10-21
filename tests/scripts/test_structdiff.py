"""scripts/structdiff.py: what Phase 3 gate 1 compares (docs/spec/structure.md)."""

import copy
import importlib.util
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("structdiff", os.path.join(HERE, "..", "..", "scripts", "structdiff.py"))
structdiff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(structdiff)

MOB_A = "urn:smpte:umid:060a2b34.01010101.01010f10.13000000." + "11111111." * 3 + "11111111"
MOB_B = "urn:smpte:umid:060a2b34.01010101.01010f10.13000000." + "22222222." * 3 + "22222222"
MOB_S = "urn:smpte:umid:060a2b34.01010101.01010f10.13000000." + "33333333." * 3 + "33333333"

BASE = {
    "format": "cutthulhu-structure",
    "version": 1,
    "bins": [{
        "name": "Dailies",
        "customColumns": ["Circled"],
        "items": [
            {"kind": "masterClip", "name": "A001_C001", "mobId": MOB_A, "editRate": "25/1", "duration": 250,
             "start": "01:00:00:00", "end": "01:00:10:00", "columns": {"Circled": "yes"}},
            {"kind": "masterClip", "name": "A001_C002", "mobId": MOB_B, "editRate": "25/1", "duration": 100,
             "start": "01:00:20:00", "end": "01:00:24:00"},
            {"kind": "sequence", "name": "Cut 1", "mobId": MOB_S, "editRate": "25/1", "duration": 75,
             "start": "10:00:00:00", "end": "10:00:03:00",
             "sequence": {"tracks": [{"name": "V1", "segments": [
                 {"source": MOB_A, "name": "A001_C001", "track": "V1", "in": 10, "length": 50},
                 {"filler": 25, "transition": "Blend/Dissolve"}]}]}},
        ],
    }],
}


def changed(fn):
    doc = copy.deepcopy(BASE)
    fn(doc["bins"][0])
    return doc


class TestStructDiff(unittest.TestCase):
    def test_same(self):
        self.assertEqual(structdiff.diff(BASE, copy.deepcopy(BASE)), [])

    def test_order_of_items_does_not_matter(self):
        other = changed(lambda b: b["items"].reverse())
        self.assertEqual(structdiff.diff(BASE, other), [])

    def test_renamed_clip_found_by_mob(self):
        other = changed(lambda b: b["items"][0].update(name="A001_C001 good"))
        self.assertEqual(structdiff.diff(BASE, other),
                         ["bin 'Dailies': masterClip 'A001_C001': name 'A001_C001' != 'A001_C001 good'"])
        # Without MobIDs it is a different clip.
        out = structdiff.diff(BASE, other, use_mob=False)
        self.assertIn("bin 'Dailies': masterClip 'A001_C001' only in the first", out)

    def test_timecodes_and_counts(self):
        other = changed(lambda b: b["items"][1].update(start="01:00:20:01"))
        self.assertEqual(structdiff.diff(BASE, other),
                         ["bin 'Dailies': masterClip 'A001_C002': start '01:00:20:00' != '01:00:20:01'"])
        other = changed(lambda b: b["items"].pop(1))
        out = structdiff.diff(BASE, other)
        self.assertIn("bin 'Dailies': counts {'masterClip': 2, 'sequence': 1} != {'masterClip': 1, 'sequence': 1}", out)

    def test_custom_columns(self):
        other = changed(lambda b: b["items"][0]["columns"].update(Circled="no"))
        self.assertEqual(structdiff.diff(BASE, other),
                         ["bin 'Dailies': masterClip 'A001_C001': column 'Circled' 'yes' != 'no'"])
        other = changed(lambda b: b.update(customColumns=["Circled", "Notes"]))
        self.assertEqual(len(structdiff.diff(BASE, other)), 1)

    def test_sequence_structure(self):
        def trim(b):
            b["items"][2]["sequence"]["tracks"][0]["segments"][0]["in"] = 11
        out = structdiff.diff(BASE, changed(trim))
        self.assertEqual(out, ["bin 'Dailies': sequence 'Cut 1': V1 segment 1: "
                               f"{MOB_A} V1 in 10 length 50 != {MOB_A} V1 in 11 length 50"])
        def retrack(b):
            b["items"][2]["sequence"]["tracks"].append({"name": "A1", "segments": []})
        self.assertIn("bin 'Dailies': sequence 'Cut 1': tracks ['V1'] != ['V1', 'A1']",
                      structdiff.diff(BASE, changed(retrack)))
        def source(b):
            b["items"][2]["sequence"]["tracks"][0]["segments"][0]["source"] = MOB_B
        self.assertEqual(len(structdiff.diff(BASE, changed(source))), 1)
        # By name, a different MobID with the same clip name is the same.
        self.assertEqual(structdiff.diff(BASE, changed(source), use_mob=False), [])

    def test_without_effects(self):
        def effects(b):
            segments = b["items"][2]["sequence"]["tracks"][0]["segments"]
            segments[1]["transition"] = "AAF/VideoDissolve_2"
            segments.append({"filler": 5, "effect": "AAF/Title_2"})
        other = changed(effects)
        self.assertEqual(len(structdiff.diff(BASE, other)), 2)
        # Effect names left out, and filler an effect kept apart is one: the
        # 25 frames and the 5 compare as 30 with the reference's 30.
        def joined(b):
            b["items"][2]["sequence"]["tracks"][0]["segments"][1] = {"filler": 30}
        self.assertEqual(structdiff.diff(changed(joined), other, effects=False), [])

    def test_sources(self):
        # Source chains are compared only when asked, in order, field by field.
        source = {"kind": "file", "mob": MOB_B, "track": "V1", "name": "A001_C001.mxf"}
        with_source = changed(lambda b: b["items"][0].update(sources=[source]))
        self.assertEqual(structdiff.diff(BASE, with_source), [])
        self.assertEqual(len(structdiff.diff(BASE, with_source, sources=True)), 1)
        renamed = changed(lambda b: b["items"][0].update(sources=[dict(source, name="other.mxf")]))
        out = structdiff.diff(with_source, renamed, sources=True)
        self.assertEqual(len(out), 1)
        self.assertIn("source 1", out[0])
        self.assertEqual(structdiff.diff(with_source, copy.deepcopy(with_source), sources=True), [])

    def test_bins(self):
        other = changed(lambda b: b.update(name="Selects"))
        self.assertEqual(structdiff.diff(BASE, other),
                         ["bin 'Dailies': only in the first", "bin 'Selects': only in the second"])


if __name__ == "__main__":
    unittest.main()
