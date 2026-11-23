"""Builds docs/report/Smart_Vertical_Farm_Report.docx. Documentation tooling only: the product is C++.

    .venv-docs\\Scripts\\python docs\\tools\\build_report.py [--no-refresh] [--no-word] [--build-dir build] [--out FILE]

The script runs the built program (farm_cli, farm_app, farm_tests), reads the files it writes and the
benchmark results, and writes every number of the report from them. Nothing measured is typed in.
It stops with a message when

  * a quoted Seoul Accord sentence is missing from the finished text,
  * a statement of the text no longer matches the data,
  * a cross-reference or a format field was left unresolved.

The title page needs the team's names, which are not in the repository: put them in
docs/tools/team.local.json (ignored by git); the script prints the format when the file is missing.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ccp
import facts
import farm_data
import report_a
import report_b
import report_c
import report_d
from docx import Document
from docx.oxml.ns import qn
from docx_kit import Report

TOOLS = Path(__file__).resolve().parent
ROOT = farm_data.ROOT
def build(D, F, team, known):
    r = Report(known)
    r.header_footer("Smart Vertical Farm  |  Design and Analysis of Algorithms")
    report_a.write(r, D, F, team)
    report_b.write(r, D, F)
    report_c.write(r, D, F)
    report_d.write(r, D, F)
    return r


def document_text(path):
    body = Document(str(path)).element.body
    return "\n".join("".join(t.text or "" for t in p.iter(qn("w:t"))) for p in body.iter(qn("w:p")))


def check(path, team):
    """Checks of the finished file: the quoted wording, unresolved fields, an empty contributions table."""
    text = document_text(path)
    ccp.check_text(text)
    for bad in ("{ref:", "%s", "%d", "%%", " ?"):
        if bad in text:
            where = text.index(bad)
            sys.exit("Leftover %r in the finished report: ...%s..." % (bad, text[max(0, where - 60):where + 60].replace("\n", " | ")))
    for pattern in (r"\bNone(?=%| ms\b| PKR\b| kg\b| trays\b| crops\b| weeks\b| nodes\b)", r"\bnan\b", r"\binf\b"):   # a Python value printed by mistake
        found = re.search(pattern, text)
        if found:
            sys.exit("Leftover %r in the finished report: ...%s..." % (found.group(0), text[max(0, found.start() - 60):found.end() + 60].replace("\n", " | ")))
    document = Document(str(path))
    ccp_tables = [t for t in document.tables if t.rows and [c.text for c in t.rows[0].cells] == ["#", "Characteristic", "Seoul Accord wording", "How this project shows it"]]
    if len(ccp_tables) != 1 or len(ccp_tables[0].rows) != 1 + len(ccp.CHARACTERISTICS):
        sys.exit("The report must have one CCP table with a header and nine rows.")
    for row, (number, name, wording) in zip(ccp_tables[0].rows[1:], ccp.CHARACTERISTICS):
        if [c.text for c in row.cells[:3]] != [str(number), name, wording]:
            sys.exit("Row %d of the CCP table differs from the Seoul Accord wording." % number)
    contributions =[t for t in document.tables if t.rows and [c.text for c in t.rows[0].cells] == ["Team member", "Roll number", "Contribution"]]
    if len(contributions) != 1 or any(c.text.strip() for row in contributions[0].rows[1:] for c in row.cells):
        sys.exit("The contributions table must exist once and be empty.")
    for member in team["members"]:
        if member["name"] not in text:
            sys.exit("A team member is missing from the title page.")
    return len(text.split())


def finish_with_word(path):
    script = TOOLS / "finish_word.ps1"
    try:
        done = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script), "-Path", str(path)],
                              capture_output=True, text=True, timeout=300)
    except (OSError, subprocess.TimeoutExpired) as problem:
        print("Word step skipped (%s). Open the report in Word and press F9 on the contents page." % problem)
        return
    if done.returncode != 0:
        print("Word step failed; open the report in Word and press F9 on the contents page.\n" + done.stderr[-400:])
    else:
        print("Word updated the contents (%s)." % done.stdout.strip().replace("\n", " "))


def main():
    parser = argparse.ArgumentParser(description="Build the report (.docx) from the program outputs.")
    parser.add_argument("--no-refresh", action="store_true", help="reuse the files of the last run instead of running the programs")
    parser.add_argument("--no-word", action="store_true", help="do not open Word to build the table of contents")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--out", default=str(ROOT / "docs" / "report" / "Smart_Vertical_Farm_Report.docx"))
    args = parser.parse_args()

    ccp.check_against_plan(ROOT / "plan" / "IMPLEMENTATION_PLAN.md")
    team = farm_data.load_team()
    D = farm_data.gather(args.build_dir, refresh=not args.no_refresh)
    F = facts.derive(D)
    first = build(D, F, team, {})
    second = build(D, F, team, first.labels)           # the second pass knows every section and table number
    if second.unresolved:
        sys.exit("Unresolved references: %s" % ", ".join(sorted(second.unresolved)))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    second.doc.save(str(out))
    if not args.no_word and sys.platform == "win32":
        finish_with_word(out)
    words = check(out, team)
    print("wrote %s (%d words, %d tables, %d figures)" % (out, words, len(Document(str(out)).tables), len(Document(str(out)).inline_shapes)))


if __name__ == "__main__":
    main()
