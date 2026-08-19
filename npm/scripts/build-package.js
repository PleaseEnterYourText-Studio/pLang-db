'use strict';

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
// 默认从 pLang-db 的 CLion 构建目录取二进制；可用参数覆盖
const buildDir = process.argv[2] || path.join(root, '..', 'cmake-build-debug');

function cp(src, dst) {
  if (!fs.existsSync(src)) {
    console.error('[build] missing: ' + src);
    process.exit(1);
  }
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.copyFileSync(src, dst);
  console.log('[build] ' + path.basename(src) + ' -> ' + path.relative(root, dst));
}

const isWin = process.platform === 'win32';
cp(path.join(buildDir, isWin ? 'pdb.exe' : 'pdb'), path.join(root, 'bin', isWin ? 'pdb.exe' : 'pdb'));

console.log('[build] done.');
