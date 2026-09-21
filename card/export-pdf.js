#!/usr/bin/env node
// Exporte card.html en PDF de deux pages 85 x 55 mm, sans marge.
//
// Le navigateur est le seul moteur qui interprete la maquette exactement
// comme l'ecran. Exporter par ce chemin, plutot qu'a la main, garantit que
// le PDF remis a l'imprimeur correspond au fichier verifie.
//
//   npm install playwright
//   node export-pdf.js                                        # la carte
//   node export-pdf.js planche-test-scan.pdf planche-test-scan.html
//   python3 verify.py --pdf carte-mosaik-recto-verso.pdf

const path = require('path');

(async () => {
  let chromium;
  try {
    ({ chromium } = require('playwright'));
  } catch {
    console.error("playwright manquant. npm install playwright");
    process.exit(1);
  }

  const target = path.resolve(__dirname, process.argv[2] || 'carte-mosaik-recto-verso.pdf');
  const source = path.resolve(__dirname, process.argv[3] || 'card.html');
  // La planche de test est une page A4 ; la carte a son propre format.
  const layout = process.argv[3]
    ? { format: 'A4' }
    : { width: '85mm', height: '55mm' };

  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto('file://' + source);
  await page.emulateMedia({ media: 'print' });
  await page.pdf({
    path: target,
    ...layout,
    printBackground: true,
    ...(process.argv[3] ? {} : { margin: { top: 0, right: 0, bottom: 0, left: 0 } }),
  });
  await browser.close();
  console.log(target);
})();
