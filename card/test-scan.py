#!/usr/bin/env python3
"""Planche de diagnostic : trouver la limite de lecture d'un telephone.

Produit une page A4 portant le meme contact encode a plusieurs densites, a
deux tailles d'impression. On scanne les codes dans l'ordre : le premier qui
resiste donne la limite de l'appareil, et dit s'il faut alleger le contenu
du QR ou agrandir le symbole.

    python3 test-scan.py
    node export-pdf.js planche-test-scan.pdf planche-test-scan.html

Un code qui ne passe avec aucune taille ni aucune densite designe le lecteur,
pas la carte : l'appareil photo de certains telephones ne traite que les URL
et laisse tomber les fiches de contact. Google Lens, lui, les prend en charge.
"""

from __future__ import annotations

import base64
import io
import pathlib

import qrcode
from qrcode.constants import ERROR_CORRECT_M

import generate

HERE = pathlib.Path(__file__).resolve().parent
TAILLES_MM = (40, 25)


def payloads() -> list[tuple[str, str, str]]:
    """(identifiant, description, donnees) du plus leger au plus dense."""
    lines = [
        l for l in (HERE / "vcard.vcf").read_text(encoding="utf-8").splitlines() if l.strip()
    ]

    def vcard(drop: tuple[str, ...]) -> str:
        kept = [l for l in lines if not l.startswith(drop)]
        return generate.to_ascii("\r\n".join(kept) + "\r\n")

    return [
        ("A", "URL seule", "https://github.com/Stab123/mosaik-hil-bench"),
        ("B", "contact minimal", vcard(("ADR", "NOTE", "TITLE", "URL"))),
        ("C", "contact de la carte", vcard(("ADR", "NOTE"))),
        ("D", "contact complet", vcard(())),
    ]


def png_data_uri(data: str) -> tuple[str, int, int]:
    qr = qrcode.QRCode(error_correction=ERROR_CORRECT_M, border=4, box_size=20)
    qr.add_data(data)
    qr.make(fit=True)
    image = qr.make_image(fill_color="black", back_color="white")
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    uri = "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()
    return uri, qr.version, qr.modules_count


def main() -> int:
    blocks = []
    for size in TAILLES_MM:
        cells = []
        for ident,描 in []:
            pass
        for ident, description, data in payloads():
            uri, version, modules = png_data_uri(data)
            total = modules + 8
            mm_per_module = size / total
            cells.append(f"""
        <figure class="cellule">
          <img src="{uri}" alt="code {ident}" style="width:{size}mm;height:{size}mm">
          <figcaption>
            <strong>{ident}{size}</strong> — {description}<br>
            version {version}, {modules} modules, {len(data.encode()):d} octets<br>
            {mm_per_module:.3f} mm par module
          </figcaption>
        </figure>""")
        blocks.append(
            f'<section><h2>Impression a {size} mm</h2><div class="rangee">'
            + "".join(cells)
            + "</div></section>"
        )

    html = f"""<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<title>Planche de test de lecture — MOSAÏK</title>
<style>
  @page {{ size: A4; margin: 12mm; }}
  body {{ font-family: "Helvetica Neue", Helvetica, Arial, sans-serif; color: #111; margin: 0; }}
  h1 {{ font-size: 15pt; margin: 0 0 2mm; }}
  .mode {{ font-size: 9pt; color: #444; line-height: 1.5; margin: 0 0 6mm; max-width: 170mm; }}
  h2 {{ font-size: 10pt; margin: 6mm 0 3mm; color: #333; }}
  .rangee {{ display: flex; gap: 6mm; align-items: flex-start; }}
  .cellule {{ margin: 0; }}
  .cellule img {{ display: block; image-rendering: pixelated; }}
  figcaption {{ font-size: 7pt; line-height: 1.5; margin-top: 2mm; color: #333; }}
  @media print {{ body {{ -webkit-print-color-adjust: exact; print-color-adjust: exact; }} }}
</style>
</head>
<body>
  <h1>Planche de test de lecture</h1>
  <p class="mode">
    Imprimer cette page a l'echelle reelle, sans ajustement automatique, puis
    scanner les codes dans l'ordre A, B, C, D. Le premier code qui resiste donne
    la limite de l'appareil. Si A passe et que B resiste, le lecteur ne traite
    que les URL et non les fiches de contact : essayer Google Lens. Si A, B et C
    passent et que seul D resiste, c'est le contenu du QR qu'il faut alleger.
    Tous les codes portent le meme contact, a l'exception de A.
  </p>
  {"".join(blocks)}
</body>
</html>
"""
    target = HERE / "planche-test-scan.html"
    target.write_text(html, encoding="utf-8")
    print(target.name)
    for ident, description, data in payloads():
        _, version, modules = png_data_uri(data)
        print(f"   {ident}  version {version:2}  {modules}x{modules} modules  "
              f"{len(data.encode()):3} octets  {description}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
