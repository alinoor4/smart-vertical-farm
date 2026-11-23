"""Small helpers on top of python-pptx for the slides. Documentation tooling only."""
import re

from lxml import etree
from PIL import Image
from pptx import Presentation
from pptx.chart.data import CategoryChartData
from pptx.dml.color import RGBColor
from pptx.enum.chart import XL_CHART_TYPE, XL_LABEL_POSITION
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Emu, Inches, Pt

FONT = "Calibri"
ACCENT = "2E7D32"
DARK = "1B3A2A"
PALE = "EAF3EA"
GREY = "5F6B5F"
LIGHTGREY = "D9E2D9"
BLUE = "2F6F8F"
BROWN = "8A6A3B"
GREEN2 = "5E9C5E"
RED = "B23A3A"
TEXT = "1F2A1F"
WHITE = "FFFFFF"
SLIDE_W, SLIDE_H = 13.333, 7.5

_INLINE = re.compile(r"(\*\*.+?\*\*|\*[^*\s][^*]*?\*)")


def rgb(hex_color):
    return RGBColor.from_string(hex_color)


class Deck:
    def __init__(self, footer_text):
        self.prs = Presentation()
        self.prs.slide_width, self.prs.slide_height = Inches(SLIDE_W), Inches(SLIDE_H)
        self.footer_text = footer_text
        self.count = 0
        self.prs.core_properties.author = "Smart Vertical Farm team"
        self.prs.core_properties.title = "Smart Vertical Farm: Grow-Space Packing and Crop Portfolio Optimization Platform"
        self.prs.core_properties.subject = "Design and Analysis of Algorithms, Complex Computing Problems"

    # ------------------------------------------------------------ slides
    def slide(self, title, notes, number=True, title_size=28, title_color=DARK, bar=True):
        self.count += 1
        slide = self.prs.slides.add_slide(self.prs.slide_layouts[5])         # "Title Only"
        title_shape = slide.shapes.title
        title_shape.left, title_shape.top, title_shape.width, title_shape.height = Inches(0.62), Inches(0.32), Inches(12.1), Inches(1.0)
        frame = title_shape.text_frame
        frame.word_wrap = True
        frame.vertical_anchor = MSO_ANCHOR.MIDDLE
        frame.margin_left = frame.margin_right = 0
        paragraph = frame.paragraphs[0]
        paragraph.alignment = PP_ALIGN.LEFT
        run = paragraph.add_run()
        run.text = title
        run.font.name, run.font.size, run.font.bold = FONT, Pt(title_size), True
        run.font.color.rgb = rgb(title_color)
        if bar:
            accent = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0.3), Inches(0.42), Inches(0.12), Inches(0.8))
            self._fill(accent, ACCENT)
            accent.line.fill.background()
        if number:
            self.footer(slide)
        slide.notes_slide.notes_text_frame.text = notes
        return slide

    def footer(self, slide):
        box = slide.shapes.add_textbox(Inches(0.62), Inches(7.05), Inches(9), Inches(0.3))
        paragraph = box.text_frame.paragraphs[0]
        run = paragraph.add_run()
        run.text = self.footer_text
        run.font.name, run.font.size = FONT, Pt(10)
        run.font.color.rgb = rgb(GREY)
        box.text_frame.margin_left = 0
        number = slide.shapes.add_textbox(Inches(11.9), Inches(7.05), Inches(0.9), Inches(0.3))
        paragraph = number.text_frame.paragraphs[0]
        paragraph.alignment = PP_ALIGN.RIGHT
        field = etree.SubElement(paragraph._p, qn("a:fld"))
        field.set("id", "{B6F15528-21DE-4FAA-801E-634DDDAF4B2B}")
        field.set("type", "slidenum")
        properties = etree.SubElement(field, qn("a:rPr"))
        properties.set("lang", "en-US")
        properties.set("sz", "1000")
        fill = etree.SubElement(properties, qn("a:solidFill"))
        etree.SubElement(fill, qn("a:srgbClr")).set("val", GREY)
        etree.SubElement(properties, qn("a:latin")).set("typeface", FONT)
        etree.SubElement(field, qn("a:t")).text = "‹#›"

    # ------------------------------------------------------------ shapes and text
    @staticmethod
    def _fill(shape, hex_color):
        shape.fill.solid()
        shape.fill.fore_color.rgb = rgb(hex_color)

    def box(self, slide, x, y, w, h, fill=PALE, line=None, shape=MSO_SHAPE.ROUNDED_RECTANGLE, radius=0.06):
        item = slide.shapes.add_shape(shape, Inches(x), Inches(y), Inches(w), Inches(h))
        self._fill(item, fill)
        if line:
            item.line.color.rgb = rgb(line)
            item.line.width = Pt(1)
        else:
            item.line.fill.background()
        if shape == MSO_SHAPE.ROUNDED_RECTANGLE:
            item.adjustments[0] = radius
        item.shadow.inherit = False
        return item

    def _runs(self, paragraph, text, size, color, bold=False, italic=False):
        for piece in _INLINE.split(text):
            if not piece:
                continue
            kind = None
            if piece.startswith("**") and piece.endswith("**") and len(piece) > 4:
                piece, kind = piece[2:-2], "b"
            elif piece.startswith("*") and piece.endswith("*") and len(piece) > 2:
                piece, kind = piece[1:-1], "i"
            run = paragraph.add_run()
            run.text = piece
            run.font.name, run.font.size = FONT, Pt(size)
            run.font.bold = bold or kind == "b"
            run.font.italic = italic or kind == "i"
            run.font.color.rgb = rgb(color)

    def text(self, slide, x, y, w, h, items, size=18, color=TEXT, bold=False, italic=False, align="l", anchor="t",
             bullets=False, space_after=6, shape=None, line_spacing=1.0):
        """items: a string or a list of strings (**bold** and *italic* markup allowed). Returns the shape."""
        item = shape or slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
        frame = item.text_frame
        frame.word_wrap = True
        frame.margin_left = frame.margin_right = Inches(0.05)
        frame.margin_top = frame.margin_bottom = Inches(0.03)
        frame.vertical_anchor = {"t": MSO_ANCHOR.TOP, "m": MSO_ANCHOR.MIDDLE, "b": MSO_ANCHOR.BOTTOM}[anchor]
        for index, text in enumerate([items] if isinstance(items, str) else items):
            paragraph = frame.paragraphs[0] if index == 0 else frame.add_paragraph()
            paragraph.alignment = {"l": PP_ALIGN.LEFT, "c": PP_ALIGN.CENTER, "r": PP_ALIGN.RIGHT}[align]
            paragraph.space_after = Pt(space_after)
            paragraph.line_spacing = line_spacing
            if bullets:
                self._bullet(paragraph, size)
            self._runs(paragraph, text, size, color, bold, italic)
        return item

    @staticmethod
    def _bullet(paragraph, size):
        properties = paragraph._p.get_or_add_pPr()
        properties.set("marL", str(int(Inches(0.3))))
        properties.set("indent", str(-int(Inches(0.3))))
        bullet = etree.SubElement(properties, qn("a:buChar"))
        bullet.set("char", "•")

    @staticmethod
    def send_to_back(slide, shape):
        tree = slide.shapes._spTree
        tree.remove(shape._element)
        tree.insert(2, shape._element)

    def card(self, slide, x, y, w, h, title, body, fill=PALE, title_color=DARK, title_size=20, body_size=16, edge=ACCENT, bullets=False):
        self.box(slide, x, y, w, h, fill=fill)
        stripe = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(x), Inches(y + 0.08), Inches(0.08), Inches(h - 0.16))
        self._fill(stripe, edge)
        stripe.line.fill.background()
        self.text(slide, x + 0.22, y + 0.08, w - 0.35, 0.5, title, size=title_size, color=title_color, bold=True)
        self.text(slide, x + 0.22, y + 0.12 + title_size / 40.0 + 0.2, w - 0.35, h - 0.9, body, size=body_size, space_after=4 if not bullets else 10, bullets=bullets)

    def big_number(self, slide, x, y, w, number, label, color=ACCENT, number_size=40, label_size=14):
        self.text(slide, x, y, w, 0.8, number, size=number_size, color=color, bold=True)
        self.text(slide, x, y + 0.72, w, 0.6, label, size=label_size, color=GREY)

    def caption(self, slide, x, y, w, text, size=11):
        return self.text(slide, x, y, w, 0.4, text, size=size, color=GREY, italic=True)

    def picture(self, slide, path, x, y, w, h, border=True):
        """Fits the image into the box (x, y, w, h) inches, keeps its proportions and centres it. Returns (x, y, w, h) used."""
        with Image.open(str(path)) as image:
            iw, ih = image.size
        scale = min(w / iw, h / ih)
        pw, ph = iw * scale, ih * scale
        px, py = x + (w - pw) / 2, y + (h - ph) / 2
        picture = slide.shapes.add_picture(str(path), Inches(px), Inches(py), Inches(pw), Inches(ph))
        if border:
            picture.line.color.rgb = rgb(LIGHTGREY)
            picture.line.width = Pt(0.75)
        return px, py, pw, ph

    # ------------------------------------------------------------ charts and tables
    def bar_chart(self, slide, x, y, w, h, categories, values, colors, number_format='0.0', maximum=None, size=14, horizontal=False):
        data = CategoryChartData()
        data.categories = categories
        data.add_series("value", values)
        kind = XL_CHART_TYPE.BAR_CLUSTERED if horizontal else XL_CHART_TYPE.COLUMN_CLUSTERED
        frame = slide.shapes.add_chart(kind, Inches(x), Inches(y), Inches(w), Inches(h), data)
        chart = frame.chart
        chart.has_legend = False
        chart.has_title = False
        chart.font.name, chart.font.size = FONT, Pt(size)
        plot = chart.plots[0]
        plot.gap_width = 55
        plot.vary_by_categories = False
        plot.has_data_labels = True
        labels = plot.data_labels
        labels.number_format, labels.number_format_is_linked = number_format, False
        labels.position = XL_LABEL_POSITION.OUTSIDE_END
        labels.font.size, labels.font.bold = Pt(size + 2), True
        series = plot.series[0]
        for index, color in enumerate(colors):
            point = series.points[index]
            point.format.fill.solid()
            point.format.fill.fore_color.rgb = rgb(color)
        value_axis = chart.value_axis
        value_axis.has_major_gridlines = False
        value_axis.visible = False
        value_axis.minimum_scale = 0
        if maximum:
            value_axis.maximum_scale = maximum
        category_axis = chart.category_axis
        category_axis.tick_labels.font.size = Pt(size)
        category_axis.format.line.color.rgb = rgb(LIGHTGREY)
        return chart

    def table(self, slide, x, y, widths, rows, size=13, row_h=0.36, header_fill=ACCENT, first_col_bold=True, align=None):
        """rows[0] is the header; widths in inches."""
        shape = slide.shapes.add_table(len(rows), len(widths), Inches(x), Inches(y), Inches(sum(widths)), Inches(row_h * len(rows)))
        table = shape.table
        table.first_row = True
        for index, width in enumerate(widths):
            table.columns[index].width = Inches(width)
        align = align or ["l"] * len(widths)
        for r, row in enumerate(rows):
            table.rows[r].height = Inches(row_h)
            for c, text in enumerate(row):
                cell = table.cell(r, c)
                cell.margin_left = cell.margin_right = Inches(0.08)
                cell.margin_top = cell.margin_bottom = Inches(0.03)
                cell.vertical_anchor = MSO_ANCHOR.MIDDLE
                cell.fill.solid()
                cell.fill.fore_color.rgb = rgb(header_fill if r == 0 else ("F3F8F3" if r % 2 == 0 else WHITE))
                frame = cell.text_frame
                frame.word_wrap = True
                paragraph = frame.paragraphs[0]
                paragraph.alignment = {"l": PP_ALIGN.LEFT, "c": PP_ALIGN.CENTER, "r": PP_ALIGN.RIGHT}[align[c]]
                self._runs(paragraph, text, size, WHITE if r == 0 else TEXT, bold=(r == 0) or (first_col_bold and c == 0))
        return table

    # ------------------------------------------------------------ output
    def save(self, path):
        self.prs.save(str(path))


def slide_text(path):
    """All text of a .pptx, slide by slide: shapes, tables and the speaker notes."""
    prs = Presentation(str(path))
    result = []
    for slide in prs.slides:
        parts = []
        for shape in slide.shapes:
            if shape.has_text_frame:
                parts.append(shape.text_frame.text)
            if getattr(shape, "has_table", False) and shape.has_table:
                for row in shape.table.rows:
                    parts.extend(cell.text_frame.text for cell in row.cells)
        notes = slide.notes_slide.notes_text_frame.text if slide.has_notes_slide else ""
        result.append(("\n".join(parts), notes))
    return result
