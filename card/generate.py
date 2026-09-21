#!/usr/bin/env python3
"""Genere le QR code vCard et la carte de visite MOSAIK.

Lit vcard.vcf, ecrit un QR vectoriel (SVG) et matriciel (PNG), puis injecte le
SVG directement dans card.html a partir de card.template.html. Aucune donnee
personnelle ne quitte la machine : pas d'appel a un generateur en ligne.

Usage:
    python3 generate.py                # vCard complete, ECC M
    python3 generate.py --compact      # sans ADR ni NOTE (QR moins dense)
    python3 generate.py --ecc Q        # correction d'erreur superieure
"""

from __future__ import annotations

import argparse
import base64
import io
import pathlib
import sys

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


# Un QR en mode octet n'indique pas son jeu de caracteres : la norme suppose
# ISO-8859-1, et un decodeur qui s'y tient lit "MOSAÃK" la ou UTF-8 ecrivait
# "MOSAÏK". zxing et les scanners courants devinent bien, mais rien ne
# l'impose. Le contenu du QR est donc translittere par defaut. La carte
# imprimee, elle, garde ses accents : ils ne passent pas par le symbole.
TRANSLITTERATION = {
    "Ï": "I", "ï": "i", "É": "E", "é": "e", "È": "E", "è": "e",
    "Ê": "E", "ê": "e", "À": "A", "à": "a", "Â": "A", "â": "a",
    "Ô": "O", "ô": "o", "Û": "U", "û": "u", "Ù": "U", "ù": "u",
    "Ç": "C", "ç": "c", "·": "-", "’": "'", "–": "-", "—": "-",
}


def to_ascii(text: str) -> str:
    return text.translate(str.maketrans(TRANSLITTERATION))


def read_vcard(path: pathlib.Path, full: bool, accents: bool) -> str:
    """Retourne la vCard normalisee en CRLF, comme l'exige la RFC 2426."""
    lines = [l for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
    if not full:
        lines = [l for l in lines if not l.startswith(("ADR", "NOTE"))]
    data = "\r\n".join(lines) + "\r\n"
    if not accents:
        data = to_ascii(data)
    if not accents and not data.isascii():
        restants = sorted({c for c in data if not c.isascii()})
        raise SystemExit(
            "caracteres non ASCII sans equivalent : " + " ".join(restants)
            + "\nles ajouter a TRANSLITTERATION, ou passer --accents"
        )
    return data


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
        f"{label:9} {len(data.encode('utf-8')):3} octets  ECC {ecc}  "
        f"version {qr.version:2}  {qr.modules_count}x{qr.modules_count} modules  "
        f"{mm:.3f} mm/module a {QR_SVG_MM:.0f} mm  ->  {verdict}"
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
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
    data = read_vcard(source, args.full, args.accents)
    qr = build_qr(data, args.ecc, args.border)
    retenue = "complete" if args.full else "carte"
    report(retenue, data, qr, args.ecc)

    # Variante non retenue, a titre de comparaison.
    other = read_vcard(source, not args.full, args.accents)
    report(
        "carte" if args.full else "complete",
        other,
        build_qr(other, args.ecc, args.border),
        args.ecc,
    )

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

    template = (HERE / "card.template.html").read_text(encoding="utf-8")
    (HERE / "card.html").write_text(template.replace("<!--QR_IMG-->", tag), encoding="utf-8")

    dpi_recto = px / (QR_SVG_MM / 25.4)
    print(f"\nqr-vcard.svg   vectoriel, pour l'imprimeur")
    print(f"qr-vcard.png   {px}x{px} px, soit {dpi_recto:.0f} dpi a {QR_SVG_MM:.0f} mm")
    print(f"card.html      carte 85x55 mm, QR embarque en PNG")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
