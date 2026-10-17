#!/usr/bin/env python3
"""Make the PDFs we send to the editor for Phase 1 gate 5.

    make-editor-pdfs.py --out <folder>

writes:
* Cutthulhu-Mac-setup.pdf, from docs/checklists/editor-setup-macos.md:
  installing, the macOS security step, a test user, the session.
* Cutthulhu-checklist.pdf, from docs/checklists/phase1-gate5.md (made by
  make-checklist.py): a fillable form. Preview on the Mac ticks the Found
  boxes and types in the Where and Notes fields, and File > Save keeps them.

Needs reportlab. The text is set in Inter, the application's own font.
"""

import argparse
import os
import re
import sys

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import landscape, letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (CondPageBreak, Flowable, Paragraph, Preformatted, SimpleDocTemplate, Spacer,
                                Table, TableStyle)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, "src", "ui", "resources", "fonts")
ACCENT = colors.HexColor("#5b3a8c")
GREY = colors.HexColor("#666666")
RULE = colors.HexColor("#c8c8c8")


def fonts():
    pdfmetrics.registerFont(TTFont("Inter", os.path.join(FONTS, "Inter-Regular.ttf")))
    pdfmetrics.registerFont(TTFont("Inter-SemiBold", os.path.join(FONTS, "Inter-SemiBold.ttf")))
    pdfmetrics.registerFontFamily("Inter", normal="Inter", bold="Inter-SemiBold", italic="Inter",
                                  boldItalic="Inter-SemiBold")


def styles(base=10.5):
    s = {
        "title": ParagraphStyle("title", fontName="Inter-SemiBold", fontSize=20, leading=25, textColor=ACCENT,
                                spaceAfter=10),
        "h2": ParagraphStyle("h2", fontName="Inter-SemiBold", fontSize=14, leading=18, textColor=ACCENT,
                             spaceBefore=12, spaceAfter=6),
        "body": ParagraphStyle("body", fontName="Inter", fontSize=base, leading=base * 1.4, spaceAfter=5,
                               alignment=TA_LEFT),
        "cell": ParagraphStyle("cell", fontName="Inter", fontSize=8.5, leading=10.5),
        "cellgrey": ParagraphStyle("cellgrey", fontName="Inter", fontSize=8.5, leading=10.5, textColor=GREY),
        "head": ParagraphStyle("head", fontName="Inter-SemiBold", fontSize=8.5, leading=10.5,
                               textColor=colors.white),
        "code": ParagraphStyle("code", fontName="Courier", fontSize=9.5, leading=13, backColor=colors.HexColor("#f0eef5"),
                               borderPadding=6, leftIndent=6, spaceBefore=4, spaceAfter=8),
    }
    return s


def inline(text):
    """Markdown **bold** and `code` to reportlab markup."""
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    text = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"`(.+?)`", r'<font name="Courier">\1</font>', text)
    text = re.sub(r"(?<![\w*])\*(\S.*?)\*(?![\w*])", r"<i>\1</i>", text)
    return text


def markdown_flowables(lines, st):
    """The subset of Markdown the guide uses: headings, paragraphs, numbered
    and bulleted lists (one level of nesting), fenced code."""
    out = []
    para = []
    code = None

    def flush():
        if para:
            out.append(Paragraph(inline(" ".join(para)), st["body"]))
            para.clear()

    for raw in lines:
        line = raw.rstrip("\n")
        if code is not None:
            if line.strip().startswith("```"):
                out.append(Preformatted("\n".join(code), st["code"]))
                code = None
            else:
                code.append(line)
            continue
        if line.strip().startswith("```"):
            flush()
            code = []
            continue
        if not line.strip():
            flush()
            continue
        m = re.match(r"^(#+)\s+(.*)", line)
        if m:
            flush()
            out.append(Paragraph(inline(m.group(2)), st["title"] if len(m.group(1)) == 1 else st["h2"]))
            continue
        m = re.match(r"^(\s*)(\d+\.|[*-])\s+(.*)", line)
        if m:
            flush()
            depth = len(m.group(1)) // 2
            bullet = m.group(2) if m.group(2)[0].isdigit() else "•"
            style = ParagraphStyle("li", parent=st["body"], leftIndent=18 + 16 * depth, bulletIndent=4 + 16 * depth,
                                   spaceAfter=3)
            out.append(Paragraph(inline(m.group(3)), style, bulletText=bullet))
            continue
        para.append(line.strip())
    flush()
    return out


def footer(text):
    def draw(canvas, doc):
        canvas.saveState()
        canvas.setFont("Inter", 8)
        canvas.setFillColor(GREY)
        canvas.drawString(doc.leftMargin, 0.45 * inch, text)
        canvas.drawRightString(doc.pagesize[0] - doc.rightMargin, 0.45 * inch, "Page %d" % doc.page)
        canvas.restoreState()
    return draw


def make_guide(src, out):
    st = styles()
    with open(src, encoding="utf-8") as f:
        flow = markdown_flowables(f.readlines(), st)
    doc = SimpleDocTemplate(out, pagesize=letter, leftMargin=0.8 * inch, rightMargin=0.8 * inch,
                            topMargin=0.7 * inch, bottomMargin=0.8 * inch, title="Testing Cutthulhu on your Mac",
                            author="Cutthulhu")
    doc.build(flow, onFirstPage=footer("Cutthulhu: Mac setup for testers"),
              onLaterPages=footer("Cutthulhu: Mac setup for testers"))


# ---------------------------------------------------------------- the checklist form

EDITOR_INTRO = """# Cutthulhu: the editor's checklist

Please find every window and menu command below in Cutthulhu, the way you would in Media Composer, without help, manuals or web searches. There are {total} entries.

* Click the **Found** box when you find an entry without help.
* Under **Where**, write where you looked first, if that was not where you found it.
* Under **Notes**, write anything that looks or works differently from Media Composer.
* Entries marked *later* are in their menus but greyed out: they come in a later version. Please still find them.
* Save often with **File > Save** in Preview, and send the saved file back when you are done.
"""

class Field(Flowable):
    """A form field drawn where the table puts it: a tick box or a text box."""

    def __init__(self, name, kind, width, height=13):
        super().__init__()
        self.name, self.kind, self.width, self.height = name, kind, width, height

    def wrap(self, aw, ah):
        return self.width, self.height

    def draw(self):
        x, y = self.canv.absolutePosition(0, 0)
        form = self.canv.acroForm
        if self.kind == "check":
            form.checkbox(name=self.name, tooltip="Found", x=x + (self.width - 11) / 2, y=y + 1, size=11,
                          buttonStyle="check", borderColor=GREY, fillColor=colors.white, borderWidth=0.8,
                          forceBorder=True)
        else:
            form.textfield(name=self.name, tooltip=self.name, x=x, y=y, width=self.width, height=self.height,
                           fontName="Helvetica", fontSize=8, borderColor=RULE, fillColor=colors.white,
                           borderWidth=0.5, forceBorder=True)


def parse_checklist(path):
    """The checklist's intro lines, then (heading, header cells, rows)."""
    intro, sections = [], []
    current = None
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    in_comment = False
    for line in lines:
        if line.startswith("<!--"):
            in_comment = True
        if in_comment:
            in_comment = "-->" not in line
            continue
        if line.startswith("## "):
            current = {"title": line[3:], "header": None, "rows": [], "after": []}
            sections.append(current)
            continue
        if current is None:
            intro.append(line)
            continue
        if line.startswith("|"):
            cells = [c.strip().replace("\\|", "|") for c in re.split(r"(?<!\\)\|", line.strip())[1:-1]]
            if set("".join(cells)) <= set("-:"):
                continue
            if current["header"] is None:
                current["header"] = cells
            else:
                current["rows"].append(cells)
        else:
            current["after"].append(line)
    return intro, sections


def make_checklist(src, out):
    st = styles(10)
    intro, sections = parse_checklist(src)
    page = landscape(letter)
    doc = SimpleDocTemplate(out, pagesize=page, leftMargin=0.5 * inch, rightMargin=0.5 * inch,
                            topMargin=0.55 * inch, bottomMargin=0.7 * inch,
                            title="Cutthulhu Phase 1: the editor's checklist", author="Cutthulhu")
    flow = []
    # The editor's own instructions; the Markdown's intro is for us.
    total = sum(len(sec["rows"]) for sec in sections if sec["header"])
    flow += markdown_flowables(EDITOR_INTRO.format(total=total).split("\n"), st)
    info = []
    for label in ("Name", "Date", "Mac model", "macOS version"):
        info += [Paragraph("<b>%s</b>" % label, st["body"]), Field("info." + label.lower(), "text", 112, 15)]
    t = Table([info], colWidths=[62, 118, 40, 118, 70, 118, 92, 118], hAlign="LEFT")
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "MIDDLE"), ("LEFTPADDING", (0, 0), (-1, -1), 2)]))
    flow += [Spacer(1, 6), t]

    for si, sec in enumerate(sections):
        if sec["header"] is None:
            # The summary: its lines, the found count as fields.
            flow.append(Paragraph(inline(sec["title"]), st["h2"]))
            for line in sec["after"]:
                if "Found:" in line:
                    row = [Paragraph("<b>Found:</b>", st["body"]), Field("summary.found", "text", 50, 15),
                           Paragraph("of", st["body"]), Field("summary.total", "text", 50, 15)]
                    ft = Table([row], colWidths=[45, 60, 20, 60], hAlign="LEFT")
                    ft.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "MIDDLE")]))
                    flow.append(ft)
                elif line.strip():
                    m = re.match(r"^(\s*)[*-]\s+(.*)", line)
                    flow.append(Paragraph(inline(m.group(2) if m else line.strip()), st["body"],
                                          bulletText="•" if m else None))
            continue
        header = sec["header"]
        fillable = "Found" in header
        widths = {"Window": 190, "Where it is": 190, "Command": 210, "Shortcut": 68, "Guide": 78,
                  "Status": 42, "Found": 36, "Where": 125, "Notes": 0}
        cols = [widths.get(h, 80) for h in header]
        total = page[0] - doc.leftMargin - doc.rightMargin
        cols[header.index("Notes")] = total - sum(cols)
        data = [[Paragraph(h, st["head"]) for h in header]]
        for ri, row in enumerate(sec["rows"]):
            cells = []
            for h, c, w in zip(header, row, cols):
                if fillable and h == "Found":
                    cells.append(Field("s%d.r%d.found" % (si, ri), "check", w - 6))
                elif fillable and h in ("Where", "Notes"):
                    cells.append(Field("s%d.r%d.%s" % (si, ri, h.lower()), "text", w - 6))
                elif h == "Status" and "later" in c:
                    cells.append(Paragraph("later", st["cellgrey"]))
                else:
                    cells.append(Paragraph(inline(c), st["cell"]))
            data.append(cells)
        table = Table(data, colWidths=cols, repeatRows=1, hAlign="LEFT")
        table.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), ACCENT),
            ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f5f3f9")]),
            ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
            ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
            ("TOPPADDING", (0, 0), (-1, -1), 2.5), ("BOTTOMPADDING", (0, 0), (-1, -1), 2.5),
            ("LEFTPADDING", (0, 0), (-1, -1), 3), ("RIGHTPADDING", (0, 0), (-1, -1), 3),
        ]))
        # The heading stays with the start of its table.
        flow.append(CondPageBreak(1.3 * inch))
        flow.append(Paragraph(inline(sec["title"]), st["h2"]))
        flow.append(table)
    doc.build(flow, onFirstPage=footer("Cutthulhu Phase 1: the editor's checklist"),
              onLaterPages=footer("Cutthulhu Phase 1: the editor's checklist"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="the folder to write the PDFs into")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    fonts()
    docs = os.path.join(ROOT, "docs", "checklists")
    guide = os.path.join(args.out, "Cutthulhu-Mac-setup.pdf")
    checklist = os.path.join(args.out, "Cutthulhu-checklist.pdf")
    make_guide(os.path.join(docs, "editor-setup-macos.md"), guide)
    make_checklist(os.path.join(docs, "phase1-gate5.md"), checklist)
    print(guide)
    print(checklist)
    return 0


if __name__ == "__main__":
    sys.exit(main())
