#!/usr/bin/env node
// Exporte card.html en PDF de deux pages 85 x 55 mm, sans marge.
//
// Le navigateur est le seul moteur qui interprete la maquette exactement
// comme l'ecran. Exporter par ce chemin, plutot qu'a la main, garantit que
// le PDF remis a l'imprimeur correspond au fichier verifie.
//
//   npm install playwright && node export-pdf.js
//   python3 verify.py --pdf carte-85x55.pdf

const path = require('path');

(async () => {
  let chromium;
  try {
    ({ chromium } = require('playwright'));
  } catch {
    console.error("playwright manquant. npm install playwright");
    process.exit(1);
  }

  const source = path.resolve(__dirname, 'card.html');
  const target = path.resolve(__dirname, process.argv[2] || 'carte-85x55.pdf');

  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto('file://' + source);
  await page.emulateMedia({ media: 'print' });
  await page.pdf({
    path: target,
    width: '85mm',
    height: '55mm',
    printBackground: true,
    margin: { top: 0, right: 0, bottom: 0, left: 0 },
  });
  await browser.close();
  console.log(target);
})();
