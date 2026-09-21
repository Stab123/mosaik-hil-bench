#!/usr/bin/env python3
"""Controle du PDF d'impression : decodage et mesure de la taille de module.

Importe par verify.py. Rasterise chaque page, decode le QR et mesure la
taille du symbole sur le papier a partir de sa position rendue, plutot que
de la deduire du CSS. Une regle de mise en page qui comprime le symbole -
un conteneur flex, par exemple - se voit alors immediatement.
"""

from __future__ import annotations

import pathlib

# Taille de module visee a l'impression, en millimetres.
MODULE_MM_SAFE = 0.40
MODULE_MM_FLOOR = 0.33

DPI = 1200
PAGE_MM = (85.0, 55.0)
TOLERANCE_MM = 0.5


def verdict(mm: float) -> str:
    if mm >= MODULE_MM_SAFE:
        return "ok"
    if mm >= MODULE_MM_FLOOR:
        return "limite"
    return "TROP DENSE"


def check(pdf_path: pathlib.Path, expected: bytes, modules: int) -> list[str]:
    """Verifie chaque page. Retourne la liste des erreurs, vide si conforme.

    `modules` est le cote du symbole en modules, zone de silence exclue :
    c'est exactement ce que delimitent les quatre coins rendus par zxing.
    """
    import numpy as np
    import pypdfium2 as pdfium
    import zxingcpp

    errors: list[str] = []
    document = pdfium.PdfDocument(pdf_path)

    for index, page in enumerate(document, start=1):
        width_pt, height_pt = page.get_size()
        size_mm = (width_pt * 25.4 / 72, height_pt * 25.4 / 72)
        if any(abs(a - b) > TOLERANCE_MM for a, b in zip(size_mm, PAGE_MM)):
            errors.append(
                f"page {index} : format {size_mm[0]:.1f} x {size_mm[1]:.1f} mm, "
                f"attendu {PAGE_MM[0]:.0f} x {PAGE_MM[1]:.0f}"
            )

        image = np.array(page.render(scale=DPI / 72).to_pil().convert("L"))
        result = zxingcpp.read_barcode(image)

        if result is None:
            errors.append(f"page {index} : aucun QR decodable")
            continue
        if bytes(result.bytes) != expected:
            errors.append(f"page {index} : contenu decode different de vcard.vcf")
            continue

        corners = result.position
        span_px = max(
            abs(corners.top_right.x - corners.top_left.x),
            abs(corners.bottom_left.y - corners.top_left.y),
        )
        span_mm = span_px * 25.4 / DPI
        module_mm = span_mm / modules
        state = verdict(module_mm)
        print(
            f"   page {index} : symbole {span_mm:.1f} mm, {modules} modules, "
            f"{module_mm:.3f} mm par module -> {state}"
        )
        if state == "TROP DENSE":
            errors.append(
                f"page {index} : {module_mm:.3f} mm par module, "
                f"sous le plancher de {MODULE_MM_FLOOR:.2f} mm"
            )

    return errors
