// Builds ui-prototypes/pet.html: the settings page template, with the prototype's bundled engine and the
// manifest and PNGs inlined, so the page works when opened from file://.
// Run from anywhere: node tools/build-pet-html.mjs   (npm run embed)
import { execFileSync } from 'node:child_process';
import { readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const proto = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const bundleDir = path.join(proto, 'dist-embed');
const template = path.join(proto, 'embed', 'settings.template.html');
const output = path.resolve(proto, '..', 'pet.html');
const vite = path.join(proto, 'node_modules', 'vite', 'bin', 'vite.js');

execFileSync(process.execPath, [vite, 'build', '--config', 'vite.embed.config.js', '--outDir', bundleDir, '--emptyOutDir'], {
  cwd: proto,
  stdio: 'inherit',
});

const bundle = readFileSync(path.join(bundleDir, 'pet-embed.js'), 'utf8');
rmSync(bundleDir, { recursive: true, force: true });
if (bundle.includes('</script')) throw new Error('bundle contains </script, would break the inline script');

const assetsDir = path.join(proto, 'assets', 'pet');
const manifest = JSON.parse(readFileSync(path.join(assetsDir, 'manifest.json'), 'utf8'));
const paths = [
  ...Object.values(manifest.actions).flatMap(action => [action.sheet, action.effect].filter(Boolean)),
  manifest.expression,
  ...manifest.accessories.map(accessory => accessory.asset),
];
const assets = Object.fromEntries(paths.map(p => [
  p,
  `data:image/png;base64,${readFileSync(path.join(assetsDir, p)).toString('base64')}`,
]));

// Serialised JSON must not close the script element early.
const json = value => JSON.stringify(value).replaceAll('</', '<\\/');
const data = `const PET_MANIFEST = ${json(manifest)};\nconst PET_ASSETS = ${json(assets)};`;

// Replacer functions, not strings: the bundle contains `$` sequences that String.replace would expand.
const html = readFileSync(template, 'utf8')
  .replace('/*@PET_DATA@*/', () => data)
  .replace('/*@PET_BUNDLE@*/', () => bundle);
writeFileSync(output, html);
console.log('wrote', output);
