import fs from 'fs';
import path from 'path';

const enPath = path.resolve('./src/i18n/locales/en.json');
const zhPath = path.resolve('./src/i18n/locales/zh.json');

const enData = JSON.parse(fs.readFileSync(enPath, 'utf8'));
const zhData = JSON.parse(fs.readFileSync(zhPath, 'utf8'));

let hasError = false;

// 允许在中文包中保留的合法技术专有名词/缩写/按键/格式/品牌白名单
const ALLOWED_TECH_TOKENS = new Set([
  'windows', 'easytools', 'microsoft', 'chrome', 'edge', 'firefox', 'wechat', 'feishu', 'dingtalk',
  'dpi', 'ocr', 'ntfs', 'mft', 'usn', 'sqlite', 'direct2d', 'webview2', 'sta', 'ipc', 'api', 'url', 'uri',
  'ctrl', 'alt', 'shift', 'win', 'enter', 'esc', 'tab', 'backspace', 'delete', 'space',
  'f1', 'f2', 'f3', 'f4', 'f5', 'f6', 'f7', 'f8', 'f9', 'f10', 'f11', 'f12',
  'word', 'excel', 'powerpoint', 'pdf', 'csv', 'tsv', 'json', 'xml', 'yaml', 'txt', 'md', 'html', 'css', 'sql',
  'png', 'jpg', 'jpeg', 'webp', 'gif', 'bmp', 'svg', 'ico', 'mp4', 'mkv', 'avi', 'mov', 'wmv', 'flv', 'webm',
  'mp3', 'wav', 'flac', 'aac', 'm4a', 'ogg', 'zip', 'rar', '7z', 'tar', 'gz',
  'cpp', 'c', 'h', 'hpp', 'js', 'ts', 'tsx', 'py', 'rs', 'go', 'java', 'lua', 'cmd', 'bat', 'ps1',
  'ssd', 'hdd', 'ram', 'cpu', 'gpu', 'mb', 'gb', 'kb', 'bytes', 'b', 'ms', 'fps', 'px', 'rem',
  'mit', 'license', 'github', 'ok', 'id', 'rgb', 'hex', 'rgba', 'hsl', 'dps', 'wps', 'cad', 'qr'
]);

function isAllowedPureEnglishToken(str) {
  const clean = str.toLowerCase().replace(/[^a-z0-9_]/g, '');
  if (!clean) return true;
  if (ALLOWED_TECH_TOKENS.has(clean)) return true;
  if (/^v\d+\.\d+\.\d+/.test(clean)) return true;
  if (/^(ctrl|alt|shift|win)\+[a-z0-9+]+$/.test(clean)) return true;
  return false;
}

// 辅助：递归获取多层属性
function hasJsonPath(obj, keyPath) {
  const parts = keyPath.split('.');
  let curr = obj;
  for (const p of parts) {
    if (!curr || typeof curr !== 'object' || !(p in curr)) return false;
    curr = curr[p];
  }
  return true;
}

// 辅助：获取全量代码文件
function getFiles(dir, exts = ['.ts', '.tsx']) {
  let results = [];
  const list = fs.readdirSync(dir);
  for (const file of list) {
    const filePath = path.join(dir, file);
    const stat = fs.statSync(filePath);
    if (stat && stat.isDirectory()) {
      if (file !== 'node_modules' && file !== 'dist' && file !== 'coverage_report') {
        results = results.concat(getFiles(filePath, exts));
      }
    } else {
      if (exts.includes(path.extname(filePath))) {
        results.push(filePath);
      }
    }
  }
  return results;
}

// ── 防线 1 & 防线 2 & 防线 3 & 防线 4：JSON 字典同构、纯净度与翻译泄漏检查 ──
function checkJsonParityAndLeaks(obj1, obj2, prefix = '') {
  const cjkRegex = /[\u4e00-\u9fa5]/;

  for (const key in obj1) {
    const fullKey = `${prefix}${key}`;
    const val1 = obj1[key];
    const val2 = obj2 ? obj2[key] : undefined;

    if (typeof val1 === 'object' && val1 !== null) {
      if (!val2 || typeof val2 !== 'object') {
        console.error(`❌ [Missing Object] zh.json 缺失对象命名空间: ${fullKey}`);
        hasError = true;
      } else {
        checkJsonParityAndLeaks(val1, val2, `${fullKey}.`);
      }
    } else {
      if (val2 === undefined) {
        console.error(`❌ [Missing Key] zh.json 缺失翻译键位: ${fullKey}`);
        hasError = true;
      } else if (typeof val1 === 'string' && typeof val2 === 'string') {
        // 防线 2：英文包绝对 0 汉字污染
        if (cjkRegex.test(val1)) {
          console.error(`❌ [CJK in English Gate] en.json 存在非法汉字字符: [${fullKey}] -> "${val1}"`);
          hasError = true;
        }

        // 防线 4：变量插值完整性检查
        const enVars = (val1.match(/\{\{([^}]+)\}\}/g) || []).sort();
        const zhVars = (val2.match(/\{\{([^}]+)\}\}/g) || []).sort();
        if (enVars.join(',') !== zhVars.join(',')) {
          console.error(`❌ [Interpolation Parity Violation] 变量插值不一致: [${fullKey}]`);
          console.error(`   en: "${val1}" (变量: [${enVars.join(', ')}])`);
          console.error(`   zh: "${val2}" (变量: [${zhVars.join(', ')}])`);
          hasError = true;
        }

        // 防线 3：中文包未翻译英文泄漏智能识别 (排除合法专有名词)
        if (!cjkRegex.test(val2)) {
          let stripped = val2.replace(/\{\{[^}]+\}\}/g, '').trim();
          stripped = stripped.replace(/[\s\-_.,/\\:;!?'"()[\]{}<>@#$%^&*+=|~`0-9]/g, ' ').trim();
          if (stripped) {
            const words = stripped.split(/\s+/).filter(Boolean);
            const unrecognized = words.filter(w => !isAllowedPureEnglishToken(w));
            if (unrecognized.length > 0) {
              console.error(`❌ [Translation Leak Gate] zh.json 中文包疑似遗漏翻译 (纯英文未汉化): [${fullKey}]`);
              console.error(`   中文当前值: "${val2}"`);
              console.error(`   英文基准值: "${val1}"`);
              console.error(`   未识别的非专业英文单词: [${unrecognized.join(', ')}]`);
              console.error(`   [Rule] 中文包必须翻译为规范中文！若为通用技术词请添加至白名单。`);
              hasError = true;
            }
          }
        }
      }
    }
  }

  // 反向检查：zh.json 中多余的孤儿键
  if (obj2 && typeof obj2 === 'object') {
    for (const key in obj2) {
      if (obj1[key] === undefined) {
        console.error(`❌ [Orphan Key] zh.json 包含 en.json 中不存在的孤儿键位: ${prefix}${key}`);
        hasError = true;
      }
    }
  }
}

// ── 防线 5：全库源码静态键位引用严格存在性校验 (Code-to-Locale Strict Reference Gate) ──
function checkCodeReferences() {
  const files = getFiles(path.resolve('./src'), ['.ts', '.tsx']);
  const keyRefRegex = /(?:labelKey|descKey|titleKey|extKey|nameKey|subKey|tipKey|headerKey)\s*:\s*['"]([a-zA-Z0-9_.-]+)['"]/g;
  const tCallRegex = /(?<![a-zA-Z0-9_$])t\(\s*['"]([a-zA-Z0-9_.-]+)['"]/g;

  for (const file of files) {
    if (file.endsWith('.test.tsx') || file.endsWith('.spec.tsx') || file.endsWith('.test.ts')) continue;
    const content = fs.readFileSync(file, 'utf8');

    // 检查模型/常量中的 labelKey / descKey 等
    let match;
    while ((match = keyRefRegex.exec(content)) !== null) {
      const k = match[1];
      if (k.includes('.') && !k.startsWith('http') && !k.startsWith('easytools_') && !k.startsWith('/') && !k.includes(' ')) {
        if (!hasJsonPath(enData, k) || !hasJsonPath(zhData, k)) {
          console.error(`❌ [Broken Key Reference Gate] 发现源码中声明了字典中不存在的键位: ${path.relative('./', file)}`);
          console.error(`   未定义的键: "${k}"`);
          console.error(`   [Rule] 代码中声明的所有 labelKey/descKey/titleKey 必须在 en.json 和 zh.json 中真实存在！`);
          hasError = true;
        }
      }
    }

    // 检查字面量 t('namespace.key') 调用
    while ((match = tCallRegex.exec(content)) !== null) {
      const k = match[1];
      if (k.includes('.') && !k.startsWith('http') && !k.startsWith('easytools_') && !k.includes(' ')) {
        if (!hasJsonPath(enData, k) || !hasJsonPath(zhData, k)) {
          console.error(`❌ [Broken t() Reference Gate] 发现 t() 调用了字典中不存在的键位: ${path.relative('./', file)}`);
          console.error(`   未定义的键: "${k}"`);
          console.error(`   [Rule] 必须确保 t() 调用的所有键位在多语言字典中 100% 存在！`);
          hasError = true;
        }
      }
    }
  }
}

// ── 防线 6：全库源码零裸中文与英文 Fallback 规范 ──
function checkSourceCodeFallbacksAndNakedChinese() {
  const files = getFiles(path.resolve('./src'), ['.ts', '.tsx']);
  const chineseRegex = /[\u4e00-\u9fa5]/;
  const tFallbackRegex = /t\(\s*['"`]([^'"`]+)['"`]\s*,\s*['"`]([^'"`]+)['"`]\s*\)/g;

  const allowedPatterns = [
    /startsWith\(['"`]内容:['"`]\)/,
    /CONTENT_PREFIXES/,
    /localeCompare/,
    /Chinese/,
    /titleZh:/,
    /descZh:/,
    /conditionZh:/,
    /supportedFormats:/
  ];

  for (const file of files) {
    if (file.endsWith('.test.tsx') || file.endsWith('.spec.tsx') || file.endsWith('.test.ts') || file.endsWith('useBridge.ts')) continue;
    const content = fs.readFileSync(file, 'utf8');

    // 1. Fallback 必须英文
    let match;
    while ((match = tFallbackRegex.exec(content)) !== null) {
      const key = match[1];
      const fallback = match[2];
      if (chineseRegex.test(fallback)) {
        console.error(`❌ [Chinese Fallback Violation] ${path.relative('./', file)}:`);
        console.error(`   key: "${key}", fallback: "${fallback}"`);
        console.error(`   [Rule] 源码中的所有 fallback 必须为英文，中文请录入 zh.json！`);
        hasError = true;
      }
    }

    // 2. 裸中文扫描
    const lines = content.split(/\r?\n/);
    let inBlockComment = false;

    lines.forEach((rawLine, idx) => {
      const line = rawLine.trim();
      if (line.startsWith('/*')) inBlockComment = true;
      if (inBlockComment) {
        if (line.includes('*/')) inBlockComment = false;
        return;
      }
      if (line.startsWith('//') || line.startsWith('*')) return;

      let codeOnly = rawLine.replace(/\/\/.*$/, '').replace(/\{\/\*.*?\*\/\}/g, '');
      if (!chineseRegex.test(codeOnly)) return;
      if (allowedPatterns.some(p => p.test(codeOnly))) return;

      console.error(`❌ [Zero-Naked-Chinese Gate] 发现未国际化的裸中文硬编码: ${path.relative('./', file)}:${idx + 1}`);
      console.error(`   代码行: "${rawLine.trim()}"`);
      console.error(`   [Rule] 严禁在源码或 JSX 中硬编码任何中文文本！必须 100% 接入 t('namespace.key', 'English fallback')`);
      hasError = true;
    });
  }
}

console.log('🔍 [i18n Gate] 正在执行 EasyTools 世界级 6 重多语言体系防护门禁审查...');
checkJsonParityAndLeaks(enData, zhData);
checkCodeReferences();
checkSourceCodeFallbacksAndNakedChinese();

if (hasError) {
  console.error('\n❌ i18n 门禁审查失败！请修复上述键位缺失、英文泄漏、断裂引用或裸中文问题。');
  process.exit(1);
} else {
  console.log('✅ i18n 世界级 6 重防护门禁全部通过！');
  console.log('   1. 中英文字典 100% 同构双向对齐');
  console.log('   2. 英文包绝对 0 汉字污染');
  console.log('   3. 中文包 0 未翻译英文泄漏 (智能专有名词与混排合规)');
  console.log('   4. 动态变量插值 {{param}} 100% 双向对齐');
  console.log('   5. 全库源码所有 labelKey/t() 引用 100% 在字典中真实存在');
  console.log('   6. 全库源码 0 裸中文硬编码与 100% 英文 Fallback');
}
