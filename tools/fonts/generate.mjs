import { readFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const font = resolve(root, '.tools/fonts/NotoSansCJKsc-Regular.otf');
const source = readFileSync(resolve(root, 'components/ui/include/rc/strings.hpp'), 'utf8');
const symbols = [...new Set([...source].filter(c => c.codePointAt(0) > 127))].sort().join('');
const checksum = createHash('sha256').update(readFileSync(font)).digest('hex');
if (checksum !== '2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b') {
  throw new Error('Font source SHA-256 mismatch');
}
console.log('Source font SHA-256:', checksum);
for (const size of [20, 24]) {
  const result = spawnSync(process.execPath, [resolve(here, 'node_modules/lv_font_conv/lv_font_conv.js'),
    '--font', '.tools/fonts/NotoSansCJKsc-Regular.otf', '--size', String(size), '--bpp', '4', '--format', 'lvgl',
    '--range', '0x20-0x7e', '--symbols', symbols, '--no-compress',
    '--lv-font-name', `rc_font_${size}`, '--output', `components/ui/src/font_${size}.c`], {stdio: 'inherit', cwd: root});
  if (result.status !== 0) process.exit(result.status ?? 1);
}
