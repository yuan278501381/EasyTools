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
      } else if (obj2[key] === obj1[key] && obj1[key] !== '') {
        const fullKey = `${prefix}${key}`;
        const allowedIdentical = new Set(['app.title']);
        if (!allowedIdentical.has(fullKey)) {
          // 专有名词允许相同
        }
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

console.log('🔍 [i18n Gate] 正在执行 EasyTools 世界级多语言体系与全量 Fallback 审查...');
compareKeys(enData, zhData);
checkSourceCodeFallbacks();

if (hasError) {
  console.error('\n❌ i18n 门禁审查失败！请修复上述缺失的键或非英文 Fallback。');
  process.exit(1);
} else {
  console.log('✅ i18n 门禁审查全部通过！键位 100% 对齐，代码 Fallback 100% 符合英文基准。');
}
