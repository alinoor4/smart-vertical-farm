"""The nine characteristics of a Complex Computing Problem, quoted from the Seoul Accord.

Source: Seoul Accord, Section D - Graduate Attributes, table D.4.1 "Range of Problem Solving",
www.seoulaccord.org (supplied by the project owner; the pages carry no date).

The two text columns are quotations. They are constants on purpose: do not paraphrase, shorten,
reorder or renumber them. `check_text()` makes the generators fail when one is missing from the
finished document. The source PDF breaks three compounds across lines; they are hyphenated as
printed here: well-founded, infrequently-encountered, sub-problems.
"""
import re
from pathlib import Path

DEFINITION = "a computing problem having some or all of the following characteristics"

# (number, characteristic, Seoul Accord wording)
CHARACTERISTICS = [
    (1, "Range of conflicting requirements",
     "Involves wide-ranging or conflicting technical, computing, and other issues"),
    (2, "Depth of analysis required",
     "Has no obvious solution, and requires conceptual thinking and innovative analysis to formulate suitable abstract models"),
    (3, "Depth of knowledge required",
     "A solution requires the use of in-depth computing or domain knowledge and an analytical approach that is based on well-founded principles"),
    (4, "Familiarity of issues",
     "Involves infrequently-encountered issues"),
    (5, "Level of problem",
     "Is outside problems encompassed by standards and standard practice for professional computing"),
    (6, "Extent of stakeholder involvement and level of conflicting requirements",
     "Involves diverse groups of stakeholders with widely varying needs"),
    (7, "Consequences",
     "Has significant consequences in a range of contexts"),
    (8, "Interdependence",
     "Is a high-level problem possibly including many component parts or sub-problems"),
    (9, "Requirement identification",
     "Identification of a requirement or the cause of a problem is ill defined or unknown"),
]


def _squash(text):
    return re.sub(r"\s+", " ", text).strip()


def check_text(finished_text, numbers=None):
    """Raises when a quoted sentence is missing from the finished text (all nine, or the listed ones)."""
    haystack = _squash(finished_text)
    missing = []
    for number, name, wording in CHARACTERISTICS:
        if numbers is not None and number not in numbers:
            continue
        for part in (name, wording):
            if _squash(part) not in haystack:
                missing.append("%d: %s" % (number, part))
    if missing:
        raise SystemExit("Seoul Accord wording missing from the finished text:\n  " + "\n  ".join(missing))


def check_against_plan(plan_path):
    """The constants above must equal the plan's table (plan section 18.4), when the plan is at hand."""
    plan = Path(plan_path)
    if not plan.is_file():
        return
    rows = {}
    text = plan.read_text(encoding="utf-8")
    section = text[text.index("### 18.4"):text.index("### 18.5")]   # other tables also have numbered rows
    for line in section.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) >= 4 and cells[0].isdigit() and 1 <= int(cells[0]) <= 9:
            rows[int(cells[0])] = (cells[1], cells[2])
    for number, name, wording in CHARACTERISTICS:
        if rows.get(number) != (name, wording):
            raise SystemExit("ccp.py differs from plan section 18.4 in row %d" % number)
