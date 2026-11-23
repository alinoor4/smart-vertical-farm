"""Collects every number that the report and the slides use, from files the program wrote.

Documentation tooling only: the product is C++ and does not need Python.

Nothing in build_report.py or build_slides.py types a measured value by hand. The numbers come from

    farm_cli run / compare --out    JSON files, written to out/report/
    farm_tests                      the count of tests and failed checks
    bench/results/summary.md, *.csv written by farm_cli bench
    data/*.json                     the two sample scenarios
    farm_app --screenshot --theme light   the figures, written to out/report/figures/

`gather()` runs the programs first (about 25 s in all) so that the documents always match the
current build. `gather(refresh=False)` reuses the files of the last run.
"""
import csv
import json
import re
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "out" / "report"
FIGURES = OUT / "figures"
BENCH = ROOT / "bench" / "results"


class MissingProgram(RuntimeError):
    pass


def exe(build_dir, name):
    for candidate in (Path(build_dir) / (name + ".exe"), Path(build_dir) / name):
        if candidate.is_file():
            return candidate
    raise MissingProgram("%s not found in %s: build first with .\\build.ps1" % (name, build_dir))


def run_program(args, **kwargs):
    done = subprocess.run([str(a) for a in args], cwd=ROOT, capture_output=True, text=True, **kwargs)
    if done.returncode != 0:
        sys.exit("command failed (exit %d): %s\n%s%s" % (done.returncode, " ".join(map(str, args)), done.stdout[-800:], done.stderr[-800:]))
    return done.stdout


def refresh_outputs(build_dir):
    """Runs the programs whose output the documents quote."""
    OUT.mkdir(parents=True, exist_ok=True)
    FIGURES.mkdir(parents=True, exist_ok=True)
    cli = exe(build_dir, "farm_cli")
    default, flat = ROOT / "data" / "default_scenario.json", ROOT / "data" / "flat_scenario.json"
    for packer in ("ffd", "bfd", "ga"):
        run_program([cli, "run", "--scenario", default, "--packer", packer, "--out", OUT / ("run_default_%s.json" % packer)])
    run_program([cli, "run", "--scenario", default, "--packer", "ga", "--out", OUT / "run_default_ga_again.json"])
    run_program([cli, "run", "--scenario", flat, "--out", OUT / "run_flat.json"])
    run_program([cli, "compare", "--scenario", default, "--out", OUT / "compare_default.json"])
    run_program([cli, "compare", "--scenario", flat, "--out", OUT / "compare_flat.json"])
    (OUT / "tests.txt").write_text(run_program([exe(build_dir, "farm_tests")]), encoding="utf-8")
    run_program([exe(build_dir, "farm_app"), "--screenshot", FIGURES, "--theme", "light"], timeout=180)


def identical_runs():
    """True when two runs with the same scenario and options gave the same result, apart from the timing key."""
    first, second = OUT / "run_default_ga.json", OUT / "run_default_ga_again.json"
    if not (first.is_file() and second.is_file()):
        return None
    a, b = (json.loads(p.read_text(encoding="utf-8")) for p in (first, second))
    a.pop("timing", None)
    b.pop("timing", None)
    return a == b


def third_party():
    """The third-party table of README.md: rows of (library, version, used for, licence)."""
    rows, reading = [], False
    for line in (ROOT / "README.md").read_text(encoding="utf-8").splitlines():
        if line.startswith("| Library |"):
            reading = True
        elif reading:
            if not line.startswith("|"):
                break
            if not set(line) <= set("|- "):
                rows.append([re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", c.strip()) for c in line.strip().strip("|").split("|")])
    return rows[1:] if rows and rows[0][0] == "Library" else rows


TEAM_FILE = Path(__file__).resolve().parent / "team.local.json"
TEAM_HELP = """docs/tools/team.local.json is missing. It holds the title page of the proposal and is ignored by git,
because the repository is public. Create it like this and fill in the details:

{
  "course": "Design and Analysis of Algorithms",
  "kind": "Complex Computing Problems",
  "title_lines": ["Smart Vertical Farm \u2014 Grow-Space Packing &", "Crop Portfolio Optimization Platform"],
  "members": [{"name": "First Member", "roll": "XX-000-000"}, {"name": "...", "roll": "..."}],
  "advisor_label": "Submitted To:",
  "advisor": "Dr. Instructor"
}"""


def load_team():
    """The title-page details (names, roll numbers, instructor), from the local file that git ignores."""
    if not TEAM_FILE.is_file():
        sys.exit(TEAM_HELP)
    team = json.loads(TEAM_FILE.read_text(encoding="utf-8"))
    for key in ("course", "kind", "title_lines", "members", "advisor_label", "advisor"):
        if key not in team:
            sys.exit("team.local.json has no \"%s\"\n%s" % (key, TEAM_HELP))
    return team


def git_commit():
    try:
        done = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True)
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, capture_output=True, text=True)
        return done.stdout.strip() + (" (with uncommitted changes)" if dirty.stdout.strip() else "")
    except OSError:
        return "unknown"


# ---------------------------------------------------------------- benchmark summary

def _number(text):
    try:
        return float(text)
    except ValueError:
        return text


class Suite:
    """One section of bench/results/summary.md plus the rows of its CSV file."""

    def __init__(self, name):
        self.name = name
        self.description = ""
        self.legend = ""
        self.table = []          # medians: dicts with algorithm, [param], n, time_ms, quality
        self.exponents = {}      # "dp" or "wp 10" -> (exponent or None, points)
        self.rows = []           # the CSV rows, numbers converted

    def series(self, algo, param=None, n=None, x="n"):
        """[(x, median time in ms, median quality)] of one algorithm, ordered by x ("n" or "param")."""
        found = [(r[x], r["time_ms"], r["quality"]) for r in self.table
                 if r["algorithm"] == algo and (param is None or r.get("param") == param)
                 and (n is None or r["n"] == n)]
        return sorted(found)

    def exponent(self, algo, param=None):
        label = algo if param is None else "%s %d" % (algo, param)
        return self.exponents.get(label, (None, 0))

    def median_of(self, column, algo, n=None, param=None):
        """Median over the seeds of one CSV column (for example aux1: nodes or states)."""
        values = [r[column] for r in self.rows if r["algo"] == algo
                  and (n is None or r["n"] == n) and (param is None or r["param"] == param)]
        return statistics.median(values) if values else None


class Bench:
    def __init__(self):
        self.header = {}
        self.suites = {}
        self._parse_summary(BENCH / "summary.md")
        for suite in self.suites.values():
            with open(BENCH / (suite.name + ".csv"), newline="", encoding="utf-8") as handle:
                suite.rows = [{k: _number(v) for k, v in row.items()} for row in csv.DictReader(handle)]

    def _parse_summary(self, path):
        current = None
        paragraph = 0
        header = None
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("## "):
                current = Suite(line[3:].strip())
                self.suites[current.name] = current
                paragraph, header = 0, None
            elif current is None:
                m = re.match(r"- (Machine|Compiler|Build flags|Logical processors): (.*)", line)
                if m:
                    self.header[m.group(1)] = m.group(2)
                elif line.startswith("- Seeds"):
                    self.header["Method"] = line[2:]
            elif line.startswith("|"):
                cells = [c.strip() for c in line.strip().strip("|").split("|")]
                if header is None:
                    header = cells
                elif not set(line) <= set("|- "):
                    row = dict(zip(header, map(_number, cells)))
                    row["time_ms"] = row.pop("median time (ms)")
                    row["quality"] = row.pop("median quality")
                    current.table.append(row)
            elif line.startswith("- ") and ":" in line:
                m = re.match(r"- ([^:]+): (none|[0-9.]+)(?: \(from (\d+) points\))?", line)
                if m:
                    value = None if m.group(2) == "none" else float(m.group(2))
                    current.exponents[m.group(1)] = (value, int(m.group(3) or 0))
            elif line.strip() and not line.startswith("Measured exponents") and not line.startswith("Every row"):
                paragraph += 1
                if paragraph == 1:
                    current.description = line.strip()
                elif paragraph == 2:
                    current.legend = line.strip()


# ---------------------------------------------------------------- the whole data set

class Compare:
    """`farm_cli compare --out`: stage -> row name -> {column: value, "note": text}."""

    def __init__(self, path):
        self.stages = {}
        for stage in json.loads(path.read_text(encoding="utf-8")):
            rows = {}
            for row in stage["rows"]:
                entry = dict(zip(stage["columns"], row["values"]))
                entry["note"] = row["note"]
                rows[row["name"]] = entry
            self.stages[stage["stage"]] = rows

    def __call__(self, stage, row, column):
        return self.stages[stage][row][column]


class Data:
    pass


def trimmed_figure(name):
    """The light-theme PNG without the empty strip at its bottom (the window is taller than some tabs)."""
    from PIL import Image, ImageOps
    source, target = FIGURES / (name + ".png"), FIGURES / "trimmed" / (name + ".png")
    target.parent.mkdir(exist_ok=True)
    image = Image.open(source).convert("RGB")
    w, h = image.size
    pixels = image.load()
    background = pixels[w - 6, h - 6]
    last = 4
    for y in range(h - 5, 3, -1):
        if any(pixels[x, y] != background for x in range(4, w - 4, 2)):
            last = y
            break
    bottom = min(h, last + 14)
    inner = image.crop((1, 1, w - 1, bottom - 1))      # redraw the 1 px frame of the window
    ImageOps.expand(inner, border=1, fill=pixels[0, 0]).save(target)
    return target


def cropped_figure(name, box, tag):
    """A region (x0, y0, x1, y1 in pixels of the 1600 x 1000 screenshot) of a light-theme figure, for slides that need it larger."""
    from PIL import Image
    target = FIGURES / "cropped" / ("%s_%s.png" % (name, tag))
    target.parent.mkdir(exist_ok=True)
    with Image.open(FIGURES / (name + ".png")) as image:
        image.convert("RGB").crop(box).save(target)
    return target


# plain number formats for the text: 12,345 / 12,345.6 / 12.3%
def n0(x):
    return "{:,.0f}".format(x)


def n1(x):
    return "{:,.1f}".format(x)


def n2(x):
    return "{:,.2f}".format(x)


def n3(x):
    return "{:,.3f}".format(x)


def gather(build_dir="build", refresh=True):
    if refresh:
        refresh_outputs(ROOT / build_dir)
    d = Data()
    d.runs = {name: json.loads((OUT / ("run_%s.json" % name)).read_text(encoding="utf-8"))
              for name in ("default_ffd", "default_bfd", "default_ga", "flat")}
    d.compare = {name: Compare(OUT / ("compare_%s.json" % name)) for name in ("default", "flat")}
    d.scenario = json.loads((ROOT / "data" / "default_scenario.json").read_text(encoding="utf-8"))
    d.flat_scenario = json.loads((ROOT / "data" / "flat_scenario.json").read_text(encoding="utf-8"))
    d.bench = Bench()
    m = re.search(r"(\d+) test\(s\) run, (\d+) check\(s\) failed", (OUT / "tests.txt").read_text(encoding="utf-8"))
    d.tests, d.failed_checks = int(m.group(1)), int(m.group(2))
    d.commit = git_commit()
    d.identical_runs = identical_runs()
    d.third_party = third_party()
    d.fig = trimmed_figure
    d.crop = cropped_figure
    return d


if __name__ == "__main__":
    data = gather(refresh="--no-refresh" not in sys.argv)
    print("runs:", ", ".join(data.runs), "| compare:", ", ".join(data.compare), "| suites:", len(data.bench.suites))
    print("tests: %d run, %d failed checks | commit %s" % (data.tests, data.failed_checks, data.commit))
