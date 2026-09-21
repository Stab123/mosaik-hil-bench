#!/usr/bin/env python3
"""Planche de diagnostic a imprimer, pour un telephone qui refuse le contact.

Deux questions, deux rangees.

Rangee 1 : quel format votre telephone transforme-t-il en fiche a
enregistrer ? Les trois codes portent le meme contact, seul le format
change. Un telephone qui ouvre un selecteur de contact, ou qui propose
d'ajouter un numero a une fiche existante, n'a pas reconnu le format.

Rangee 2 : jusqu'a quelle densite votre telephone lit-il ? Les quatre
codes vont de l'URL seule au contact complet, a taille d'impression
reduite pour mettre le lecteur en difficulte.

    python3 test-scan.py
    node export-pdf.js planche-test-scan.pdf planche-test-scan.html
"""

from __future__ import annotations

import base64
import io
import pathlib

import qrcode
from qrcode.constants import ERROR_CORRECT_M

import formats

HERE = pathlib.Path(__file__).resolve().parent
SOURCE = HERE / "vcard.vcf"

TAILLE_FORMAT_MM = 40
TAILLE_DENSITE_MM = 25


def encode(data: str) -> tuple[str, int, int]:
    qr = qrcode.QRCode(error_correction=ERROR_CORRECT_M, border=4, box_size=20)
    qr.add_data(data)
    qr.make(fit=True)
    buffer = io.BytesIO()
    qr.make_image(fill_color="black", back_color="white").save(
        buffer, format="PNG", optimize=True
    )
    uri = "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()
    return uri, qr.version, qr.modules_count


def cellule(ident: str, titre: str, data: str, size: int) -> str:
    uri, version, modules = encode(data)
    mm_par_module = size / (modules + 8)
    return f"""
      <figure class="cellule">
        <img src="{uri}" alt="code {ident}" style="width:{size}mm;height:{size}mm">
        <figcaption>
          <strong>{ident}</strong> &nbsp; {titre}<br>
          version {version}, {modules} modules<br>
          {len(data.encode()):d} octets, {mm_par_module:.3f} mm par module
        </figcaption>
      </figure>"""


def main() -> int:
    par_format = [
        ("1", "vCard 3.0", formats.payload(SOURCE, "vcard3", False, False)),
        ("2", "vCard 2.1", formats.payload(SOURCE, "vcard21", False, False)),
        ("3", "MECARD", formats.payload(SOURCE, "mecard", False, False)),
    ]
    par_densite = [
        ("A", "URL seule", "https://github.com/Stab123/mosaik-hil-bench"),
        ("B", "MECARD", formats.payload(SOURCE, "mecard", False, False)),
        ("C", "vCard de la carte", formats.payload(SOURCE, "vcard3", False, False)),
        ("D", "vCard complete", formats.payload(SOURCE, "vcard3", True, False)),
    ]

    rangee1 = "".join(cellule(i, t, d, TAILLE_FORMAT_MM) for i, t, d in par_format)
    rangee2 = "".join(cellule(i, t, d, TAILLE_DENSITE_MM) for i, t, d in par_densite)

    html = f"""<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<title>Planche de diagnostic — MOSAÏK</title>
<style>
  @page {{ size: A4; margin: 12mm; }}
  body {{
    font-family: "Helvetica Neue", Helvetica, Arial, sans-serif;
    color: #111; margin: 0; font-size: 9pt; line-height: 1.5;
  }}
  h1 {{ font-size: 15pt; margin: 0 0 2mm; }}
  h2 {{ font-size: 10.5pt; margin: 8mm 0 1mm; }}
  p {{ margin: 0 0 3mm; max-width: 180mm; }}
  .rangee {{ display: flex; gap: 8mm; align-items: flex-start; margin-top: 3mm; }}
  .cellule {{ margin: 0; }}
  .cellule img {{ display: block; image-rendering: pixelated; }}
  figcaption {{ font-size: 7pt; line-height: 1.45; margin-top: 2mm; }}
  .lecture {{ border-left: 2px solid #3b82f6; padding-left: 4mm; margin-top: 4mm; }}
  .lecture li {{ margin-bottom: 1.5mm; }}
  ul {{ margin: 0; padding-left: 4mm; }}
  @media print {{ body {{ -webkit-print-color-adjust: exact; print-color-adjust: exact; }} }}
</style>
</head>
<body>
  <h1>Planche de diagnostic</h1>
  <p>
    Imprimer a l'echelle reelle, sans ajustement automatique, puis scanner les
    codes dans l'ordre. Tous portent le meme contact, sauf le code A.
  </p>

  <h2>1. Quel format votre telephone reconnait-il ?</h2>
  <p>
    Meme contact, meme taille : seul le format change. On cherche celui qui
    ouvre une fiche pre-remplie avec un bouton d'enregistrement.
  </p>
  <div class="rangee">{rangee1}</div>
  <div class="lecture">
    <ul>
      <li>Un code ouvre une fiche a enregistrer : c'est le format a retenir,
          via <code>python3 generate.py --format ...</code></li>
      <li>Un code ouvre un selecteur de contact, ou propose d'ajouter un numero
          a une fiche existante : le format n'a pas ete reconnu, le lecteur n'a
          retenu que le telephone.</li>
      <li>Aucun des trois ne donne de fiche : l'appareil photo du telephone ne
          traite pas les contacts. Google Lens le fait.</li>
    </ul>
  </div>

  <h2>2. Jusqu'a quelle densite votre telephone lit-il ?</h2>
  <p>
    Taille reduite a {TAILLE_DENSITE_MM} mm, du plus leger au plus dense, pour
    trouver le point de decrochage.
  </p>
  <div class="rangee">{rangee2}</div>
  <div class="lecture">
    <ul>
      <li>Tous passent : la densite n'est pas en cause.</li>
      <li>A et B passent, C ou D resistent : il faut alleger le contenu du QR
          ou agrandir le symbole.</li>
      <li>Meme A resiste : le probleme est la mise au point, l'eclairage ou un
          reflet, pas le code.</li>
    </ul>
  </div>
</body>
</html>
"""
    target = HERE / "planche-test-scan.html"
    target.write_text(html, encoding="utf-8")
    print(target.name)
    for ident, titre, data in par_format + par_densite:
        _, version, modules = encode(data)
        print(f"   {ident}  {titre:20} version {version:2}  {modules} modules  "
              f"{len(data.encode()):3} octets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
