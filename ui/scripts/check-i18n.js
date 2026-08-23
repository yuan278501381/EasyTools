import fs from 'fs';
import path from 'path';

const enPath = path.resolve('./src/i18n/locales/en.json');
const zhPath = path.resolve('./src/i18n/locales/zh.json');

const enData = JSON.parse(fs.readFileSync(enPath, 'utf8'));
const zhData = JSON.parse(fs.readFileSync(zhPath, 'utf8'));

let hasError = false;

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
        // 允许软件官方品牌标识等专有名词保持统一
        const allowedIdentical = new Set(['app.title']);
        if (!allowedIdentical.has(fullKey)) {
          console.warn(`⚠️ [Warning] zh.json value is identical to English for: ${fullKey}`);
        }
      }
    }
  }

  // Check reverse (keys in zh but not in en)
  for (const key in obj2) {
    if (obj1[key] === undefined) {
      console.error(`❌ [Extra Key] zh.json has extra key not in en.json: ${prefix}${key}`);
      hasError = true;
    }
  }
}

console.log('🔍 Checking i18n locale files...');
compareKeys(enData, zhData);

if (hasError) {
  console.error('\n❌ i18n check failed. Please fix the missing or extra keys in zh.json.');
  process.exit(1);
} else {
  console.log('✅ i18n check passed. All keys are aligned.');
}
