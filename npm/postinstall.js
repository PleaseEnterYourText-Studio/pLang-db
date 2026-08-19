// pdb 安装脚本：检测 LLDB，配置 pdb 的 liblldb 动态库路径
// - macOS: 查找 brew LLVM 的 liblldb（Intel /usr/local/opt 与 ARM /opt/homebrew），用 install_name_tool 改写
// - Linux: 查找 liblldb.so，设置 rpath
// - Windows: 查找 lldb.dll，复制到 bin 旁
'use strict';

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const binDir = path.join(__dirname, 'bin');

function log(msg) { console.log('[pdb] ' + msg); }
function warn(msg) { console.warn('[pdb] WARN: ' + msg); }

function findLLVMDir() {
  const candidates = [
    process.env.LLVM_HOME,
    '/opt/homebrew/opt/llvm',          // Apple Silicon brew
    '/usr/local/opt/llvm',             // Intel brew
    '/usr/lib/llvm-22', '/usr/lib/llvm-21', '/usr/lib/llvm-20',
    '/usr/lib/llvm-19', '/usr/lib/llvm-18', '/usr/lib/llvm-17', '/usr/lib/llvm-16',
    '/usr/local/llvm',
  ].filter(Boolean);
  for (const dir of candidates) {
    if (fs.existsSync(dir)) return dir;
  }
  return null;
}

function findLib(dir) {
  const names = ['liblldb.dylib', 'liblldb.so', 'lldb.dll'];
  for (const n of names) {
    const p = path.join(dir, 'lib', n);
    if (fs.existsSync(p)) return p;
  }
  // 库可能在 lib/ 下子目录（如 lib/llvm-18）
  try {
    for (const sub of fs.readdirSync(path.join(dir, 'lib'))) {
      const subDir = path.join(dir, 'lib', sub);
      if (fs.statSync(subDir).isDirectory()) {
        for (const n of names) {
          const p = path.join(subDir, n);
          if (fs.existsSync(p)) return p;
        }
      }
    }
  } catch (e) { /* ignore */ }
  return null;
}

function currentLldbRef(binPath) {
  // macOS: otool -L 读当前 liblldb 引用（可能是带版本号的 .dylib）
  try {
    const out = execSync(`otool -L "${binPath}"`, { encoding: 'utf8' });
    for (const line of out.split('\n')) {
      const m = line.trim().match(/^(\S*liblldb[^\s]*dylib)/);
      if (m) return m[1];
    }
  } catch (e) { /* ignore */ }
  return '/usr/local/opt/llvm/lib/liblldb.dylib';
}

function main() {
  const platform = process.platform;
  const binName = platform === 'win32' ? 'pdb.exe' : 'pdb';
  const binPath = path.join(binDir, binName);

  if (!fs.existsSync(binPath)) {
    warn('pdb binary not found at ' + binPath);
    return;
  }

  const llvmDir = findLLVMDir();
  if (!llvmDir) {
    warn('LLVM/LLDB not found. pdb requires LLDB.');
    warn('  macOS:  brew install llvm');
    warn('  Linux:  sudo apt install llvm');
    warn('  Windows: install LLVM from https://llvm.org and set LLVM_HOME');
    log('You can set LLVM_HOME to point at your LLVM installation.');
    return; // 不硬失败：用户可能已手动配置
  }
  const lldbLib = findLib(llvmDir);
  if (!lldbLib) {
    warn('LLVM found at ' + llvmDir + ' but liblldb not located. Set LLVM_HOME.');
    return;
  }
  log('Found LLDB: ' + lldbLib);

  try {
    if (platform === 'darwin') {
      const oldRef = currentLldbRef(binPath);
      execSync(`install_name_tool -change "${oldRef}" "${lldbLib}" "${binPath}"`);
      log('pdb -> ' + lldbLib);
    } else if (platform === 'linux') {
      execSync(`patchelf --set-rpath "${path.dirname(lldbLib)}" "${binPath}"`);
      log('pdb rpath -> ' + path.dirname(lldbLib));
    } else if (platform === 'win32') {
      fs.copyFileSync(lldbLib, path.join(binDir, 'lldb.dll'));
      log('copied lldb.dll to bin/');
    }
  } catch (e) {
    warn('Failed to configure LLDB path: ' + e.message);
    warn('Set LLVM_HOME or configure manually.');
  }
}

main();
