# Carte de visite MOSAÏK

Carte de visite au format européen normalisé 85 × 55 mm, QR code vCard intégré,
généré et vérifié localement.

## Contenu

| Fichier | Rôle |
| --- | --- |
| `vcard.vcf` | Source de vérité des coordonnées. Tout le reste en découle. |
| `formats.py` | Les formats portés par le QR : vCard 3.0, vCard 2.1, MECARD, URL. |
| `generate.py` | Produit le QR vectoriel et matriciel, puis assemble `card.html`. |
| `verify.py` | Décode le QR produit et le compare à la source, image et PDF. |
| `check_pdf.py` | Décode chaque page du PDF et mesure la taille de module réelle. |
| `export-pdf.js` | Exporte une page HTML en PDF, à l'octet près reproductible. |
| `test-scan.py` | Planche de diagnostic pour trouver la limite d'un téléphone. |
| `card.template.html` | Maquette recto/verso. `<!--QR_IMG-->` reçoit le symbole. |
| `card.html` | Carte générée, QR embarqué en PNG. Aucune ressource externe. |
| `carte-mosaik-recto-verso.pdf` | PDF d'impression, deux pages sans marge. |
| `qr-vcard.svg` | QR vectoriel, si l'imprimeur préfère du vectoriel. |
| `qr-vcard.png` | QR matriciel haute résolution. |

## Utilisation

```sh
pip install -r requirements.txt
npm install playwright

python3 generate.py                                   # QR + card.html
node export-pdf.js                                    # le PDF d'impression
python3 verify.py --pdf carte-mosaik-recto-verso.pdf  # contrôle de bout en bout
```

Options :

```sh
python3 generate.py --format url      # le QR porte l'adresse de la page publiée
python3 generate.py --format mecard   # format compact, mieux reconnu sur Android
python3 generate.py --format vcard21  # vCard 2.1, pour analyseurs anciens
python3 generate.py --full            # inclut ADR et NOTE dans le QR
python3 generate.py --accents         # garde les accents dans le QR
python3 generate.py --ecc Q           # correction d'erreur supérieure
```

## Ce que contient le QR

Le QR porte le nom, la fonction, l'organisation, le téléphone, le courriel et
l'adresse du dépôt. Il ne porte ni l'adresse postale ni la mention des
affiliations, toutes deux imprimées en clair sur la carte. Les y remettre fait
passer le symbole de la version 11 à la version 14, soit de 61 à 73 modules,
et dégrade la lisibilité au scan sans rien apporter au lecteur. `--full` rétablit
le contenu intégral si le besoin s'en fait sentir.

## Quand aucun format encodé ne déclenche l'enregistrement

Si ni la vCard ni MECARD n'ouvrent de fiche à enregistrer, le problème n'est
plus la charge utile : cet appareil photo ne route aucune fiche vers l'écran
d'enregistrement, quel que soit le format encodé dans le symbole. Aucune
modification du QR n'y changera quoi que ce soit.

Le seul mécanisme qui fonctionne alors est celui des cartes de visite
numériques commerciales : le QR ne porte pas la fiche, il porte l'adresse d'une
page qui en sert une. Scanner une adresse, tous les appareils photo savent le
faire.

```sh
python3 generate.py --format url
```

Le QR tombe à 43 octets, version 4, 33 modules, soit 0,707 mm par module : près
de deux fois plus gros que celui de la vCard, et d'autant plus facile à lire.

**Mise en ligne.** Le dossier `site/` à la racine du dépôt contient la page et
la fiche. Deux conditions, toutes deux nécessaires :

1. Dans les réglages GitHub du dépôt, section Pages, choisir comme source
   « GitHub Actions ». Le workflow `.github/workflows/pages.yml` fait le reste.
2. Fusionner la branche de travail dans `main`. Le workflow ne publie que
   depuis `main` : tant que `site/` n'y est pas, il n'y a rien à servir.

La page est ensuite servie à l'adresse inscrite dans `formats.py` sous
`URL_PAGE`. Tant que ces deux conditions ne sont pas remplies, un QR au format
`url` mène à une page inexistante.

Le workflow contrôle au passage que `site/contact.vcf` n'a pas dévié de
`card/vcard.vcf` et que la fiche porte bien ses champs essentiels. Une fiche
désynchronisée fait échouer la publication plutôt que de mettre en ligne des
coordonnées périmées.

**Ce que cela publie.** Le téléphone, le courriel et l'adresse postale
deviennent accessibles à une adresse publique. Ce sont les mêmes informations
que celles imprimées sur la carte, mais elles passent d'un support remis en main
propre à une page indexable. C'est une décision à prendre en connaissance de
cause.

**Comment la page procède.** Le bouton lit `contact.vcf`, réemballe son contenu
avec le type `text/vcard` et le remet au navigateur sous le nom `Sami-Bey.vcf`.
Ce réemballage garantit le bon type quel que soit celui que le serveur annonce,
et c'est ce type qui déclenche l'import dans le carnet d'adresses. La fiche
n'est pas recopiée dans la page : `generate.py` réaligne `site/contact.vcf` sur
`vcard.vcf` à chaque exécution.

## Quand le téléphone ouvre un sélecteur au lieu d'une fiche

Symptôme : le code est bien détecté, mais appuyer sur « Ajouter un contact »
ouvre la liste des contacts existants au lieu d'une fiche pré-remplie avec un
bouton d'enregistrement. Le lecteur a reconnu un numéro de téléphone, pas une
fiche : il propose donc de rattacher ce numéro à quelqu'un.

La vCard produite est pourtant valide, `vobject` la relit champ par champ. Le
problème est côté lecteur : tous ne routent pas une vCard vers le formulaire
d'enregistrement.

Trois formats sont donc disponibles, portant le même contact.

| Format | Octets | Version | Modules | Remarque |
| --- | ---: | ---: | ---: | --- |
| `vcard3` | 238 | 11 | 61 | RFC 2426, le standard, défaut |
| `vcard21` | 228 | 11 | 61 | version antérieure, analyseurs anciens |
| `mecard` | 160 | 9 | 53 | né du mobile, souvent mieux reconnu sur Android |
| `url` | 43 | 4 | 33 | pas une fiche, l'adresse d'une page qui en sert une |

MECARD est à la fois le mieux reconnu par les appareils photo Android et le
moins dense, d'où un symbole nettement plus lisible. Il n'a en revanche ni
champ organisation ni champ fonction : les deux rejoignent la note de la fiche.

La planche de diagnostic ci-dessous tranche en une minute quel format ce
téléphone-là accepte.

## Le QR est en ASCII, pas en UTF-8

Un QR en mode octet ne déclare pas son jeu de caractères. La norme suppose
ISO-8859-1, et un décodeur qui s'y tient lit `MOSAÃK` là où UTF-8 écrivait
`MOSAÏK`. Un marqueur ECI lèverait l'ambiguïté, mais tous les lecteurs ne le
traitent pas. Le contenu du QR est donc translittéré : `MOSAIK`, `Ingenieur`,
et le séparateur `·` devient un tiret.

La carte imprimée garde ses accents : ils ne passent pas par le symbole. Seule
la fiche enregistrée dans le téléphone les perd, en échange d'une lecture
identique sur tous les appareils. `verify.py` signale tout contenu non ASCII.

## Densité et lisibilité à l'impression

Valeurs mesurées dans le PDF par `check_pdf.py`, pas déduites de la feuille de
style. La règle usuelle demande 0,40 mm par module pour un scan fiable en
lumière médiocre, et descend difficilement sous 0,33 mm.

| Face | Octets | Version | Symbole | mm par module | |
| --- | ---: | ---: | ---: | ---: | --- |
| Recto | 238 | 11 | 25,8 mm | 0,422 | ok |
| Verso | 238 | 11 | 30,2 mm | 0,494 | ok |
| Recto, `--full` | 347 | 14 | 26,2 mm | 0,360 | limite |
| Verso, `--full` | 347 | 14 | 30,8 mm | 0,421 | ok |

La configuration par défaut passe le seuil sur les deux faces. C'est le produit
de deux décisions : sortir l'adresse postale et la note du QR, et porter le
carré du recto de 30 à 32 mm.

## Quand un téléphone refuse de scanner

`test-scan.py` produit une page A4 qui pose deux questions distinctes.

```sh
python3 test-scan.py
node export-pdf.js planche-test-scan.pdf planche-test-scan.html
```

Imprimer à l'échelle réelle, sans ajustement automatique.

**Rangée 1, le format.** Trois codes de 40 mm, même contact, formats différents.
On cherche celui qui ouvre une fiche pré-remplie avec un bouton
d'enregistrement. Un code qui ouvre un sélecteur de contact n'a pas été reconnu
comme une fiche. Si aucun des trois ne donne de fiche, c'est l'appareil photo
qui ne traite pas les contacts, et Google Lens prend le relais.

**Rangée 2, la densité.** Quatre codes de 25 mm, de l'URL seule au contact
complet, à taille réduite pour mettre le lecteur en difficulté. Le premier qui
résiste situe la limite. Si même le code A résiste, le problème est la mise au
point, l'éclairage ou un reflet.

Scanner un écran est toujours plus difficile que scanner un tirage papier, à
cause du rétroéclairage et du moiré. Un téléphone ne peut évidemment pas scanner
son propre écran.

## Aucun générateur en ligne

Coller une vCard dans un générateur web transmet un numéro de téléphone, une
adresse électronique et une adresse postale à un tiers, à chaque rendu si le QR
est chargé depuis une URL d'API. `card.html` embarque le symbole dans le fichier
lui-même : la page ne fait aucune requête réseau et reste lisible hors ligne.

## Pièges du rendu paginé

Trois défauts n'apparaissaient qu'à l'impression, l'aperçu écran étant correct.
Tous sont désormais verrouillés par `check_pdf.py`, et les règles concernées
portent un commentaire expliquant pourquoi elles sont écrites ainsi.

**Le centrage par `transform`.** Le QR du recto était centré par `top: 50%` et
`translateY(-50%)`. À l'export PDF, Chromium peignait le carré blanc deux fois,
à la position transformée et à la position d'origine, et abandonnait le tracé du
symbole : la carte sortait avec un rectangle blanc vide. La cote est maintenant
fixée en millimètres, sans transformation.

**Le tracé vectoriel trop lourd.** Le symbole était embarqué en SVG inline. Un
QR de version 14 compte plusieurs milliers de sous-chemins, et un lecteur PDF
peut renoncer à peindre un tracé aussi lourd, surtout découpé par un coin
arrondi. La carte embarque donc une image, que tout lecteur sait afficher. À
29 mm, ce PNG imprime à 1450 points par pouce, bien au-delà de ce que demande
l'offset.

**La compression par le conteneur flex.** Le QR du verso était déclaré à 34 mm
mais le conteneur le ramenait à 25 mm, faute de `flex-shrink: 0`, soit une
densité pire que celle du recto alors que le verso existe pour la corriger.

Le PDF est contrôlé à 96, 150, 300 et 600 points par pouce : les deux faces
décodent à chaque résolution, y compris celles d'un lecteur de téléphone.

La leçon tient en une ligne : sur un imprimé, seul le PDF fait foi.

## Export reproductible

Chromium date chaque export, si bien qu'un PDF régénéré différait du précédent
par quatre octets alors que son contenu était identique. Le fichier étant suivi
par git, chaque exécution produisait un faux diff. `export-pdf.js` fige donc
l'horodatage après l'export. Deux exports successifs donnent maintenant le même
fichier au bit près, et un diff sur le PDF signale un vrai changement.

## Vérification

`verify.py` décode l'image produite et la compare à la vCard attendue. Avec
`--pdf`, il rastérise en plus chaque page du PDF à 1200 dpi, décode le symbole
et mesure sa taille réelle sur le papier à partir de sa position rendue. Le
décodeur est zxing-cpp, la bibliothèque dont dérivent la plupart des
applications de scan.

À noter : `cv2.QRCodeDetector` d'OpenCV ne détecte pas ces symboles, alors que
zxing-cpp les lit au bit près. Cette faiblesse connue d'OpenCV sur les versions
élevées ne dit rien de la lisibilité réelle. Le script ne s'en sert qu'en
secours, et le signale.

## Encodage

La vCard est émise en mode octet avec des fins de ligne CRLF, comme l'exige la
RFC 2426. Le décodage restitue les octets à l'identique.

## Au scan

Sur iOS, l'appareil photo affiche une bannière de contact ; un appui ouvre la
fiche pré-remplie avec l'option d'enregistrement. Sur Android, l'appareil photo
ou Google Lens propose d'ajouter le contact. Certains appareils photo constructeur
ne reconnaissent que les URL : voir la planche de diagnostic plus haut.
