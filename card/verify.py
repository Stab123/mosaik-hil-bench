#!/usr/bin/env python3
"""Verifie que le QR genere restitue exactement la vCard attendue.

Un QR qui s'affiche n'est pas un QR qui se scanne. Ce controle decode
qr-vcard.png et compare les octets obtenus a vcard.vcf, accents compris.

Le decodeur de reference est zxing-cpp, la bibliotheque dont derivent la
plupart des applications de scan. cv2.QRCodeDetector, utilise en secours,
echoue sur les symboles de version elevee comme celui-ci : son echec ne dit
rien de la lisibilite reelle du code.

Usage:
    python3 verify.py                                     # variante de la carte
    python3 verify.py --full                              # QR incluant ADR et NOTE
    python3 verify.py --pdf carte-mosaik-recto-verso.pdf  # controle aussi le PDF
"""

from __future__ import annotations

import argparse
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent


def expected_bytes(kind: str, full: bool, accents: bool) -> bytes:
    import formats

    return formats.payload(HERE / "vcard.vcf", kind, full, accents).encode("utf-8")


def modules_side(data: bytes, ecc: str = "M") -> int:
    """Cote du symbole en modules, zone de silence exclue."""
    import qrcode
    from qrcode.constants import ERROR_CORRECT_M, ERROR_CORRECT_Q

    qr = qrcode.QRCode(
        error_correction={"M": ERROR_CORRECT_M, "Q": ERROR_CORRECT_Q}[ecc], border=4
    )
    qr.add_data(data.decode("utf-8"))
    qr.make(fit=True)
    return qr.modules_count


def decode(png: pathlib.Path) -> tuple[bytes, str]:
    """Retourne les octets decodes et le nom du decodeur utilise."""
    try:
        import cv2
    except ImportError:
        sys.exit("opencv manquant. pip install -r requirements.txt")

    image = cv2.imread(str(png), cv2.IMREAD_GRAYSCALE)
    if image is None:
        sys.exit(f"{png.name} illisible")

    try:
        import zxingcpp
    except ImportError:
        result = cv2.QRCodeDetector().detectAndDecodeBytes(image)[0]
        return bytes(result), "opencv (secours)"

    result = zxingcpp.read_barcode(image)
    return (bytes(result.bytes) if result else b""), "zxing-cpp"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--format", default="vcard3", dest="kind", help="format attendu")
    ap.add_argument("--full", action="store_true", help="QR incluant ADR et NOTE")
    ap.add_argument("--accents", action="store_true", help="QR gardant les accents")
    ap.add_argument("--png", default="qr-vcard.png")
    ap.add_argument("--pdf", help="controle aussi le PDF d'impression")
    args = ap.parse_args()

    png = HERE / args.png
    if not png.exists():
        print(f"{png.name} absent : lancer d'abord python3 generate.py", file=sys.stderr)
        return 1

    want = expected_bytes(args.kind, args.full, args.accents)
    got, decoder = decode(png)

    if not got:
        print(f"echec : aucun QR detecte par {decoder}", file=sys.stderr)
        return 1
    if got != want:
        print(f"echec : le contenu decode par {decoder} differe de vcard.vcf", file=sys.stderr)
        print(f"  decode  {len(got):3} octets", file=sys.stderr)
        print(f"  attendu {len(want):3} octets", file=sys.stderr)
        return 1

    text = got.decode("utf-8")
    props = sum(1 for l in text.split("\r\n") if l) if text.startswith("BEGIN") else 1
    print(f"ok ({decoder}) : {len(got)} octets decodes, identiques a la source")
    print(f"   {props} proprietes")
    if got.isascii():
        print("   contenu entierement ASCII : aucune ambiguite de jeu de caracteres")
    else:
        print("   ATTENTION : contenu non ASCII sans marqueur ECI, lecture "
              "dependante du decodeur")

    if args.pdf:
        import check_pdf

        pdf = HERE / args.pdf
        if not pdf.exists():
            print(f"{pdf.name} absent : lancer d'abord node export-pdf.js", file=sys.stderr)
            return 1
        print(f"controle de {pdf.name} :")
        errors = check_pdf.check(pdf, want, modules_side(want))
        if errors:
            for e in errors:
                print(f"echec : {e}", file=sys.stderr)
            return 1
        print("   les deux faces decodent et restituent la vCard")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
