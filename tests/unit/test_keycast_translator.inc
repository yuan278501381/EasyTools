// ─────────────────────────────────────────────────────────────────────────────
// test_keycast_translator.inc — KeyTranslator 与按键回显意图裁决单元测试
// ─────────────────────────────────────────────────────────────────────────────

#include <gtest/gtest.h>
#include "core/hotkey/KeyTranslator.h"

using namespace tools3000::core;

TEST(KeycastTranslatorTest, Shift8FoldingToAsteriskInAllKeys) {
    // 验证 Shift + 8 在 all_keys 模式下折叠为单个 [*] 键帽
    KeycastKeyInfo info = KeyTranslator::resolveKeycastEvent(
        '8', 0,
        false /*Ctrl*/, false /*Alt*/, true /*Shift*/, false /*Win*/,
        "all_keys", false
    );

    EXPECT_TRUE(info.shouldDisplay);
    EXPECT_FALSE(info.isShortcut);
    ASSERT_EQ(info.tokens.size(), 1u);
    EXPECT_EQ(info.tokens[0], "*");
    EXPECT_EQ(info.rawKey, "*");
}

TEST(KeycastTranslatorTest, ShiftEqualFoldingToPlusInAllKeys) {
    // 验证 Shift + = (VK_OEM_PLUS) 在 all_keys 模式下折叠为单个 [+] 键帽，且加号不被吞噬
    KeycastKeyInfo info = KeyTranslator::resolveKeycastEvent(
        VK_OEM_PLUS, 0,
        false, false, true, false,
        "all_keys", false
    );

    EXPECT_TRUE(info.shouldDisplay);
    EXPECT_FALSE(info.isShortcut);
    ASSERT_EQ(info.tokens.size(), 1u);
    EXPECT_EQ(info.tokens[0], "+");
    EXPECT_EQ(info.rawKey, "+");
}

TEST(KeycastTranslatorTest, Shift1FoldingToExclamation) {
    // 验证 Shift + 1 折叠为单个 [!] 键帽
    KeycastKeyInfo info = KeyTranslator::resolveKeycastEvent(
        '1', 0,
        false, false, true, false,
        "all_keys", false
    );

    EXPECT_TRUE(info.shouldDisplay);
    EXPECT_FALSE(info.isShortcut);
    ASSERT_EQ(info.tokens.size(), 1u);
    EXPECT_EQ(info.tokens[0], "!");
    EXPECT_EQ(info.rawKey, "!");
}

TEST(KeycastTranslatorTest, ShiftPunctuationQuietInSmartShortcuts) {
    // 【世界级体验】：日常打字输入标点符号 (*, +, !) 在智能快捷键模式下必须完全静默，绝不误弹
    KeycastKeyInfo info8 = KeyTranslator::resolveKeycastEvent(
        '8', 0,
        false, false, true, false,
        "smart_shortcuts", false
    );
    EXPECT_FALSE(info8.shouldDisplay);

    KeycastKeyInfo infoPlus = KeyTranslator::resolveKeycastEvent(
        VK_OEM_PLUS, 0,
        false, false, true, false,
        "smart_shortcuts", false
    );
    EXPECT_FALSE(infoPlus.shouldDisplay);

    KeycastKeyInfo info1 = KeyTranslator::resolveKeycastEvent(
        '1', 0,
        false, false, true, false,
        "smart_shortcuts", false
    );
    EXPECT_FALSE(info1.shouldDisplay);
}

TEST(KeycastTranslatorTest, ShiftLetterQuietInSmartShortcutsAndFoldingInAllKeys) {
    // Shift + A 大写打字：smart_shortcuts 下静默，all_keys 下折叠为单个 [A]
    KeycastKeyInfo infoSmart = KeyTranslator::resolveKeycastEvent(
        'A', 0,
        false, false, true, false,
        "smart_shortcuts", false
    );
    EXPECT_FALSE(infoSmart.shouldDisplay);

    KeycastKeyInfo infoAll = KeyTranslator::resolveKeycastEvent(
        'A', 0,
        false, false, true, false,
        "all_keys", false
    );
    EXPECT_TRUE(infoAll.shouldDisplay);
    ASSERT_EQ(infoAll.tokens.size(), 1u);
    EXPECT_EQ(infoAll.tokens[0], "A");
    EXPECT_EQ(infoAll.rawKey, "A");
}

TEST(KeycastTranslatorTest, FullShortcutWithCtrlAltWinPreservesFullSequence) {
    // 含有主控修饰键的快捷键 (Ctrl + Shift + 8) 必须完整保留修饰键与主键名
    KeycastKeyInfo info = KeyTranslator::resolveKeycastEvent(
        '8', 0,
        true /*Ctrl*/, false /*Alt*/, true /*Shift*/, false /*Win*/,
        "smart_shortcuts", false
    );

    EXPECT_TRUE(info.shouldDisplay);
    EXPECT_TRUE(info.isShortcut);
    ASSERT_EQ(info.tokens.size(), 3u);
    EXPECT_EQ(info.tokens[0], "Ctrl");
    EXPECT_EQ(info.tokens[1], "Shift");
    EXPECT_EQ(info.tokens[2], "8");
    EXPECT_EQ(info.rawKey, "Ctrl + Shift + 8");
}

TEST(KeycastTranslatorTest, CtrlPlusZoomShortcutPreservesPlus) {
    // 放大快捷键 Ctrl + +：验证加号完整保留，不被任何分词规则吞噬
    KeycastKeyInfo info = KeyTranslator::resolveKeycastEvent(
        VK_ADD, 0,
        true /*Ctrl*/, false, false, false,
        "smart_shortcuts", false
    );

    EXPECT_TRUE(info.shouldDisplay);
    EXPECT_TRUE(info.isShortcut);
    ASSERT_EQ(info.tokens.size(), 2u);
    EXPECT_EQ(info.tokens[0], "Ctrl");
    EXPECT_EQ(info.tokens[1], "+");
    EXPECT_EQ(info.rawKey, "Ctrl + +");
}

TEST(KeycastTranslatorTest, ShiftActionFunctionalKeysAreShortcuts) {
    // Shift + 动作功能键 (如 Shift + Tab, Shift + Delete) 属于动作快捷键
    KeycastKeyInfo infoTab = KeyTranslator::resolveKeycastEvent(
        VK_TAB, 0,
        false, false, true, false,
        "smart_shortcuts", false
    );
    EXPECT_TRUE(infoTab.shouldDisplay);
    EXPECT_TRUE(infoTab.isShortcut);
    ASSERT_EQ(infoTab.tokens.size(), 2u);
    EXPECT_EQ(infoTab.tokens[0], "Shift");
    EXPECT_EQ(infoTab.tokens[1], "Tab");

    KeycastKeyInfo infoDel = KeyTranslator::resolveKeycastEvent(
        VK_DELETE, 0,
        false, false, true, false,
        "smart_shortcuts", false
    );
    EXPECT_TRUE(infoDel.shouldDisplay);
    EXPECT_TRUE(infoDel.isShortcut);
    ASSERT_EQ(infoDel.tokens.size(), 2u);
    EXPECT_EQ(infoDel.tokens[0], "Shift");
    EXPECT_EQ(infoDel.tokens[1], "Delete");
}

TEST(KeycastTranslatorTest, FunctionalKeysWithoutModifiersPolicy) {
    // 单按功能键 (Esc, F5, PageUp)：由 includeFunctionKeys 控制
    KeycastKeyInfo infoOff = KeyTranslator::resolveKeycastEvent(
        VK_ESCAPE, 0,
        false, false, false, false,
        "smart_shortcuts", false /*includeFunctionKeys*/
    );
    EXPECT_FALSE(infoOff.shouldDisplay);

    KeycastKeyInfo infoOn = KeyTranslator::resolveKeycastEvent(
        VK_ESCAPE, 0,
        false, false, false, false,
        "smart_shortcuts", true /*includeFunctionKeys*/
    );
    EXPECT_TRUE(infoOn.shouldDisplay);
    ASSERT_EQ(infoOn.tokens.size(), 1u);
    EXPECT_EQ(infoOn.tokens[0], "Esc");

    KeycastKeyInfo infoAll = KeyTranslator::resolveKeycastEvent(
        VK_F5, 0,
        false, false, false, false,
        "all_keys", false
    );
    EXPECT_TRUE(infoAll.shouldDisplay);
    ASSERT_EQ(infoAll.tokens.size(), 1u);
    EXPECT_EQ(infoAll.tokens[0], "F5");
}

TEST(KeycastTranslatorTest, StandardKeyNamesNoSpaceTearing) {
    // 验证特殊按键名称无空格撕裂
    EXPECT_EQ(KeyTranslator::getStandardKeyName(VK_PRIOR), "PageUp");
    EXPECT_EQ(KeyTranslator::getStandardKeyName(VK_NEXT), "PageDown");
    EXPECT_EQ(KeyTranslator::getStandardKeyName(VK_CAPITAL), "CapsLock");
    EXPECT_EQ(KeyTranslator::getStandardKeyName(VK_NUMLOCK), "NumLock");
    EXPECT_EQ(KeyTranslator::getStandardKeyName(VK_SCROLL), "ScrollLock");
    EXPECT_EQ(KeyTranslator::getStandardKeyName(VK_SPACE), "Space");
}

TEST(KeycastTranslatorTest, FormatComboUtility) {
    EXPECT_EQ(KeyTranslator::formatCombo({"Ctrl", "Shift", "P"}), "Ctrl + Shift + P");
    EXPECT_EQ(KeyTranslator::formatCombo({"*"}), "*");
    EXPECT_EQ(KeyTranslator::formatCombo({"+"}), "+");
    EXPECT_EQ(KeyTranslator::formatCombo({}), "");
}

TEST(KeycastTranslatorTest, TranslateKeyToUnicodeSafeFallback) {
    // 静态保底与 Unicode 安全调用测试
    std::string s8 = KeyTranslator::translateKeyToUnicode('8', 0, true);
    EXPECT_EQ(s8, "*");

    std::string sEq = KeyTranslator::translateKeyToUnicode(VK_OEM_PLUS, 0, true);
    EXPECT_EQ(sEq, "+");

    std::string sSlash = KeyTranslator::translateKeyToUnicode(VK_OEM_2, 0, true);
    EXPECT_EQ(sSlash, "?");
}
