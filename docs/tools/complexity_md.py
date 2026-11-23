"""Reads docs/complexity-analysis.md so that the report quotes its pseudo-code, bounds and proofs
instead of retyping them. Documentation tooling only.

Each algorithm section "### 3.2 Welsh-Powell" has seven parts, introduced by "**1 Problem.**" ...
"**7 Pipeline.**". The report uses parts 1 to 5; the measured exponents (part 6) come from
bench/results/summary.md through farm_data.py.
"""
import re
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "complexity-analysis.md"

# section numbers of the markdown file -> label keys of the report ({ref:key} is resolved by report_b.algo)
RENUMBER = {"3.1": "a31", "3.2": "a32", "3.3": "a33", "3.4": "a34",
            "4.1": "a41", "4.2": "a42", "4.3": "a43",
            "5.1": "a51", "5.2": "a52", "5.3": "a53",
            "6.1": "a61", "6.2": "a62",
            "7.1": "a71", "7.2": "a72", "7.3": "a73", "7.4": "a74"}

# sentences that only make sense inside the markdown file or the plan
REWORD = [
    ("Each test is the rule of plan section 7.1, and", "Each test is one of the rules above, and"),
    ("as the plan specified; it does not", "as specified; it does not"),
    ("on the G1 crown graph where", "on the crown graph of golden case G1 where"),
    ("NP-hard (section 8)", "NP-hard (section {ref:hard})"),
    ("For `f > 0` I have not proved", "For `f > 0` we have not proved"),
    ("on the G4 instance of the plan's Appendix A", "on golden case G4 of the plan's Appendix A"),
    ("In the plan's Appendix A, G5,", "In golden case G5 of the plan's Appendix A,"),
]


def clean(text):
    for old, new in REWORD:
        text = text.replace(old, new)
    text = re.sub(r"\b(of|in|as|see) (\d\.\d)\b", lambda m: "%s {ref:%s}" % (m.group(1), RENUMBER[m.group(2)]) if m.group(2) in RENUMBER else m.group(0), text)
    return text


def sections():
    """{'3.2': {'title': ..., '1': problem, '2': text before the code, 'code': ..., '3': time, '4': space, '5': correctness}}"""
    lines = PATH.read_text(encoding="utf-8").splitlines()
    result, current, part, in_code = {}, None, None, False
    for line in lines:
        if line.startswith("### "):
            m = re.match(r"### (\d\.\d) (.*)", line)
            current = {"title": re.sub(r"\s*\(`[^)]*`\)\s*$", "", m.group(2)).strip(), "code": []}
            result[m.group(1)] = current
            part, in_code = None, False
            continue
        if line.startswith("## "):
            current, part = None, None
            continue
        if current is None:
            continue
        if line.startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            current["code"].append(line)
            continue
        m = re.match(r"\*\*(\d) [A-Za-z-]+\.\*\*\s*(.*)", line)
        if m:
            part = m.group(1)
            current[part] = [m.group(2)] if m.group(2) else []
        elif part is not None and line.strip():
            current.setdefault(part, []).append(line)
        elif part is None and line.strip() and not line.startswith("---"):
            current.setdefault("intro", []).append(line)        # section 4.3 has no numbered parts
    for current in result.values():
        current["code"] = clean("\n".join(current["code"]))
        for key in ["1", "2", "3", "4", "5", "intro"]:
            current[key] = [clean(p) for p in current.get(key, [])]
    return result


def summary_table():
    """The bounds table at the top of the file: a list of rows (component, algorithm, paradigm, time, space, optimal)."""
    rows, reading = [], False
    for line in PATH.read_text(encoding="utf-8").splitlines():
        if line.startswith("| Component |"):
            reading = True
            continue
        if reading:
            if not line.startswith("|"):
                break
            if set(line) <= set("|- "):
                continue
            rows.append([c.strip().replace("`", "") for c in line.strip().strip("|").split("|")])
    return rows


def references():
    """The reference list of section 10 as (text without the checking note) strings."""
    text = PATH.read_text(encoding="utf-8")
    block = text[text.index("## 10. References"):]
    entries = []
    for line in block.splitlines():
        if line.startswith("- "):
            entry = re.sub(r"\s*\[(Record|Statement|Not looked up)[^\]]*\]\s*$", "", line[2:])
            entries.append(entry)
    return entries


if __name__ == "__main__":
    import sys
    sys.stdout.reconfigure(encoding="utf-8")
    found = sections()
    longest = max((len(l) for s in found.values() for l in s["code"].split("\n")), default=0)
    print(len(found), "sections; longest pseudo-code line:", longest, "characters")
    for number, s in found.items():
        lines = s["code"].count("\n") + 1
        parts = [k for k in "12345" if s[k]]
        print("%s %-52s code lines %2d  parts %s" % (number, s["title"][:52], lines, "".join(parts)))
    bad = [(n, p) for n, s in found.items() for k in "12345" for p in s[k]
           if re.search(r"\bI\b|plan section|\b\d\.\d\b(?!\d)", re.sub(r"`[^`]*`", "", p)) and not re.search(r"\d\.\d+%|0\.\d|\d,\d{3}", p)]
    for n, p in bad[:20]:
        print("CHECK", n, p[:160])
    print(len(summary_table()), "bound rows;", len(references()), "references")
