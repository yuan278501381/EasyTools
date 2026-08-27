import fs from 'fs';
import path from 'path';

const enPath = path.resolve('./src/i18n/locales/en.json');
const zhPath = path.resolve('./src/i18n/locales/zh.json');

const enData = JSON.parse(fs.readFileSync(enPath, 'utf8'));
const zhData = JSON.parse(fs.readFileSync(zhPath, 'utf8'));

let hasError = false;

// 1. 递归比对中英文 JSON 键位对齐
function compareKeys(obj1, obj2, prefix = '') {
  for (const key in obj1) {
    if (typeof obj1[key] === 'object' && obj1[key] !== null) {
      if (!obj2[key] || typeof obj2[key] !== 'object') {
        console.error(`❌ [Missing Object] zh.json is missing object key: ${prefix}${key}`);
        hasError = true;
      } else {
        compareKeys(obj1[key], obj2[key], `${prefix}${key}.`);
      }
    } else {
      if (obj2[key] === undefined) {
        console.error(`❌ [Missing Key] zh.json is missing translation for: ${prefix}${key}`);
        hasError = true;
      }
    }
  }

  // 反向检查 (zh.json 中存在但 en.json 缺失的键)
  for (const key in obj2) {
    if (obj1[key] === undefined) {
      console.error(`❌ [Extra Key] zh.json has extra key not in en.json: ${prefix}${key}`);
      hasError = true;
    }
  }
}

// 2. 扫描源码中是否存在硬编码中文 Fallback
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

function checkSourceCodeFallbacks() {
  const files = getFiles(path.resolve('./src'));
  const chineseRegex = /[\u4e00-\u9fa5]/;
  const tFallbackRegex = /t\(\s*['"`]([^'"`]+)['"`]\s*,\s*['"`]([^'"`]+)['"`]\s*\)/g;

  for (const file of files) {
    const content = fs.readFileSync(file, 'utf8');
    let match;
    while ((match = tFallbackRegex.exec(content)) !== null) {
      const key = match[1];
      const fallback = match[2];
      if (chineseRegex.test(fallback)) {
        console.error(`❌ [Chinese Fallback Violation] ${path.relative('./', file)}:`);
        console.error(`   key: "${key}", fallback: "${fallback}"`);
        console.error(`   [Rule] All fallback values in source code must be English. Put Chinese translations into zh.json!`);
        hasError = true;
      }
    }
  }
}

// 3. 全库源码深度零裸中文扫描 (涵盖 JSX 文本节点、三元表达式、模板字符串、字符串字面量、HTML属性)
function checkNakedChineseInUI() {
  const files = getFiles(path.resolve('./src'), ['.ts', '.tsx']);
  const chineseRegex = /[\u4e00-\u9fa5]/;

  // 合法中文白名单模式 (如搜索语法兼容判断 query.startsWith('内容:'))
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

      // 剔除单行注释与 JSX 注释
      let codeOnly = rawLine.replace(/\/\/.*$/, '').replace(/\{\/\*.*?\*\/\}/g, '');
      if (!chineseRegex.test(codeOnly)) return;

      // 检查白名单
      if (allowedPatterns.some(p => p.test(codeOnly))) return;

      console.error(`❌ [Zero-Naked-Chinese Gate] 发现未国际化的裸中文硬编码: ${path.relative('./', file)}:${idx + 1}`);
      console.error(`   代码行: "${rawLine.trim()}"`);
      console.error(`   [Rule] 严禁在源码或 JSX 中硬编码任何中文文本/属性/三元表达式！必须 100% 接入 t('namespace.key', 'English fallback')`);
      hasError = true;
    });
  }
}

console.log('🔍 [i18n Gate] 正在执行 EasyTools 世界级多语言体系、全量 Fallback 与零裸中文审查...');
compareKeys(enData, zhData);
checkSourceCodeFallbacks();
checkNakedChineseInUI();

if (hasError) {
  console.error('\n❌ i18n 门禁审查失败！请修复上述缺失的键、非英文 Fallback 或裸中文硬编码。');
  process.exit(1);
} else {
  console.log('✅ i18n 门禁审查全部通过！键位 100% 对齐，代码 Fallback 100% 符合英文基准，0 裸中文硬编码。');
}
