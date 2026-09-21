# Carte de visite MOSAÏK

Carte de visite au format européen normalisé 85 × 55 mm, QR code vCard intégré,
générée et vérifiée localement.

## Contenu

| Fichier | Rôle |
| --- | --- |
| `vcard.vcf` | Source de vérité des coordonnées. Tout le reste en découle. |
| `generate.py` | Produit le QR vectoriel et matriciel, puis assemble `card.html`. |
| `verify.py` | Décode le QR produit et compare les octets à `vcard.vcf`. |
| `card.template.html` | Maquette recto/verso. `<!--QR_SVG-->` reçoit le symbole. |
| `card.html` | Carte générée, QR inclus en SVG inline. Aucune ressource externe. |
| `qr-vcard.svg` | QR vectoriel, à remettre à l'imprimeur. |
| `qr-vcard.png` | QR matriciel 1944 × 1944 px. |

## Utilisation

```sh
pip install -r requirements.txt
python3 generate.py     # écrit qr-vcard.svg, qr-vcard.png, card.html
python3 verify.py       # contrôle que le QR restitue bien la vCard
```

Ouvrir `card.html` dans un navigateur pour l'aperçu. Imprimer ou exporter en PDF
donne directement deux pages de 85 × 55 mm, sans marge : le recto sombre avec le
QR à droite, le verso clair avec un QR agrandi.

Options :

```sh
python3 generate.py --compact   # retire ADR et NOTE, symbole moins dense
python3 generate.py --ecc Q     # correction d'erreur supérieure
```

## Aucun générateur en ligne

Coller une vCard dans un générateur web transmet un numéro de téléphone, une
adresse électronique et une adresse postale à un tiers, à chaque rendu si le QR
est chargé depuis une URL d'API. `card.html` embarque le symbole en SVG inline :
la page ne fait aucune requête réseau, et le fichier reste lisible hors ligne.

## Densité et lisibilité à l'impression

Le symbole occupe 27 mm de côté sur la carte, à l'intérieur d'un carré blanc de
30 mm dont la marge prolonge la zone de silence.

| Variante | Octets | Version | Modules | mm par module |
| --- | ---: | ---: | ---: | ---: |
| Complète, ECC M | 350 | 14 | 73 × 73 | 0,333 |
| Compacte, ECC M | 241 | 11 | 61 × 61 | 0,391 |

La règle usuelle demande 0,40 mm par module pour un scan fiable en lumière
médiocre, et descend difficilement sous 0,33 mm. Les deux variantes sont donc
dans la zone limite : elles supposent un offset soigné sur papier mat, pas une
impression jet d'encre ni un papier brillant.

Trois leviers, par ordre d'efficacité :

1. Utiliser le verso, où le QR passe à 34 mm, soit 0,42 mm par module pour la
   vCard complète. C'est la seule variante qui sort de la zone limite.
2. Passer en `--compact`. L'adresse postale et la mention des affiliations sont
   déjà imprimées en clair sur la carte ; les retirer du QR fait tomber le
   symbole de la version 14 à la version 11.
3. Demander un tirage d'essai à l'imprimeur et le scanner avant de lancer le
   tirage complet.

Le niveau de correction reste M. Passer à Q ajoute de la redondance mais fait
grimper le symbole à la version 17, ce qui réduit la taille de module et dégrade
le résultat net sur une surface aussi petite.

## Vérification

`verify.py` décode l'image produite et compare les octets à `vcard.vcf`. Le
décodeur est zxing-cpp, la bibliothèque dont dérivent la plupart des
applications de scan.

À noter : `cv2.QRCodeDetector` d'OpenCV ne détecte pas ce symbole, alors que
zxing-cpp le lit au bit près, y compris capturé depuis le rendu de la page. La
faiblesse connue d'OpenCV sur les versions élevées ne dit rien de la lisibilité
réelle du code. Le script ne s'en sert qu'en secours, et le signale.

## Encodage

La vCard est émise en UTF-8, en mode octet, avec des fins de ligne CRLF comme
l'exige la RFC 2426. Les caractères accentués de `MOSAÏK`, `Ingénieur` et du
séparateur `·` sont préservés : le décodage restitue les 350 octets à
l'identique.

## Au scan

Sur iOS, l'appareil photo affiche une bannière de contact ; un appui ouvre la
fiche pré-remplie avec l'option d'enregistrement. Sur Android, l'appareil photo
ou Google Lens propose d'ajouter le contact. Aucune application tierce n'est
nécessaire sur les deux plateformes.
