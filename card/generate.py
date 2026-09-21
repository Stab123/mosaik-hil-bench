#!/usr/bin/env python3
"""Genere le QR code de contact et la carte de visite MOSAIK.

Lit vcard.vcf, ecrit un QR vectoriel (SVG) et matriciel (PNG), puis assemble
card.html a partir de card.template.html. Aucune donnee personnelle ne quitte
la machine : pas d'appel a un generateur en ligne.

Usage:
    python3 generate.py                   # vCard 3.0, ASCII, sans ADR ni NOTE
    python3 generate.py --format mecard   # format compact, mieux reconnu
    python3 generate.py --full            # ajoute l'adresse postale et la note
    python3 generate.py --accents         # garde les accents dans le QR
    python3 generate.py --ecc Q           # correction d'erreur superieure
"""

from __future__ import annotations

import argparse
import base64
import io
import pathlib
import sys

import formats

try:
    import qrcode
    from qrcode.constants import (
        ERROR_CORRECT_L,
        ERROR_CORRECT_M,
        ERROR_CORRECT_Q,
        ERROR_CORRECT_H,
    )
except ImportError:  # pragma: no cover
    sys.exit("qrcode manquant. Installer avec : pip install 'qrcode[pil]'")

HERE = pathlib.Path(__file__).resolve().parent

ECC = {
    "L": (ERROR_CORRECT_L, 7),
    "M": (ERROR_CORRECT_M, 15),
    "Q": (ERROR_CORRECT_Q, 25),
    "H": (ERROR_CORRECT_H, 30),
}

# Taille de module minimale recommandee a l'impression, en millimetres.
# En dessous, une camera de smartphone decroche des que l'eclairage faiblit.
MODULE_MM_SAFE = 0.40
MODULE_MM_FLOOR = 0.33

# Cote utile du symbole sur la carte, en millimetres : le carre blanc fait
# 32 mm avec 1,5 mm de marge, soit 29 mm de modules.
QR_SVG_MM = 29.0


def build_qr(data: str, ecc: str, border: int):
    level, _ = ECC[ecc]
    qr = qrcode.QRCode(version=None, error_correction=level, box_size=10, border=border)
    qr.add_data(data)
    qr.make(fit=True)
    return qr


def matrix_to_svg(matrix, dark: str, light: str | None) -> str:
    """Rend la matrice en un seul chemin SVG, sans filets blancs entre modules."""
    n = len(matrix)
    runs: list[str] = []
    for y, row in enumerate(matrix):
        x = 0
        while x < n:
            if row[x]:
                start = x
                while x < n and row[x]:
                    x += 1
                runs.append(f"M{start} {y}h{x - start}v1h-{x - start}z")
            else:
                x += 1
    bg = f'<rect width="{n}" height="{n}" fill="{light}"/>' if light else ""
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {n} {n}" '
        f'shape-rendering="crispEdges" role="img" '
        f'aria-label="QR code vCard Sami Bey">'
        f'{bg}<path fill="{dark}" d="{"".join(runs)}"/></svg>'
    )


def report(label: str, data: str, qr, ecc: str) -> None:
    n = qr.modules_count + 2 * qr.border
    mm = QR_SVG_MM / n
    if mm >= MODULE_MM_SAFE:
        verdict = "OK"
    elif mm >= MODULE_MM_FLOOR:
        verdict = "LIMITE (offset soigne uniquement)"
    else:
        verdict = "TROP DENSE pour une carte de visite"
    print(
        f"{label:16} {len(data.encode('utf-8')):3} octets  ECC {ecc}  "
        f"version {qr.version:2}  {qr.modules_count}x{qr.modules_count} modules  "
        f"{mm:.3f} mm/module a {QR_SVG_MM:.0f} mm  ->  {verdict}"
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--format", choices=formats.FORMATS, default="vcard3", dest="kind",
        help="format de contact porte par le QR",
    )
    ap.add_argument(
        "--url", default=formats.URL_PAGE,
        help="adresse portee par le QR quand --format url est choisi",
    )
    ap.add_argument(
        "--full", action="store_true",
        help="inclut ADR et NOTE dans le QR (symbole nettement plus dense)",
    )
    ap.add_argument(
        "--accents", action="store_true",
        help="garde les accents dans le QR au lieu de les translitterer",
    )
    ap.add_argument("--ecc", choices=sorted(ECC), default="M", help="niveau de correction")
    ap.add_argument("--border", type=int, default=4, help="zone de silence en modules")
    ap.add_argument("--png-scale", type=int, default=24, help="pixels par module du PNG")
    args = ap.parse_args()

    source = HERE / "vcard.vcf"
    data = formats.payload(source, args.kind, args.full, args.accents, args.url)
    qr = build_qr(data, args.ecc, args.border)
    report(f"{args.kind} retenu", data, qr, args.ecc)

    # Les autres formats, a titre de comparaison.
    for kind in formats.FORMATS:
        if kind == args.kind:
            continue
        other = formats.payload(source, kind, args.full, args.accents, args.url)
        report(kind, other, build_qr(other, args.ecc, args.border), args.ecc)

    matrix = qr.get_matrix()
    # Le SVG reste le livrable destine a l'imprimeur.
    (HERE / "qr-vcard.svg").write_text(
        matrix_to_svg(matrix, dark="#000000", light="#ffffff"), encoding="utf-8"
    )

    img = qr.make_image(fill_color="black", back_color="white")
    px = (qr.modules_count + 2 * args.border) * args.png_scale
    img = img.resize((px, px))
    img.save(HERE / "qr-vcard.png")

    # La carte embarque le PNG, pas le SVG. Un symbole de version 14 fait
    # plusieurs milliers de sous-chemins : certains lecteurs PDF renoncent a
    # peindre un trace aussi lourd, surtout decoupe par un coin arrondi, et la
    # carte sort avec un carre blanc vide. Une image, tout lecteur sait
    # l'afficher. A 27 mm, ce PNG imprime a plus de 1800 points par pouce.
    buffer = io.BytesIO()
    img.save(buffer, format="PNG", optimize=True)
    data_uri = "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()
    tag = f'<img src="{data_uri}" alt="QR code vCard Sami Bey">'

    publie = HERE.parent / "site" / "contact.vcf"
    if publie.parent.exists():
        source_texte = source.read_text(encoding="utf-8")
        if publie.read_text(encoding="utf-8") != source_texte:
            publie.write_text(source_texte, encoding="utf-8")
            print(f"site/contact.vcf realigne sur {source.name}")

    template = (HERE / "card.template.html").read_text(encoding="utf-8")
    (HERE / "card.html").write_text(template.replace("<!--QR_IMG-->", tag), encoding="utf-8")

    dpi_recto = px / (QR_SVG_MM / 25.4)
    print(f"\nqr-vcard.svg   vectoriel, pour l'imprimeur")
    print(f"qr-vcard.png   {px}x{px} px, soit {dpi_recto:.0f} dpi a {QR_SVG_MM:.0f} mm")
    print(f"card.html      carte 85x55 mm, QR embarque en PNG")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
