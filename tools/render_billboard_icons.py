"""Render the viewport light billboard glyphs in Branding/billboards to editor/assets/billboards.

Requires PySide6. Run from the repository root: python tools/render_billboard_icons.py
"""
import pathlib
import sys

from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

SIZE = 128
GLYPH_INSET = 16  # shaders/billboard.frag draws the tile behind this area

root = pathlib.Path(__file__).resolve().parent.parent
source_dir = root / "Branding" / "billboards"
output_dir = root / "editor" / "assets" / "billboards"

app = QGuiApplication(sys.argv)
for svg in sorted(source_dir.glob("*.svg")):
    renderer = QSvgRenderer(str(svg))
    if not renderer.isValid():
        sys.exit(f"Could not read {svg}")
    image = QImage(SIZE, SIZE, QImage.Format_ARGB32_Premultiplied)
    image.fill(Qt.transparent)
    painter = QPainter(image)
    painter.setRenderHint(QPainter.Antialiasing)
    renderer.render(painter, QRectF(GLYPH_INSET, GLYPH_INSET, SIZE - 2 * GLYPH_INSET, SIZE - 2 * GLYPH_INSET))
    painter.end()
    target = output_dir / (svg.stem + ".png")
    image.convertToFormat(QImage.Format_ARGB32).save(str(target))
    print(f"Wrote {target.relative_to(root)}")
