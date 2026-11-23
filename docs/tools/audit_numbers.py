"""Lists the numbers of a finished report (.docx) or deck (.pptx) that appear in no file the program wrote.

    .venv-docs\\Scripts\\python docs\\tools\\audit_numbers.py [docs\\report\\Smart_Vertical_Farm_Report.docx]

The generators insert every measured number from the program's output files, so almost every number
of the document is found in one of them: the JSON files of `farm_cli run` and `compare` (out/report/),
the scenario files (data/), the benchmark CSV files and summary.md, and the output of `farm_tests`.
What is left is printed with its context, for a person to trace. It is expected to be

  * a value computed from output values (a ratio, a difference, a share given in per cent),
  * a constant of the code or of the mathematics (a limit of 22 lots, 19^5, a Bell number),
  * a number of a section, table, figure or roll number.

Citation years and the references section are skipped. The script only reports; it exits with 0.
"""
import csv
import io
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import farm_data
from docx import Document
from docx.oxml.ns import qn
from pptx_kit import slide_text

ROOT = farm_data.ROOT
PATTERN = re.compile(r"(?<![\w.])(?<![A-Za-z]-)(\d[\d,]*(?:\.\d+)?)")


def known_numbers():
    known = set()

    def add(value):
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return
        for digits in range(5):
            known.add("%.*f" % (digits, abs(value)))

    def walk(node):
        if isinstance(node, dict):
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)
        else:
            add(node)

    for path in farm_data.OUT.glob("*.json"):
        walk(json.loads(path.read_text(encoding="utf-8")))
    for path in (ROOT / "data").glob("*.json"):
        scenario = json.loads(path.read_text(encoding="utf-8"))
        walk(scenario)
        add(sum(s["width"] * s["depth"] for s in scenario["shelves"]))
    for path in farm_data.BENCH.glob("*.csv"):
        for row in csv.DictReader(io.open(path, encoding="utf-8")):
            for value in row.values():
                try:
                    add(float(value))
                except ValueError:
                    pass
    for text in ((farm_data.BENCH / "summary.md").read_text(encoding="utf-8"), (farm_data.OUT / "tests.txt").read_text(encoding="utf-8")):
        for match in re.finditer(r"\d[\d.]*", text):
            try:
                add(float(match.group(0).rstrip(".")))
            except ValueError:
                pass
    return known


def docx_texts(path):
    """Body paragraphs and table cells, without headings, captions' numbers, the contents, code boxes and the references."""
    texts, in_references = [], False
    for child in Document(str(path)).element.body.iterchildren():
        if child.tag == qn("w:p"):
            text = "".join(t.text or "" for t in child.iter(qn("w:t")))
            properties = child.find(qn("w:pPr"))
            style = properties.find(qn("w:pStyle")).get(qn("w:val")) if properties is not None and properties.find(qn("w:pStyle")) is not None else ""
            if style.startswith("Heading"):
                in_references = text.split(" ")[-1] == "References"
                continue
            if in_references or style.startswith("TOC") or not text.strip():
                continue
            texts.append(re.sub(r"^(Table|Figure) \d+\. ", "", text))
        elif child.tag == qn("w:tbl") and not in_references:
            rows = child.findall(qn("w:tr"))
            cells = [cell for row in rows for cell in row.findall(qn("w:tc"))]
            if len(rows) == 1 and len(cells) == 1:
                continue                                         # a pseudo-code box or a call-out
            for cell in cells:
                texts.append(" ".join("".join(t.text or "" for t in p.iter(qn("w:t"))) for p in cell.findall(qn("w:p"))))
    return texts


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "report" / "Smart_Vertical_Farm_Report.docx"
    if path.suffix == ".pptx":
        texts = [part for text, notes in slide_text(path)[1:] for part in (text, notes)]     # the title slide holds roll numbers
    else:
        texts = docx_texts(path)
    known = known_numbers()
    total, leftover = 0, {}
    for text in texts:
        for match in PATTERN.finditer(text):
            token = match.group(1).rstrip(",").replace(",", "")
            before = text[max(0, match.start() - 2):match.start()]
            if re.fullmatch(r"(19|20)\d\d", token) and len(before) == 2 and before[0].isalpha():
                continue                                         # a citation year, as in "Karp 1972"
            total += 1
            if token not in known:
                leftover.setdefault(token, text[max(0, match.start() - 45):match.end() + 25].replace("\n", " "))
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")
    print("%s: %d numbers, %d distinct ones not found in any output file:" % (path.name, total, len(leftover)))
    for token, context in sorted(leftover.items(), key=lambda item: float(item[0])):
        print("%10s | %s" % (token, context))


if __name__ == "__main__":
    main()
