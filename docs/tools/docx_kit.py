"""Small helpers on top of python-docx for the report. Documentation tooling only.

`Report` keeps section, table and figure numbers. The report is built twice: the first pass only
collects the numbers, the second one writes them into the cross-references, so a table may refer to
a section that comes later.
"""
import re

from docx import Document
from docx.enum.style import WD_STYLE_TYPE
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK, WD_TAB_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

FONT = "Calibri"
MONO = "Consolas"
MATH = "Cambria Math"
ACCENT = "2E7D32"      # green
DARK = "1B3A2A"
PALE = "EAF3EA"
GREY = "5F6B5F"
LINE = "B9C6B9"
TEXT_WIDTH_CM = 16.4   # A4 with 2.3 cm margins

_INLINE = re.compile(r"(\*\*.+?\*\*|\*[^*\s][^*]*?\*|`.+?`|\^\{.+?\}|_\{.+?\})")


def _rgb(hex_color):
    return RGBColor.from_string(hex_color)


def _set_fonts(element_rpr, name):
    rfonts = element_rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        element_rpr.insert(0, rfonts)
    for attribute in ("w:asciiTheme", "w:hAnsiTheme", "w:eastAsiaTheme", "w:cstheme"):
        if rfonts.get(qn(attribute)) is not None:
            del rfonts.attrib[qn(attribute)]
    for attribute in ("w:ascii", "w:hAnsi", "w:eastAsia", "w:cs"):
        rfonts.set(qn(attribute), name)


def _style(document, name, font=FONT, size=None, bold=None, italic=None, color=None,
           before=None, after=None, keep_next=None, line=None):
    style = document.styles[name]
    style.font.name = font
    _set_fonts(style.element.get_or_add_rPr(), font)
    if size is not None:
        style.font.size = Pt(size)
    if bold is not None:
        style.font.bold = bold
    if italic is not None:
        style.font.italic = italic
    if color is not None:
        style.font.color.rgb = _rgb(color)
    paragraph = style.paragraph_format
    if before is not None:
        paragraph.space_before = Pt(before)
    if after is not None:
        paragraph.space_after = Pt(after)
    if keep_next is not None:
        paragraph.keep_with_next = keep_next
    if line is not None:
        paragraph.line_spacing = line
    return style


def _fill(cell, hex_fill):
    properties = cell._tc.get_or_add_tcPr()
    for old in properties.findall(qn("w:shd")):
        properties.remove(old)
    shading = OxmlElement("w:shd")
    shading.set(qn("w:val"), "clear")
    shading.set(qn("w:color"), "auto")
    shading.set(qn("w:fill"), hex_fill)
    properties.append(shading)


def _borders(table, color=LINE, size=4, edges=("top", "left", "bottom", "right", "insideH", "insideV")):
    properties = table._tbl.tblPr
    for old in properties.findall(qn("w:tblBorders")):
        properties.remove(old)
    borders = OxmlElement("w:tblBorders")
    for edge in ("top", "left", "bottom", "right", "insideH", "insideV"):
        element = OxmlElement("w:" + edge)
        if edge in edges:
            element.set(qn("w:val"), "single")
            element.set(qn("w:sz"), str(size))
            element.set(qn("w:color"), color)
        else:
            element.set(qn("w:val"), "nil")
        element.set(qn("w:space"), "0")
        borders.append(element)
    properties.append(borders)


def _cell_edge(cell, edge, color, size):
    properties = cell._tc.get_or_add_tcPr()
    borders = properties.find(qn("w:tcBorders"))
    if borders is None:
        borders = OxmlElement("w:tcBorders")
        properties.append(borders)
    element = OxmlElement("w:" + edge)
    element.set(qn("w:val"), "single")
    element.set(qn("w:sz"), str(size))
    element.set(qn("w:space"), "0")
    element.set(qn("w:color"), color)
    borders.append(element)


def _table_layout(table, widths_cm, margins=(40, 80)):
    table.autofit = False
    properties = table._tbl.tblPr
    layout = OxmlElement("w:tblLayout")
    layout.set(qn("w:type"), "fixed")
    properties.append(layout)
    for old in properties.findall(qn("w:tblW")):
        properties.remove(old)
    width = OxmlElement("w:tblW")
    width.set(qn("w:w"), str(int(sum(widths_cm) / 2.54 * 1440)))
    width.set(qn("w:type"), "dxa")
    properties.append(width)
    cell_margins = OxmlElement("w:tblCellMar")
    for edge, value in (("top", margins[0]), ("left", margins[1]), ("bottom", margins[0]), ("right", margins[1])):
        element = OxmlElement("w:" + edge)
        element.set(qn("w:w"), str(value))
        element.set(qn("w:type"), "dxa")
        cell_margins.append(element)
    properties.append(cell_margins)
    for column, width_cm in zip(table.columns, widths_cm):
        column.width = Cm(width_cm)
    for row in table.rows:
        for cell, width_cm in zip(row.cells, widths_cm):
            cell.width = Cm(width_cm)


def _row_flags(row, header=False, keep_together=True):
    properties = row._tr.get_or_add_trPr()
    if keep_together:
        properties.append(OxmlElement("w:cantSplit"))
    if header:
        properties.append(OxmlElement("w:tblHeader"))


def _field(paragraph, instruction, placeholder="1", size=None, color=None):
    def run_with(element):
        run = paragraph.add_run()
        if size:
            run.font.size = Pt(size)
        if color:
            run.font.color.rgb = _rgb(color)
        run._r.append(element)
        return run
    begin = OxmlElement("w:fldChar")
    begin.set(qn("w:fldCharType"), "begin")
    begin.set(qn("w:dirty"), "true")
    run_with(begin)
    code = OxmlElement("w:instrText")
    code.set(qn("xml:space"), "preserve")
    code.text = " %s " % instruction
    run_with(code)
    separate = OxmlElement("w:fldChar")
    separate.set(qn("w:fldCharType"), "separate")
    run_with(separate)
    text = OxmlElement("w:t")
    text.text = placeholder
    run_with(text)
    end = OxmlElement("w:fldChar")
    end.set(qn("w:fldCharType"), "end")
    run_with(end)


class Report:
    def __init__(self, known_labels=None):
        self.doc = Document()
        self.known = known_labels or {}
        self.labels = {}
        self.unresolved = set()
        self._counters = [0, 0, 0]
        self._tables = 0
        self._figures = 0
        self._setup()

    # ------------------------------------------------------------ page and styles
    def _setup(self):
        section = self.doc.sections[0]
        section.page_width, section.page_height = Cm(21.0), Cm(29.7)
        section.left_margin = section.right_margin = Cm(2.3)
        section.top_margin, section.bottom_margin = Cm(2.3), Cm(2.2)
        section.header_distance = section.footer_distance = Cm(1.1)
        d = self.doc
        _style(d, "Normal", size=11, after=6, line=1.12, color="1F2A1F")
        _style(d, "Heading 1", size=18, bold=True, color=DARK, before=20, after=8, keep_next=True)
        _style(d, "Heading 2", size=14, bold=True, color=ACCENT, before=14, after=5, keep_next=True)
        _style(d, "Heading 3", size=11.5, bold=True, color=DARK, before=10, after=3, keep_next=True)
        _style(d, "Title", size=26, bold=True, color=DARK, before=0, after=6)
        front = d.styles.add_style("Front Heading", WD_STYLE_TYPE.PARAGRAPH)   # like Heading 1, but not in the contents
        front.base_style = d.styles["Normal"]
        _style(d, "Front Heading", size=18, bold=True, color=DARK, before=20, after=8, keep_next=True)
        _style(d, "Caption", size=9.5, bold=False, italic=False, color=GREY, before=2, after=2)
        for name in ("List Bullet", "List Number"):
            _style(d, name, size=11, after=3)
        properties = d.core_properties
        properties.author = properties.last_modified_by = "Smart Vertical Farm team"
        properties.title = "Smart Vertical Farm: Grow-Space Packing and Crop Portfolio Optimization Platform"
        properties.subject = "Design and Analysis of Algorithms, Complex Computing Problems"
        properties.comments = ""

    def header_footer(self, header_text):
        section = self.doc.sections[0]
        section.different_first_page_header_footer = True
        header = section.header.paragraphs[0]
        header.text = ""
        run = header.add_run(header_text)
        run.font.size, run.font.color.rgb = Pt(8.5), _rgb(GREY)
        header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
        footer = section.footer.paragraphs[0]
        footer.alignment = WD_ALIGN_PARAGRAPH.CENTER
        _field(footer, "PAGE", size=9, color=GREY)

    # ------------------------------------------------------------ labels and references
    def _label(self, key, text):
        if key in self.labels:
            raise ValueError("label used twice: " + key)
        self.labels[key] = text

    def ref(self, key):
        if key in self.known:
            return self.known[key]
        self.unresolved.add(key)
        return "?"

    def sec(self, key):
        return "section " + self.ref(key)

    def secs(self, *keys):
        """'sections 4 and 7.2' (or 'section 4' for one key)."""
        numbers = [self.ref(k) for k in keys]
        if len(numbers) == 1:
            return "section " + numbers[0]
        return "sections " + ", ".join(numbers[:-1]) + " and " + numbers[-1]

    # ------------------------------------------------------------ text
    def _runs(self, paragraph, text, size=None, color=None, bold=None, italic=None, font=None):
        for piece in _INLINE.split(text):
            if not piece:
                continue
            kind = None
            if piece.startswith("**") and piece.endswith("**") and len(piece) > 4:
                piece, kind = piece[2:-2], "b"
            elif piece.startswith("`") and piece.endswith("`") and len(piece) > 2:
                piece, kind = piece[1:-1], "m"
            elif piece.startswith("*") and piece.endswith("*") and len(piece) > 2:
                piece, kind = piece[1:-1], "i"
            elif piece.startswith("^{") and piece.endswith("}"):
                piece, kind = piece[2:-1], "sup"
            elif piece.startswith("_{") and piece.endswith("}"):
                piece, kind = piece[2:-1], "sub"
            run = paragraph.add_run(piece)
            if font:
                run.font.name = font
                _set_fonts(run._r.get_or_add_rPr(), font)
            if size:
                run.font.size = Pt(size)
            if color:
                run.font.color.rgb = _rgb(color)
            run.font.bold = True if (kind == "b" or bold) else None
            run.font.italic = True if (kind == "i" or italic) else None
            if kind == "m":
                run.font.name = MONO
                _set_fonts(run._r.get_or_add_rPr(), MONO)
                run.font.size = Pt((size or 11) - 1)
            elif kind == "sup":
                run.font.superscript = True
            elif kind == "sub":
                run.font.subscript = True
        return paragraph

    def p(self, text, size=None, color=None, bold=None, italic=None, align=None, after=None, before=None,
          keep_next=False, indent_cm=None, style=None, container=None):
        paragraph = (container or self.doc).add_paragraph(style=style)
        self._runs(paragraph, text, size, color, bold, italic)
        pf = paragraph.paragraph_format
        if align == "center":
            paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
        elif align == "right":
            paragraph.alignment = WD_ALIGN_PARAGRAPH.RIGHT
        if after is not None:
            pf.space_after = Pt(after)
        if before is not None:
            pf.space_before = Pt(before)
        if keep_next:
            pf.keep_with_next = True
        if indent_cm:
            pf.left_indent = Cm(indent_cm)
        return paragraph

    def bullets(self, items, size=None):
        for text in items:
            self.p(text, style="List Bullet", size=size)

    def numbered(self, items):
        # List Number continues across lists in Word; write the numbers so that each list restarts at 1
        for index, text in enumerate(items, 1):
            paragraph = self.p("%d. %s" % (index, text), after=3, indent_cm=0.9)
            paragraph.paragraph_format.first_line_indent = Cm(-0.6)

    def page_break(self):
        self.doc.add_paragraph().add_run().add_break(WD_BREAK.PAGE)

    # ------------------------------------------------------------ headings
    def h(self, level, key, text):
        self._counters[level - 1] += 1
        for deeper in range(level, 3):
            self._counters[deeper] = 0
        number = ".".join(str(c) for c in self._counters[:level])
        self._label(key, number)
        self.doc.add_heading("%s %s" % (number, text), level)

    def appendix(self, key, letter, text):
        self._label(key, "Appendix " + letter)
        self._counters = [0, 0, 0]
        self.doc.add_heading("Appendix %s %s" % (letter, text), 1)

    def plain_heading(self, text):
        self.doc.add_paragraph(text, style="Front Heading")

    # ------------------------------------------------------------ tables
    def caption(self, kind, key, text, source=None):
        if kind == "Table":
            self._tables += 1
            number = self._tables
        else:
            self._figures += 1
            number = self._figures
        self._label(key, "%s %d" % (kind, number))
        paragraph = self.doc.add_paragraph(style="Caption")
        run = paragraph.add_run("%s %d. " % (kind, number))
        run.font.bold = True
        run.font.color.rgb = _rgb(DARK)
        self._runs(paragraph, text)
        if source:
            run = paragraph.add_run("  Source: " + source)
            run.font.italic = True
        return paragraph

    def table(self, key, caption, headers, rows, widths, source=None, align=None, size=9, first_col_bold=False,
              zebra=True, header_fill=ACCENT, notes=None, min_row_cm=None):
        """rows: list of lists of strings (inline markup allowed; a list inside a cell is several paragraphs)."""
        cap = self.caption("Table", key, caption, source)
        cap.paragraph_format.keep_with_next = True
        cap.paragraph_format.space_before = Pt(8)
        table = self.doc.add_table(rows=1 + len(rows), cols=len(headers))
        _borders(table)
        _table_layout(table, widths)
        align = align or ["l"] * len(headers)
        for column, text in enumerate(headers):
            cell = table.rows[0].cells[column]
            _fill(cell, header_fill)
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            paragraph = cell.paragraphs[0]
            paragraph.paragraph_format.space_after = Pt(0)
            paragraph.paragraph_format.keep_with_next = True
            self._runs(paragraph, text, size=size, color="FFFFFF", bold=True)
            self._align(paragraph, align[column])
        _row_flags(table.rows[0], header=True)
        for number, values in enumerate(rows, 1):
            row = table.rows[number]
            _row_flags(row)
            if min_row_cm:
                height = OxmlElement("w:trHeight")
                height.set(qn("w:val"), str(int(min_row_cm / 2.54 * 1440)))
                height.set(qn("w:hRule"), "atLeast")
                row._tr.get_or_add_trPr().append(height)
            for column, content in enumerate(values):
                cell = row.cells[column]
                cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.TOP
                if zebra and number % 2 == 0:
                    _fill(cell, "F3F8F3")
                parts = content if isinstance(content, list) else [content]
                for index, part in enumerate(parts):
                    paragraph = cell.paragraphs[0] if index == 0 else cell.add_paragraph()
                    paragraph.paragraph_format.space_after = Pt(2 if index < len(parts) - 1 else 0)
                    paragraph.paragraph_format.line_spacing = 1.0
                    self._runs(paragraph, part, size=size, bold=(first_col_bold and column == 0) or None)
                    self._align(paragraph, align[column])
        if len(rows) <= 14:
            for row in table.rows[:-1]:
                for cell in row.cells:
                    for paragraph in cell.paragraphs:
                        paragraph.paragraph_format.keep_with_next = True
        spacer = self.doc.add_paragraph()
        spacer.paragraph_format.space_after = Pt(4)
        spacer.paragraph_format.line_spacing = 0.6
        if notes:
            spacer.paragraph_format.space_after = Pt(0)
            self.p(notes, size=9, color=GREY, after=6)
        return table

    @staticmethod
    def _align(paragraph, code):
        if code == "r":
            paragraph.alignment = WD_ALIGN_PARAGRAPH.RIGHT
        elif code == "c":
            paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER

    # ------------------------------------------------------------ figures
    def figure(self, key, path, caption, source=None, width_cm=TEXT_WIDTH_CM):
        paragraph = self.doc.add_paragraph()
        paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
        paragraph.paragraph_format.keep_with_next = True
        paragraph.paragraph_format.space_before = Pt(6)
        paragraph.paragraph_format.space_after = Pt(2)
        paragraph.add_run().add_picture(str(path), width=Cm(width_cm))
        cap = self.caption("Figure", key, caption, source)
        cap.paragraph_format.space_after = Pt(10)

    # ------------------------------------------------------------ code, formulas, call-outs
    def code(self, text, size=8):
        table = self.doc.add_table(rows=1, cols=1)
        _borders(table, color=LINE, size=4, edges=("top", "bottom", "left", "right"))
        _table_layout(table, [TEXT_WIDTH_CM], margins=(50, 110))
        cell = table.rows[0].cells[0]
        _fill(cell, "F6F8F6")
        _cell_edge(cell, "left", ACCENT, 18)
        lines = text.rstrip("\n").split("\n")
        char_cm = size * 0.55 / 28.35        # width of one Consolas character
        for index, line in enumerate(lines):
            paragraph = cell.paragraphs[0] if index == 0 else cell.add_paragraph()
            pf = paragraph.paragraph_format
            pf.space_after = Pt(0)
            pf.line_spacing = 1.0
            pf.keep_with_next = index < len(lines) - 1
            indent = len(line) - len(line.lstrip(" "))
            if indent:                       # a wrapped line continues two characters deeper than it started
                pf.left_indent = Cm(char_cm * (indent + 2))
                pf.first_line_indent = Cm(-char_cm * 2)
                line = line.lstrip(" ")
                pf.first_line_indent = Cm(-char_cm * 2)
            run = paragraph.add_run(line if line else " ")
            run.font.name = MONO
            _set_fonts(run._r.get_or_add_rPr(), MONO)
            run.font.size = Pt(size)
        spacer = self.doc.add_paragraph()
        spacer.paragraph_format.space_after = Pt(2)
        spacer.paragraph_format.line_spacing = 0.5

    def formula(self, text, number=None):
        """One display line in Cambria Math; `number` is shown at the right margin."""
        paragraph = self.doc.add_paragraph()
        pf = paragraph.paragraph_format
        pf.space_after, pf.space_before, pf.line_spacing = Pt(3), Pt(1), 1.05
        paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
        if number is not None:
            pf.tab_stops.add_tab_stop(Cm(TEXT_WIDTH_CM / 2), WD_TAB_ALIGNMENT.CENTER)
            pf.tab_stops.add_tab_stop(Cm(TEXT_WIDTH_CM), WD_TAB_ALIGNMENT.RIGHT)
            paragraph.alignment = WD_ALIGN_PARAGRAPH.LEFT
            paragraph.add_run("\t")
        self._runs(paragraph, text, size=11, font=MATH)      # _{sub} and ^{sup} markup works here too
        if number is not None:
            paragraph.add_run("\t(%s)" % number)
        return paragraph

    def callout(self, text, fill=PALE, edge=ACCENT, size=10):
        table = self.doc.add_table(rows=1, cols=1)
        _borders(table, color=fill, size=2, edges=())
        _table_layout(table, [TEXT_WIDTH_CM], margins=(70, 130))
        cell = table.rows[0].cells[0]
        _fill(cell, fill)
        _cell_edge(cell, "left", edge, 24)
        parts = text if isinstance(text, list) else [text]
        for index, part in enumerate(parts):
            paragraph = cell.paragraphs[0] if index == 0 else cell.add_paragraph()
            paragraph.paragraph_format.space_after = Pt(3 if index < len(parts) - 1 else 0)
            paragraph.paragraph_format.line_spacing = 1.05
            self._runs(paragraph, part, size=size)
        spacer = self.doc.add_paragraph()
        spacer.paragraph_format.space_after = Pt(2)
        spacer.paragraph_format.line_spacing = 0.5

    def flow(self, key, caption, steps, source=None):
        """A row of boxes joined by arrows, with a short note under each box. steps: (title, note, fill)."""
        count = len(steps)
        arrow_cm = 0.45
        box_cm = (TEXT_WIDTH_CM - arrow_cm * (count - 1)) / count
        widths = []
        for index in range(count):
            widths += [box_cm] + ([arrow_cm] if index < count - 1 else [])
        table = self.doc.add_table(rows=2, cols=len(widths))
        _borders(table, edges=())
        _table_layout(table, widths, margins=(50, 40))
        for index, (title, note, fill) in enumerate(steps):
            top, bottom = table.rows[0].cells[2 * index], table.rows[1].cells[2 * index]
            _fill(top, fill)
            top.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            paragraph = top.paragraphs[0]
            paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
            paragraph.paragraph_format.space_after = Pt(0)
            paragraph.paragraph_format.keep_with_next = True
            self._runs(paragraph, title, size=8.5, color="FFFFFF", bold=True)
            paragraph = bottom.paragraphs[0]
            paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
            paragraph.paragraph_format.space_before = Pt(3)
            paragraph.paragraph_format.space_after = Pt(0)
            paragraph.paragraph_format.keep_with_next = True
            paragraph.paragraph_format.line_spacing = 1.0
            self._runs(paragraph, note, size=7.5, color=GREY)
            if index < count - 1:
                arrow = table.rows[0].cells[2 * index + 1]
                arrow.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
                paragraph = arrow.paragraphs[0]
                paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
                paragraph.paragraph_format.space_after = Pt(0)
                self._runs(paragraph, "▶", size=9, color=ACCENT)
        for row in table.rows:
            _row_flags(row)
        cap = self.caption("Figure", key, caption, source)
        cap.paragraph_format.space_before = Pt(4)
        cap.paragraph_format.space_after = Pt(10)

    # ------------------------------------------------------------ title page and contents
    def title_page(self, course, kind, title_lines, members, advisor_label, advisor):
        def centered(text, size, bold=False, before=0, after=6, color=None):
            paragraph = self.p(text, size=size, bold=bold, align="center", before=before, after=after, color=color)
            return paragraph
        centered(course, 15, before=96, after=26)
        centered(kind, 28, bold=True, after=26, color=DARK)
        for line in title_lines:
            centered(line, 17, bold=True, after=2, color=ACCENT)
        centered("Submitted By:", 12.5, bold=True, before=34, after=10)
        table = self.doc.add_table(rows=len(members), cols=2)
        _borders(table, edges=())
        _table_layout(table, [6.4, 4.6], margins=(60, 100))
        table.alignment = 1
        for row, (name, roll) in zip(table.rows, members):
            for cell, text, side in ((row.cells[0], name, "r"), (row.cells[1], roll, "l")):
                paragraph = cell.paragraphs[0]
                paragraph.paragraph_format.space_after = Pt(0)
                self._runs(paragraph, text, size=12)
                self._align(paragraph, side)
        centered(advisor_label, 12.5, before=30, after=2)
        centered(advisor, 12.5, after=0)
        self.page_break()

    def contents(self, levels="1-2"):
        self.plain_heading("Contents")
        paragraph = self.doc.add_paragraph()
        _field(paragraph, 'TOC \\o "%s" \\h \\z \\u' % levels,
               placeholder="Open the file in Word and press F9 to build the table of contents.")
