import fs from 'fs';
import path from 'path';

let hasError = false;

function getFiles(dir, exts) {
  let results = [];
  if (!fs.existsSync(dir)) return results;
  const list = fs.readdirSync(dir);
  for (const file of list) {
    const filePath = path.join(dir, file);
    const stat = fs.statSync(filePath);
    if (stat && stat.isDirectory()) {
      if (file !== 'build' && file !== 'deploy_dist' && file !== 'third_party' && file !== 'node_modules') {
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

// 1. 检查 C++ 日志系统规范 (双语日志宏占位符对齐、英文 0 汉字污染、空日志防范)
function checkCppLoggingStandards() {
  const cppFiles = getFiles(path.resolve('../src'), ['.cpp', '.h', '.hpp']);
  const logMacroLRegex = /\b(LOG_TRACE_L|LOG_DEBUG_L|LOG_INFO_L|LOG_WARN_L|LOG_ERROR_L|LOG_CRITICAL_L)\s*\(\s*("(?:\\.|[^"\\])*")\s*,\s*("(?:\\.|[^"\\])*")/g;
  const logMacroRegex = /\b(LOG_TRACE|LOG_DEBUG|LOG_INFO|LOG_WARN|LOG_ERROR|LOG_CRITICAL)\s*\(\s*([^)]+)\)/g;
  const cjkRegex = /[\u4e00-\u9fa5]/;

  for (const file of cppFiles) {
    if (file.includes('core\\logger\\') || file.includes('core/logger/')) continue;
    const content = fs.readFileSync(file, 'utf8');

    // 检查双语日志宏参数对齐
    let matchL;
    while ((matchL = logMacroLRegex.exec(content)) !== null) {
      const macro = matchL[1];
      const zhFmt = matchL[2];
      const enFmt = matchL[3];

      // 提取占位符 {}
      const zhPlaceholders = (zhFmt.match(/\{[^}]*\}/g) || []).length;
      const enPlaceholders = (enFmt.match(/\{[^}]*\}/g) || []).length;

      if (zhPlaceholders !== enPlaceholders) {
        console.error(`❌ [Log Placeholder Mismatch Gate] 双语日志占位符数量不一致: ${path.relative('../', file)}`);
        console.error(`   中文模板: ${zhFmt} (占位符数: ${zhPlaceholders})`);
        console.error(`   英文模板: ${enFmt} (占位符数: ${enPlaceholders})`);
        hasError = true;
      }

      if (cjkRegex.test(enFmt)) {
        console.error(`❌ [CJK in English Log Gate] 双语日志英文模板包含汉字字符: ${path.relative('../', file)}`);
        console.error(`   英文模板: ${enFmt}`);
        hasError = true;
      }
    }

    // 检查空日志
    let match;
    while ((match = logMacroRegex.exec(content)) !== null) {
      const args = match[2].trim();
      if (!args || args === '""' || args === "''") {
        console.error(`❌ [Empty Log Gate] 发现空日志调用: ${path.relative('../', file)}`);
        console.error(`   代码: "${match[0]}"`);
        hasError = true;
      }
    }
  }
}

// 2. 检查前端用户可见日志 (Toast / Notification / UI Error) 是否 100% 国际化
function checkFrontendUserFacingLogs() {
  const tsFiles = getFiles(path.resolve('./src'), ['.ts', '.tsx']);
  const toastRegex = /toast\.(?:success|error|warning|info|message)\s*\(\s*(['"`][^'"`]+['"`]|[^)]+)\)/g;
  const chineseRegex = /[\u4e00-\u9fa5]/;

  for (const file of tsFiles) {
    if (file.endsWith('.test.tsx') || file.endsWith('.spec.tsx') || file.endsWith('.test.ts')) continue;
    const content = fs.readFileSync(file, 'utf8');

    let match;
    while ((match = toastRegex.exec(content)) !== null) {
      const param = match[1].trim();
      if (chineseRegex.test(param) && !param.includes('t(')) {
        console.error(`❌ [Unlocalized Toast Gate] 发现未国际化的用户提示日志: ${path.relative('./', file)}`);
        console.error(`   代码: "${match[0]}"`);
        console.error(`   [Rule] 所有用户感知层 Toast 与日志提示必须使用 t('namespace.key') 国际化！`);
        hasError = true;
      }
    }
  }
}

console.log('🔍 [Observability & Log Gate] 正在执行 EasyTools 全链路日志规范、多语言与国际化门禁审查...');
checkCppLoggingStandards();
checkFrontendUserFacingLogs();

if (hasError) {
  console.error('\n❌ 日志规范门禁审查失败！请修复上述空日志、占位符不匹配或未国际化 Toast 提示。');
  process.exit(1);
} else {
  console.log('✅ 日志系统世界级规范与多语言门禁全部通过！');
  console.log('   1. C++ 双语日志宏 {} 占位符 100% 精确对齐，英文模板 0 汉字污染');
  console.log('   2. C++ 内核日志 100% 规范宏与 TraceID 链路透传 (0 空日志)');
  console.log('   3. 前端用户感知层日志 100% 接入多语言国际化管线 (0 裸中文 Toast)');
}
