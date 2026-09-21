#!/usr/bin/env python3
"""Les trois formats de contact qu'un QR peut porter.

Tous decrivent la meme personne. Ils different par ce que le lecteur en
fait, et c'est la tout l'enjeu : un telephone qui ouvre un selecteur de
contact au lieu d'une fiche a enregistrer n'a pas reconnu le format.

url      Pas une fiche mais l'adresse d'une page qui en sert une. Le seul
         mecanisme qui fonctionne quand l'appareil photo refuse de traiter
         les fiches encodees directement dans le symbole.
vcard3   RFC 2426. Le plus riche, le standard des ordinateurs et d'iOS.
vcard21  La version anterieure, encore attendue par de vieux analyseurs.
mecard   Format compact ne de l'ecosysteme mobile japonais, que les
         appareils photo Android reconnaissent souvent la ou la vCard
         passe pour du texte brut. Nettement plus court, donc moins dense.
"""

from __future__ import annotations

import pathlib

CRLF = "\r\n"

TRANSLITTERATION = {
    "Ï": "I", "ï": "i", "É": "E", "é": "e", "È": "E", "è": "e",
    "Ê": "E", "ê": "e", "À": "A", "à": "a", "Â": "A", "â": "a",
    "Ô": "O", "ô": "o", "Û": "U", "û": "u", "Ù": "U", "ù": "u",
    "Ç": "C", "ç": "c", "·": "-", "’": "'", "–": "-", "—": "-",
}

FORMATS = ("vcard3", "vcard21", "mecard", "url")

# Page publiee par GitHub Pages depuis docs/. Le QR au format "url" ne porte
# que cette adresse : le telephone ouvre une page dont le bouton reemballe la
# fiche avec le type text/vcard, ce qui declenche l'import. Ce detour existe
# parce que certains appareils photo ne routent aucune fiche vers l'ecran
# d'enregistrement, quel que soit le format encode dans le symbole.
URL_PAGE = "https://stab123.github.io/mosaik-hil-bench/"


def to_ascii(text: str) -> str:
    return text.translate(str.maketrans(TRANSLITTERATION))


def read_fields(path: pathlib.Path) -> dict[str, str]:
    """Extrait les champs de vcard.vcf, seule source de verite."""
    fields: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or ":" not in line:
            continue
        head, value = line.split(":", 1)
        name = head.split(";", 1)[0].upper()
        if name == "N":
            parts = value.split(";")
            fields["last"] = parts[0]
            fields["first"] = parts[1] if len(parts) > 1 else ""
        elif name == "ADR":
            parts = value.split(";")
            fields["street"] = parts[2] if len(parts) > 2 else ""
            fields["city"] = parts[3] if len(parts) > 3 else ""
            fields["zip"] = parts[5] if len(parts) > 5 else ""
            fields["country"] = parts[6] if len(parts) > 6 else ""
        elif name in ("FN", "TITLE", "ORG", "TEL", "EMAIL", "URL", "NOTE"):
            fields[name.lower()] = value
    return fields


def _escape_mecard(value: str) -> str:
    """MECARD reserve \\ ; : et la virgule."""
    for char in ("\\", ";", ":", ","):
        value = value.replace(char, "\\" + char)
    return value


def build(fields: dict[str, str], kind: str, full: bool, url: str = URL_PAGE) -> str:
    """Assemble la charge utile. `full` ajoute l'adresse postale et la note."""
    if kind not in FORMATS:
        raise ValueError(f"format inconnu : {kind}")

    if kind == "url":
        return url

    if kind == "mecard":
        # MECARD n'a ni ORG ni TITLE : ils rejoignent la note, sans quoi
        # l'appartenance a MOSAIK disparaitrait de la fiche enregistree.
        note_parts = [p for p in (fields.get("title"), fields.get("org")) if p]
        if full and fields.get("note"):
            note_parts.append(fields["note"])
        items = [
            f"N:{_escape_mecard(fields['last'])},{_escape_mecard(fields['first'])}",
            f"TEL:{fields['tel']}",
            f"EMAIL:{fields['email']}",
            f"URL:{fields['url']}",
        ]
        if full and fields.get("street"):
            adr = ",".join(
                [fields.get("street", ""), fields.get("city", ""), "",
                 fields.get("zip", ""), fields.get("country", "")]
            )
            items.append("ADR:" + _escape_mecard(adr))
        if note_parts:
            items.append("NOTE:" + _escape_mecard(" - ".join(note_parts)))
        return "MECARD:" + ";".join(items) + ";;"

    version = "3.0" if kind == "vcard3" else "2.1"
    tel_param = "TYPE=CELL" if kind == "vcard3" else "CELL"
    mail_param = "TYPE=INTERNET" if kind == "vcard3" else "INTERNET"

    lines = [
        "BEGIN:VCARD",
        f"VERSION:{version}",
        f"N:{fields['last']};{fields['first']};;;",
        f"FN:{fields['fn']}",
        f"TITLE:{fields['title']}",
        f"ORG:{fields['org']}",
        f"TEL;{tel_param}:{fields['tel']}",
        f"EMAIL;{mail_param}:{fields['email']}",
        f"URL:{fields['url']}",
    ]
    if full:
        if fields.get("street"):
            adr_param = "TYPE=WORK" if kind == "vcard3" else "WORK"
            lines.append(
                f"ADR;{adr_param}:;;{fields['street']};{fields['city']};;"
                f"{fields['zip']};{fields['country']}"
            )
        if fields.get("note"):
            lines.append(f"NOTE:{fields['note']}")
    lines.append("END:VCARD")
    return CRLF.join(lines) + CRLF


def payload(
    path: pathlib.Path, kind: str, full: bool, accents: bool, url: str = URL_PAGE
) -> str:
    data = build(read_fields(path), kind, full, url)
    if not accents:
        data = to_ascii(data)
        if not data.isascii():
            restants = sorted({c for c in data if not c.isascii()})
            raise SystemExit(
                "caracteres non ASCII sans equivalent : " + " ".join(restants)
                + "\nles ajouter a TRANSLITTERATION, ou passer --accents"
            )
    return data
