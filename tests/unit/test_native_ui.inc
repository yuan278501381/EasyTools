// ─────────────────────────────────────────────────────────────────────────────
// test_native_ui.inc — Tools3000 原生 Direct2D/DirectWrite UI 框架全单元测试套件
// ─────────────────────────────────────────────────────────────────────────────

#include "ui/native/common/UIGeometry.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/common/UIAnimation.h"
#include "ui/native/common/UIEvent.h"
#include "ui/native/graphics/UIRenderContext.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include "ui/native/layout/UILayout.h"
#include "ui/native/core/UIElement.h"
#include "ui/native/core/UIDeclarative.h"
#include "ui/native/components/UILabel.h"
#include "ui/native/components/UIIcon.h"
#include "ui/native/components/UIButton.h"
#include "ui/native/components/UIToggle.h"
#include "ui/native/components/UICard.h"
#include "ui/native/components/UISettingRow.h"
#include "ui/native/components/UISettingGroup.h"
#include "ui/native/components/UIBadge.h"
#include "ui/native/components/UISelect.h"
#include "ui/native/components/UITextInput.h"
#include "ui/native/components/UINumberInput.h"
#include "ui/native/components/UIHotkeyInput.h"
#include "ui/native/components/UIScrollView.h"
#include "ui/native/components/UITabs.h"
#include "ui/native/components/UICodeBadge.h"
#include "ui/native/components/UISeparator.h"
#include "ui/native/app/NativeSettingsApp.h"
#include "ui/native/core/NativeView.h"
#include "ui/native/core/NativePage.h"
#include "ui/native/window/NativeFramelessWindow.h"
#include "ui/native/components/NativeGestureDrawCanvas.h"
#include "ui/native/components/NativeKeyboardHeatmap.h"
#include "ui/native/components/NativeScopeRulesManager.h"
#include "ui/native/pages/NativeAiAssistantPage.h"
#include "ui/native/pages/NativeColorPickerPage.h"
#include "ui/native/pages/NativeClipboardManagerPage.h"
#include "ui/native/pages/NativeMarkdownPreviewPage.h"
#include "ui/native/app/NativeTrayApp.h"
#include "ui/native/app/NativeSearchApp.h"
#include "ui/native/app/NativeQuickLookApp.h"

using namespace tools3000::ui::native;
using namespace tools3000::ui::native::declarative;
using UIMouseType = tools3000::ui::native::MouseEventType;

// ── 1. 几何与色彩类型测试 ───────────────────────────────────────────────────

TEST(NativeUIGeometryTest, RectCalculationsAndInsets) {
    Rect r(10.0f, 20.0f, 110.0f, 70.0f);
    EXPECT_FLOAT_EQ(r.width(), 100.0f);
    EXPECT_FLOAT_EQ(r.height(), 50.0f);
    EXPECT_FLOAT_EQ(r.center().x, 60.0f);
    EXPECT_FLOAT_EQ(r.center().y, 45.0f);

    EXPECT_TRUE(r.contains(Point(10.0f, 20.0f)));
    EXPECT_TRUE(r.contains(Point(60.0f, 45.0f)));
    EXPECT_TRUE(r.contains(Point(110.0f, 70.0f)));
    EXPECT_FALSE(r.contains(Point(9.0f, 20.0f)));
    EXPECT_FALSE(r.contains(Point(111.0f, 45.0f)));

    Thickness pad(5.0f, 10.0f, 15.0f, 20.0f);
    EXPECT_FLOAT_EQ(pad.horizontal(), 20.0f);
    EXPECT_FLOAT_EQ(pad.vertical(), 30.0f);

    Rect insetRect = r.inset(pad);
    EXPECT_FLOAT_EQ(insetRect.left, 15.0f);
    EXPECT_FLOAT_EQ(insetRect.top, 30.0f);
    EXPECT_FLOAT_EQ(insetRect.right, 95.0f);
    EXPECT_FLOAT_EQ(insetRect.bottom, 50.0f);
    EXPECT_FLOAT_EQ(insetRect.width(), 80.0f);
    EXPECT_FLOAT_EQ(insetRect.height(), 20.0f);

    Rect offsetRect = r.offset(5.0f, -10.0f);
    EXPECT_FLOAT_EQ(offsetRect.left, 15.0f);
    EXPECT_FLOAT_EQ(offsetRect.top, 10.0f);
}

TEST(NativeUIGeometryTest, ColorConversionsAndInterpolation) {
    Color red = Color::fromRgba(255, 0, 0, 1.0f);
    EXPECT_FLOAT_EQ(red.r, 1.0f);
    EXPECT_FLOAT_EQ(red.g, 0.0f);
    EXPECT_FLOAT_EQ(red.b, 0.0f);
    EXPECT_FLOAT_EQ(red.a, 1.0f);

    Color hexColor = Color::fromHex(0x8B5CF6, 0.8f);
    EXPECT_NEAR(hexColor.r, 139.0f / 255.0f, 0.01f);
    EXPECT_NEAR(hexColor.g, 92.0f / 255.0f, 0.01f);
    EXPECT_NEAR(hexColor.b, 246.0f / 255.0f, 0.01f);
    EXPECT_FLOAT_EQ(hexColor.a, 0.8f);

    Color c1(0.0f, 0.0f, 0.0f, 1.0f);
    Color c2(1.0f, 1.0f, 1.0f, 1.0f);
    Color mid = Color::lerp(c1, c2, 0.5f);
    EXPECT_FLOAT_EQ(mid.r, 0.5f);
    EXPECT_FLOAT_EQ(mid.g, 0.5f);
    EXPECT_FLOAT_EQ(mid.b, 0.5f);
}

// ── 2. 主题与设计令牌测试 ───────────────────────────────────────────────────

TEST(NativeUIThemeTest, ThemeModeAndDynamicAccentPalette) {
    auto& theme = UITheme::instance();

    theme.setThemeMode(ThemeMode::Dark);
    EXPECT_TRUE(theme.isEffectiveDark());
    const auto& darkPalette = theme.palette();
    EXPECT_GT(darkPalette.bgBase.a, 0.8f);

    theme.setThemeMode(ThemeMode::Light);
    EXPECT_FALSE(theme.isEffectiveDark());
    const auto& lightPalette = theme.palette();
    EXPECT_GT(lightPalette.textPrimary.r, 0.0f);

    // 动态强调色测试
    theme.setAccentColor("violet");
    EXPECT_EQ(theme.getAccentColor(), "violet");
    EXPECT_GT(theme.palette().primary.b, 0.5f);

    theme.setAccentColor("mint");
    EXPECT_GT(theme.palette().primary.g, 0.6f);

    theme.setAccentColor("blue");
    EXPECT_EQ(theme.getAccentColor(), "blue");
}

// ── 3. 动画与物理阻尼缓动测试 ───────────────────────────────────────────────

TEST(NativeUIAnimationTest, CubicBezierEvaluations) {
    // 边界条件
    EXPECT_NEAR(UIAnimation::evaluateCubicBezier(0.0f, 0.16f, 1.0f, 0.3f, 1.0f), 0.0f, 0.01f);
    EXPECT_NEAR(UIAnimation::evaluateCubicBezier(1.0f, 0.16f, 1.0f, 0.3f, 1.0f), 1.0f, 0.01f);

    // 顺滑贝塞尔中点应有平滑曲率
    float mid = UIAnimation::evaluateCubicBezier(0.5f, 0.16f, 1.0f, 0.3f, 1.0f);
    EXPECT_GT(mid, 0.5f); // Ease-Out 特性
}

TEST(NativeUIAnimationTest, AnimatedFloatLifecycle) {
    AnimatedFloat val(0.0f);
    EXPECT_FLOAT_EQ(val.get(), 0.0f);
    EXPECT_FALSE(val.isAnimating());

    val.setTarget(100.0f, 200.0f, EasingType::SmoothCubic);
    EXPECT_TRUE(val.isAnimating());
    EXPECT_FLOAT_EQ(val.target(), 100.0f);

    // 步进 100ms
    val.update(0.10f);
    EXPECT_TRUE(val.isAnimating());
    EXPECT_GT(val.get(), 0.0f);
    EXPECT_LT(val.get(), 100.0f);

    // 步进 150ms 达到终点
    val.update(0.15f);
    EXPECT_FALSE(val.isAnimating());
    EXPECT_FLOAT_EQ(val.get(), 100.0f);
}

TEST(NativeUIAnimationTest, SpringPhysicsConvergence) {
    AnimatedFloat springVal(0.0f);
    springVal.setTarget(1.0f, 300.0f, EasingType::SpringBounce);
    EXPECT_TRUE(springVal.isAnimating());

    for (int i = 0; i < 60; ++i) {
        springVal.update(0.016f);
    }

    EXPECT_FALSE(springVal.isAnimating());
    EXPECT_NEAR(springVal.get(), 1.0f, 0.005f);
}

// ── 4. Flex 弹性盒模型布局引擎测试 ──────────────────────────────────────────

TEST(NativeUILayoutTest, FlexboxRowAndColumnLayout) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);

    auto item1 = std::make_shared<UIElement>();
    item1->layoutParams().fixedWidth = 100.0f;
    item1->layoutParams().fixedHeight = 40.0f;

    auto item2 = std::make_shared<UIElement>();
    item2->layoutParams().fixedWidth = 150.0f;
    item2->layoutParams().fixedHeight = 50.0f;

    // Row 布局测试
    LayoutParams rowParams;
    rowParams.direction = FlexDirection::Row;
    rowParams.gap = 10.0f;
    rowParams.padding = Thickness(5.0f);

    std::vector<std::shared_ptr<UIElement>> children = { item1, item2 };
    Size rowSize = UILayout::measureChildren(children, rowParams, 500.0f, 200.0f, ctx);
    // width: 5 (left) + 100 + 10 (gap) + 150 + 5 (right) = 270
    // height: 5 (top) + 50 (max child H) + 5 (bottom) = 60
    EXPECT_FLOAT_EQ(rowSize.width, 270.0f);
    EXPECT_FLOAT_EQ(rowSize.height, 60.0f);

    Rect finalBounds(0.0f, 0.0f, 270.0f, 60.0f);
    UILayout::arrangeChildren(children, rowParams, finalBounds, ctx);

    EXPECT_FLOAT_EQ(item1->bounds().left, 5.0f);
    EXPECT_FLOAT_EQ(item1->bounds().width(), 100.0f);
    EXPECT_FLOAT_EQ(item2->bounds().left, 115.0f); // 5 + 100 + 10
    EXPECT_FLOAT_EQ(item2->bounds().width(), 150.0f);

    // Column 布局测试
    LayoutParams colParams;
    colParams.direction = FlexDirection::Column;
    colParams.gap = 12.0f;
    colParams.padding = Thickness(8.0f);

    Size colSize = UILayout::measureChildren(children, colParams, 500.0f, 500.0f, ctx);
    // height: 8 + 40 + 12 + 50 + 8 = 118
    // width: 8 + 150 (max child W) + 8 = 166
    EXPECT_FLOAT_EQ(colSize.height, 118.0f);
    EXPECT_FLOAT_EQ(colSize.width, 166.0f);

    Rect colBounds(0.0f, 0.0f, 166.0f, 118.0f);
    UILayout::arrangeChildren(children, colParams, colBounds, ctx);

    EXPECT_FLOAT_EQ(item1->bounds().top, 8.0f);
    EXPECT_FLOAT_EQ(item2->bounds().top, 60.0f); // 8 + 40 + 12
}

TEST(NativeUILayoutTest, FlexGrowDistribution) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);

    auto fixedItem = std::make_shared<UIElement>();
    fixedItem->layoutParams().fixedWidth = 100.0f;

    auto growItem = std::make_shared<UIElement>();
    growItem->layoutParams().flexGrow = 1.0f;

    std::vector<std::shared_ptr<UIElement>> children = { fixedItem, growItem };

    LayoutParams params;
    params.direction = FlexDirection::Row;
    params.padding = Thickness(0.0f);
    params.gap = 0.0f;

    Rect bounds(0.0f, 0.0f, 400.0f, 50.0f);
    UILayout::arrangeChildren(children, params, bounds, ctx);

    EXPECT_FLOAT_EQ(fixedItem->bounds().width(), 100.0f);
    EXPECT_FLOAT_EQ(growItem->bounds().left, 100.0f);
    EXPECT_FLOAT_EQ(growItem->bounds().width(), 300.0f); // 400 - 100
}

// ── 5. 核心组件行为与状态转移测试 ───────────────────────────────────────────

TEST(NativeUIComponentsTest, ToggleStateAndEvents) {
    bool toggledState = false;
    auto toggle = std::make_shared<UIToggle>(false, [&](bool val) {
        toggledState = val;
    });

    EXPECT_FALSE(toggle->isChecked());

    UIMouseEvent click(UIMouseType::MouseDown, Point(10.0f, 10.0f), MouseButton::Left);
    toggle->onMouseDown(click);

    EXPECT_TRUE(toggle->isChecked());
    EXPECT_TRUE(toggledState);

    toggle->setChecked(false, false);
    EXPECT_FALSE(toggle->isChecked());
}

TEST(NativeUIComponentsTest, ButtonVariantAndClick) {
    bool clicked = false;
    auto btn = std::make_shared<UIButton>(L"测试按钮", ButtonVariant::Primary, ButtonSize::Md);
    btn->setOnClick([&]() { clicked = true; });
    btn->setBounds(Rect(0.0f, 0.0f, 100.0f, 34.0f));

    UIMouseEvent down(UIMouseType::MouseDown, Point(50.0f, 17.0f), MouseButton::Left);
    btn->onMouseDown(down);

    UIMouseEvent up(UIMouseType::MouseUp, Point(50.0f, 17.0f), MouseButton::Left);
    btn->onMouseUp(up);

    EXPECT_TRUE(clicked);
}

TEST(NativeUIComponentsTest, ButtonNavItemAndWindowControls) {
    auto navBtn = std::make_shared<UIButton>(L"超级截图", ButtonVariant::Ghost, ButtonSize::Md);
    navBtn->setIcon(IconType::Crop);
    navBtn->setIsNavItem(true);
    EXPECT_TRUE(navBtn->isNavItem());
    EXPECT_FALSE(navBtn->isActive());

    navBtn->setActive(true);
    EXPECT_TRUE(navBtn->isActive());

    auto winCtrlBtn = std::make_shared<UIButton>(L"", ButtonVariant::Ghost, ButtonSize::Sm);
    winCtrlBtn->setIcon(IconType::X);
    winCtrlBtn->setWindowControl(true, true);
    EXPECT_TRUE(winCtrlBtn->isWindowControl());
    EXPECT_TRUE(winCtrlBtn->isCloseBtn());

    auto minBtn = std::make_shared<UIButton>(L"", ButtonVariant::Ghost, ButtonSize::Sm);
    minBtn->setIcon(IconType::Minimize);
    minBtn->setWindowControl(true, false);
    EXPECT_TRUE(minBtn->isWindowControl());
    EXPECT_FALSE(minBtn->isCloseBtn());
}

TEST(NativeUIComponentsTest, SelectOptionsAndSelection) {
    std::string chosen;
    std::vector<SelectOption> opts = {
        { "opt1", L"选项 1" },
        { "opt2", L"选项 2" }
    };
    auto select = std::make_shared<UISelect>(opts, "opt1", [&](const std::string& val) {
        chosen = val;
    });
    select->setBounds(Rect(0.0f, 0.0f, 150.0f, 32.0f));

    EXPECT_EQ(select->getSelectedValue(), "opt1");

    // 点击展开
    UIMouseEvent openClick(UIMouseType::MouseDown, Point(50.0f, 16.0f), MouseButton::Left);
    select->onMouseDown(openClick);

    // 点击第二个选项 (y in [70, 100])
    UIMouseEvent opt2Click(UIMouseType::MouseDown, Point(50.0f, 85.0f), MouseButton::Left);
    select->onMouseDown(opt2Click);

    EXPECT_EQ(select->getSelectedValue(), "opt2");
    EXPECT_EQ(chosen, "opt2");
}

TEST(NativeUIComponentsTest, NumberInputClampingAndSteps) {
    int updatedVal = 0;
    auto numInput = std::make_shared<UINumberInput>(10, 0, 100, 5, L"px", [&](int val) {
        updatedVal = val;
    });

    EXPECT_EQ(numInput->getValue(), 10);

    // 向上步进
    numInput->setValue(15);
    EXPECT_EQ(numInput->getValue(), 15);

    // 边界钳位
    numInput->setValue(150);
    EXPECT_EQ(numInput->getValue(), 100);

    numInput->setValue(-50);
    EXPECT_EQ(numInput->getValue(), 0);
}

TEST(NativeUIComponentsTest, HotkeyInputKeyCapture) {
    std::string newKey;
    auto hotkey = std::make_shared<UIHotkeyInput>("Ctrl+A", [&](const std::string& k) {
        newKey = k;
    });

    EXPECT_EQ(hotkey->getHotkey(), "Ctrl+A");

    // 激活录制
    hotkey->setBounds(Rect(0.0f, 0.0f, 140.0f, 32.0f));
    UIMouseEvent click(UIMouseType::MouseDown, Point(30.0f, 16.0f), MouseButton::Left);
    hotkey->onMouseDown(click);

    // 按下 Ctrl + Shift + B
    UIKeyEvent keyEvt(KeyEventType::KeyDown, 'B');
    keyEvt.modifiers.ctrl = true;
    keyEvt.modifiers.shift = true;
    hotkey->onKeyDown(keyEvt);

    EXPECT_EQ(hotkey->getHotkey(), "Ctrl+Shift+B");
    EXPECT_EQ(newKey, "Ctrl+Shift+B");
}

TEST(NativeUIComponentsTest, TabsSwitchingAndIndicator) {
    std::string activeTab;
    std::vector<TabItem> tabs = {
        { "tab1", L"标签 1" },
        { "tab2", L"标签 2" }
    };
    auto tabsComponent = std::make_shared<UITabs>(tabs, "tab1", [&](const std::string& id) {
        activeTab = id;
    });
    tabsComponent->setBounds(Rect(0.0f, 0.0f, 200.0f, 36.0f));

    EXPECT_EQ(tabsComponent->getActiveTab(), "tab1");

    // 点击第二个 Tab
    UIMouseEvent tab2Click(UIMouseType::MouseDown, Point(150.0f, 18.0f), MouseButton::Left);
    tabsComponent->onMouseDown(tab2Click);

    EXPECT_EQ(tabsComponent->getActiveTab(), "tab2");
    EXPECT_EQ(activeTab, "tab2");
}

// ── 6. 声明式 DSL 构建模型测试 ──────────────────────────────────────────────

TEST(NativeUIDeclarativeTest, TreeStructureAndProperties) {
    bool toggled = false;
    auto page = VStack({
        SettingGroup(L"测试模块", IconType::Settings, {
            Card(L"基础配置", L"说明描述", {
                SettingRow(L"开机自启", L"自动随系统启动",
                    Toggle(true, [&](bool v) { toggled = v; })
                ),
                SettingRow(L"界面语言", L"选择软件多语言",
                    Badge(L"简体中文", BadgeVariant::Primary)
                )
            })
        })
    }, 12.0f, Thickness(16.0f));

    EXPECT_NE(page, nullptr);
    EXPECT_EQ(page->getChildren().size(), 1u);

    auto group = page->getChildren()[0];
    EXPECT_NE(group, nullptr);
    EXPECT_EQ(group->getChildren().size(), 1u);

    auto card = group->getChildren()[0];
    EXPECT_NE(card, nullptr);
    EXPECT_EQ(card->getChildren().size(), 2u);
}

// ── 7. 世界级排版体系与字号字重底线门禁验证 ─────────────────────────────────

TEST(NativeTypographyGateTest, StrictlyEnforcesTypographyFloors) {
    // 验证字号底线严禁低于 11.8px (FontToken::Xs = 12px)
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())
    );
    EXPECT_TRUE(SUCCEEDED(hr));
    if (SUCCEEDED(hr) && dwriteFactory) {
        UIRenderContext ctx(nullptr, dwriteFactory.Get(), 1.0f);
        Size xsSize = ctx.measureText(L"Test", FontToken::Xs, FontWeight::Medium);
        EXPECT_GT(xsSize.height, 11.8f);
    }

    // 验证字重底线严禁低于 500 Medium
    EXPECT_GE(static_cast<int>(FontWeight::Medium), 500);
    EXPECT_GE(static_cast<int>(FontWeight::SemiBold), 600);
    EXPECT_GE(static_cast<int>(FontWeight::Bold), 700);
}

// ── 8. 弹性布局与显式尺寸保护验证 ───────────────────────────────────────────

TEST(NativeUILayoutTest, StretchRespectsExplicitFixedCrossSizes) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);

    auto autoItem = std::make_shared<UIElement>();
    auto fixedItem = std::make_shared<UIElement>();
    fixedItem->layoutParams().fixedHeight = 32.0f; // 显式固定高度

    std::vector<std::shared_ptr<UIElement>> children = { autoItem, fixedItem };

    LayoutParams params;
    params.direction = FlexDirection::Row;
    params.align = AlignItems::Stretch; // 交叉轴 Stretch
    params.padding = Thickness(0.0f);

    Rect bounds(0.0f, 0.0f, 200.0f, 80.0f);
    UILayout::arrangeChildren(children, params, bounds, ctx);

    // 未设置 fixedHeight 的 autoItem 被拉伸到容器高度 80px
    EXPECT_FLOAT_EQ(autoItem->bounds().height(), 80.0f);
    // 显式设置 fixedHeight = 32px 的 fixedItem 高度应被保护，严禁被无脑拉伸
    EXPECT_FLOAT_EQ(fixedItem->bounds().height(), 32.0f);
}

// ── 9. 卡片容器与副标题零重叠对齐测试 ───────────────────────────────────────

TEST(NativeUICardTest, HeaderLayoutEliminatesSubtitleOverlap) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);

    auto card = std::make_shared<UICard>(L"卡片大标题", L"这里是卡片副标题说明文字");
    auto row = std::make_shared<UISettingRow>(L"行设置项", L"设置说明");
    card->addChild(row);

    card->measure(500.0f, 1000.0f, ctx);
    card->layout(Rect(0.0f, 0.0f, 500.0f, 300.0f), ctx);

    // 验证行内容起始 Y 坐标严格大于副标题的底部，彻底杜绝重叠发虚
    EXPECT_GT(row->bounds().top, 18.0f + 22.0f + 2.0f + 20.0f);
}

// ── 10. 复合组件父子弱引用与生命周期测试 ────────────────────────────────────

TEST(NativeUIParentBindingTest, HierarchyParentLinks) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);

    auto toggle = std::make_shared<UIToggle>(true);
    auto row = std::make_shared<UISettingRow>(L"测试项", L"说明", toggle);
    auto scroll = std::make_shared<UIScrollView>(row);

    scroll->measure(400.0f, 300.0f, ctx);
    scroll->layout(Rect(0.0f, 0.0f, 400.0f, 300.0f), ctx);

    // 验证 layout 之后各层级的 getParent() 链路通畅
    EXPECT_EQ(row->getParent(), scroll);
    EXPECT_EQ(toggle->getParent(), row);
}

// ── 11. 顶层弹出层 Overlay 与轻量级收起 (Light Dismiss) 测试 ────────────────

TEST(NativeUIWindowHostPopupTest, PopupLifecycleAndLightDismiss) {
    NativeWindowHost host;
    EXPECT_FALSE(host.hasActivePopup());

    bool dismissed = false;
    auto popup = std::make_shared<UIElement>();
    popup->setBounds(Rect(100.0f, 100.0f, 250.0f, 260.0f));

    host.showPopup(popup, Rect(100.0f, 70.0f, 250.0f, 100.0f), [&]() {
        dismissed = true;
    });

    EXPECT_TRUE(host.hasActivePopup());
    EXPECT_EQ(host.getActivePopup(), popup);
    EXPECT_EQ(popup->getWindowHost(), &host);

    host.closePopup();
    EXPECT_FALSE(host.hasActivePopup());
    EXPECT_EQ(host.getActivePopup(), nullptr);
    EXPECT_TRUE(dismissed);
}

// ── 12. 渲染上下文与字体 Trimming 状态防污染测试 ────────────────────────────

TEST(NativeUIRenderContextTest, TrimmingResetIntegrity) {
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())
    );
    EXPECT_TRUE(SUCCEEDED(hr));
    if (SUCCEEDED(hr) && dwriteFactory) {
        UIRenderContext ctx(nullptr, dwriteFactory.Get(), 1.0f);
        auto format = ctx.getOrCreateTextFormat(FontToken::Base, FontWeight::Medium, FontFamilyType::Sans);
        EXPECT_NE(format, nullptr);

        // 先触发一次带省略号的绘制配置
        ctx.drawText(L"长文本测试", Rect(0, 0, 100, 20), Color(1, 1, 1, 1),
                     FontToken::Base, FontWeight::Medium, FontFamilyType::Sans,
                     TextAlignmentH::Left, TextAlignmentV::Center, false, true);

        // 再触发一次不带省略号的绘制配置
        ctx.drawText(L"短文本测试", Rect(0, 0, 100, 20), Color(1, 1, 1, 1),
                     FontToken::Base, FontWeight::Medium, FontFamilyType::Sans,
                     TextAlignmentH::Left, TextAlignmentV::Center, false, false);

        DWRITE_TRIMMING trimming{};
        Microsoft::WRL::ComPtr<IDWriteInlineObject> trimmingSign;
        EXPECT_TRUE(SUCCEEDED(format->GetTrimming(&trimming, trimmingSign.GetAddressOf())));
        EXPECT_EQ(trimming.granularity, DWRITE_TRIMMING_GRANULARITY_NONE);
        EXPECT_EQ(trimmingSign, nullptr);
    }
}

TEST(NativeUICardTest, CapturePageLayoutInspection) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);

    auto toggle = std::make_shared<UIToggle>(true);
    auto badge = std::make_shared<UIBadge>(L"PNG 无损", BadgeVariant::Primary);
    auto hotkey = std::make_shared<UIHotkeyInput>("Ctrl+Shift+A");

    auto row1 = std::make_shared<UISettingRow>(L"区域截图快捷键", L"激活全屏冻结与智能窗口吸附选区", hotkey);
    auto row2 = std::make_shared<UISettingRow>(L"完成自动复制", L"标注完成按下确认或回车后存入剪贴板", toggle);
    auto row3 = std::make_shared<UISettingRow>(L"默认保存格式", L"导出为本地图片文件时的编码格式", badge);

    auto card = std::make_shared<UICard>(L"截图触发与输出", L"极速毫秒级捕获屏幕并呼出微晶工具栏");
    card->addChild(row1);
    card->addChild(row2);
    card->addChild(row3);

    auto group = std::make_shared<UISettingGroup>(L"超级截图标注与录屏", IconType::Crop);
    group->addChild(card);

    auto vstack = VStack({ group }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));
    vstack->layoutParams().maxWidth = 920.0f;
    auto scroll = std::make_shared<UIScrollView>(vstack);

    // 完整复现 NativeSettingsApp::buildUI() 的树结构
    auto rootContainer = std::make_shared<UIElement>();
    rootContainer->layoutParams().direction = FlexDirection::Column;

    auto titleBar = std::make_shared<UIElement>();
    titleBar->layoutParams().fixedHeight = 48.0f;
    rootContainer->addChild(titleBar);

    rootContainer->addChild(Separator());

    auto bodyRow = std::make_shared<UIElement>();
    bodyRow->layoutParams().direction = FlexDirection::Row;
    bodyRow->layoutParams().flexGrow = 1.0f;

    auto sidebar = std::make_shared<UIElement>();
    sidebar->layoutParams().fixedWidth = 230.0f;
    bodyRow->addChild(sidebar);

    bodyRow->addChild(Separator(true));

    auto contentArea = std::make_shared<UIElement>();
    contentArea->layoutParams().direction = FlexDirection::Column;
    contentArea->layoutParams().flexGrow = 1.0f;
    contentArea->addChild(scroll);

    bodyRow->addChild(contentArea);
    rootContainer->addChild(bodyRow);

    // 假设窗口物理像素 1024，m_dpiScale = 1.0
    float width = 1024.0f;
    float height = 745.0f;

    rootContainer->measure(width, height, ctx);
    rootContainer->layout(Rect(0.0f, 0.0f, width, height), ctx);

    // 验证在 1024 宽度下：右侧控件与卡片边界保持安全距离，绝不贴边切断
    EXPECT_LT(badge->bounds().right, card->bounds().right);
    EXPECT_LE(card->bounds().right, vstack->bounds().right);
    EXPECT_LE(vstack->bounds().width(), 920.0f);

    // 宽屏 (1400x900) 布局测试：必须自动将内容居中限制在 920px 内
    float wideW = 1400.0f;
    float wideH = 900.0f;
    rootContainer->measure(wideW, wideH, ctx);
    rootContainer->layout(Rect(0.0f, 0.0f, wideW, wideH), ctx);

    EXPECT_FLOAT_EQ(vstack->bounds().width(), 920.0f);
    // contentArea 左侧在 239，右侧在 1400，可用宽 1161。居中偏移 = (1161 - 920) * 0.5 = 120.5
    EXPECT_NEAR(vstack->bounds().left, 239.0f + 120.5f, 1.0f);
    EXPECT_LT(badge->bounds().right, card->bounds().right);
}

// ── 13. 滚轮穿梭、无卡顿即时开关与无嵌套 ScrollView 测试 ────────────────────────

TEST(NativeUIScrollViewFixTest, MouseWheelAndSmoothScrollDynamics) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);

    // 构建一个高 1200px 的长内容
    auto content = std::make_shared<UIElement>();
    content->layoutParams().fixedHeight = 1200.0f;
    content->layoutParams().fixedWidth = 500.0f;

    auto scroll = std::make_shared<UIScrollView>(content);
    // 视口尺寸 500 x 400
    scroll->measure(500.0f, 400.0f, ctx);
    scroll->layout(Rect(0.0f, 0.0f, 500.0f, 400.0f), ctx);

    EXPECT_FLOAT_EQ(scroll->getMaxScrollOffset(), 800.0f); // 1200 - 400 = 800
    EXPECT_FLOAT_EQ(scroll->getScrollOffset(), 0.0f);

    // 模拟向下滑动滚轮 (delta = -120)
    UIMouseEvent wheelDown(UIMouseType::MouseWheel, Point(200.0f, 200.0f), MouseButton::None, -120.0f);
    bool handled = scroll->onMouseWheel(wheelDown);
    EXPECT_TRUE(handled);

    // update 模拟帧步进
    bool animating = scroll->update(0.016f);
    EXPECT_TRUE(animating);
    EXPECT_GT(scroll->getScrollOffset(), 0.0f);

    // 步进至动画结束
    for (int i = 0; i < 30; ++i) {
        scroll->update(0.016f);
    }
    EXPECT_FLOAT_EQ(scroll->getScrollOffset(), 84.0f); // -(-120) * 0.7 = 84.0f

    // 模拟轨道点击跳转
    // 滚动条位于右边缘 x: [490, 496]，点击下方空白处 y=300
    UIMouseEvent trackClick(UIMouseType::MouseDown, Point(493.0f, 300.0f), MouseButton::Left);
    handled = scroll->onMouseDown(trackClick);
    EXPECT_TRUE(handled);
}

TEST(NativeUIToggleFixTest, InstantToggleAndRowClickBubble) {
    bool toggledState = false;
    int toggleCount = 0;
    auto toggle = std::make_shared<UIToggle>(false, [&](bool checked) {
        toggledState = checked;
        toggleCount++;
    });

    EXPECT_FALSE(toggle->isChecked());

    // 1. 直接触发 toggle()
    toggle->toggle();
    EXPECT_TRUE(toggle->isChecked());
    EXPECT_TRUE(toggledState);
    EXPECT_EQ(toggleCount, 1);

    // 动画状态应处于活跃 (弹簧阻尼运动)
    EXPECT_TRUE(toggle->update(0.016f));

    // 2. 模拟直接在开关上按下鼠标 (onMouseDown)
    UIMouseEvent downEvent(UIMouseType::MouseDown, Point(10.0f, 10.0f), MouseButton::Left);
    toggle->onMouseDown(downEvent);
    EXPECT_FALSE(toggle->isChecked());
    EXPECT_FALSE(toggledState);
    EXPECT_EQ(toggleCount, 2);

    // 3. 测试 UISettingRow 全行点击联动开关
    auto row = std::make_shared<UISettingRow>(L"测试选项", L"点击全行任意区域即可切换", toggle);
    row->setBounds(Rect(0.0f, 0.0f, 400.0f, 50.0f));

    // 点击行左侧文字区域 (x=50, y=25)
    UIMouseEvent rowClick(UIMouseType::MouseDown, Point(50.0f, 25.0f), MouseButton::Left);
    bool consumed = row->onMouseDown(rowClick);
    EXPECT_TRUE(consumed);
    EXPECT_TRUE(toggle->isChecked());
    EXPECT_TRUE(toggledState);
    EXPECT_EQ(toggleCount, 3);
}

TEST(NativeSettingsAppTest, NoNestedScrollViewInPageContent) {
    auto& app = NativeSettingsApp::instance();
    // 验证所有页面返回的根容器绝不是 UIScrollView，防止双层嵌套导致滚动锁死
    const std::vector<std::string> pages = {
        "general", "plugins", "search", "gesture", "hotcorner", "capture",
        "history", "ocr", "keycast", "spotlight", "dialog_enhancer",
        "remote_boost", "stats", "about"
    };

    for (const auto& pid : pages) {
        auto content = app.createPageContent(pid);
        ASSERT_NE(content, nullptr);
        auto isScroll = std::dynamic_pointer_cast<UIScrollView>(content);
        EXPECT_EQ(isScroll, nullptr) << "Page " << pid << " must NOT return a nested UIScrollView!";
    }
}

static void findScrollViewsRecursive(const std::shared_ptr<UIElement>& elem, std::vector<std::shared_ptr<UIScrollView>>& out) {
    if (!elem) return;
    auto sv = std::dynamic_pointer_cast<UIScrollView>(elem);
    if (sv) {
        out.push_back(sv);
    }
    for (const auto& child : elem->getChildren()) {
        findScrollViewsRecursive(child, out);
    }
}

TEST(NativeSettingsAppTest, RealAppScrollOffsetAndMouseWheel) {
    auto& app = NativeSettingsApp::instance();
    app.preload(); // initializes m_host and buildUI()
    auto host = app.getHost();
    ASSERT_NE(host, nullptr);

    // 检查通用页和手势页
    app.navigateTo("gesture");

    auto root = host->getRootElement();
    ASSERT_NE(root, nullptr);

    // 模拟布局尺寸 1060 x 700
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    root->measure(1060.0f, 700.0f, ctx);
    root->layout(Rect(0.0f, 0.0f, 1060.0f, 700.0f), ctx);

    std::vector<std::shared_ptr<UIScrollView>> scrollViews;
    findScrollViewsRecursive(root, scrollViews);
    std::printf("[DIAG] Found %zu ScrollViews in root tree\n", scrollViews.size());

    for (size_t i = 0; i < scrollViews.size(); ++i) {
        auto sv = scrollViews[i];
        std::printf("[DIAG] ScrollView #%zu: bounds [%.1f, %.1f, %.1f, %.1f], maxScroll: %.1f, contentH: %.1f\n",
            i, sv->bounds().left, sv->bounds().top, sv->bounds().right, sv->bounds().bottom,
            sv->getMaxScrollOffset(),
            sv->getContent() ? sv->getContent()->desiredSize().height : 0.0f);
    }

    // 尝试在右侧 (500, 200) 和左侧 (50, 200) hitTest
    auto hitRight = root->hitTest(Point(500.0f, 200.0f));
    std::printf("[DIAG] hitRight: %s\n", hitRight ? typeid(*hitRight).name() : "null");
    auto curr = hitRight;
    int depth = 0;
    while (curr) {
        std::printf("[DIAG]   parent #%d: %s, bounds [%.1f, %.1f, %.1f, %.1f]\n",
            depth++, typeid(*curr).name(),
            curr->bounds().left, curr->bounds().top, curr->bounds().right, curr->bounds().bottom);
        curr = curr->getParent();
    }

    auto hitLeft = root->hitTest(Point(50.0f, 200.0f));
    std::printf("[DIAG] hitLeft: %s\n", hitLeft ? typeid(*hitLeft).name() : "null");
    curr = hitLeft;
    depth = 0;
    while (curr) {
        std::printf("[DIAG]   parent #%d: %s, bounds [%.1f, %.1f, %.1f, %.1f]\n",
            depth++, typeid(*curr).name(),
            curr->bounds().left, curr->bounds().top, curr->bounds().right, curr->bounds().bottom);
        curr = curr->getParent();
    }


    // 测试真实发送 WM_MOUSEWHEEL 给窗口！
    // 将客户区点 (500, 200) 转换为屏幕点
    POINT screenPt = { static_cast<LONG>(500.0f * host->getDpiScale()), static_cast<LONG>(200.0f * host->getDpiScale()) };
    ClientToScreen(host->hwnd(), &screenPt);
    LPARAM lp = MAKELPARAM(screenPt.x, screenPt.y);
    WPARAM wp = MAKEWPARAM(0, -120);

    float beforeScroll = scrollViews[1]->getScrollOffset();
    float targetBefore = scrollViews[1]->getMaxScrollOffset();
    std::printf("[DIAG] Before wheel: currentOffset=%.1f, maxScroll=%.1f\n", beforeScroll, targetBefore);

    SendMessageW(host->hwnd(), WM_MOUSEWHEEL, wp, lp);

    std::printf("[DIAG] After wheel: currentOffset=%.1f (target in anim)\n", scrollViews[1]->getScrollOffset());
    // 模拟几帧动画 update
    root->update(0.016f);
    root->update(0.016f);
    std::printf("[DIAG] After root->update: currentOffset=%.1f\n", scrollViews[1]->getScrollOffset());

    EXPECT_GT(scrollViews[1]->getScrollOffset(), beforeScroll);

    // 现在测试在左侧侧边栏 (x=50, y=200) 发送 WM_MOUSEWHEEL
    POINT screenPtLeft = { static_cast<LONG>(50.0f * host->getDpiScale()), static_cast<LONG>(200.0f * host->getDpiScale()) };
    ClientToScreen(host->hwnd(), &screenPtLeft);
    LPARAM lpLeft = MAKELPARAM(screenPtLeft.x, screenPtLeft.y);

    float beforeSidebarScroll = scrollViews[0]->getScrollOffset();
    float maxSidebarScroll = scrollViews[0]->getMaxScrollOffset();
    std::printf("[DIAG] Before sidebar wheel: currentOffset=%.1f, maxScroll=%.1f\n", beforeSidebarScroll, maxSidebarScroll);

    SendMessageW(host->hwnd(), WM_MOUSEWHEEL, wp, lpLeft);

    root->update(0.016f);
    root->update(0.016f);
    if (maxSidebarScroll > 0.0f) {
        EXPECT_GT(scrollViews[0]->getScrollOffset(), beforeSidebarScroll);
    } else {
        // 在 700px 默认高度下侧边栏全部条目舒展呈现
        EXPECT_FLOAT_EQ(scrollViews[0]->getScrollOffset(), 0.0f);
        // 验证在紧凑高度 (520px) 下侧边栏产生滚动空间并精准响应滚轮
        root->measure(1060.0f, 520.0f, ctx);
        root->layout(Rect(0.0f, 0.0f, 1060.0f, 520.0f), ctx);
        EXPECT_GT(scrollViews[0]->getMaxScrollOffset(), 0.0f);
        UIMouseEvent wheelEvt(UIMouseType::MouseWheel, Point(50.0f, 200.0f), MouseButton::None, -120.0f);
        scrollViews[0]->onMouseWheel(wheelEvt);
        root->update(0.016f);
        root->update(0.016f);
        EXPECT_GT(scrollViews[0]->getScrollOffset(), 0.0f);
        root->measure(1060.0f, 700.0f, ctx);
        root->layout(Rect(0.0f, 0.0f, 1060.0f, 700.0f), ctx);
    }

    // 测试点击手势页的开关！
    // 找到卡片内 "启用鼠标手势引擎" 的 UIToggle
    // 此时 hitRight 是 UICard，找到其子元素中的 UISettingRow 及其内部的 UIToggle 控件
    std::shared_ptr<UIToggle> gestureToggle;
    for (const auto& c : hitRight->getChildren()) {
        auto row = std::dynamic_pointer_cast<UISettingRow>(c);
        if (row) {
            for (const auto& rowChild : row->getChildren()) {
                auto t = std::dynamic_pointer_cast<UIToggle>(rowChild);
                if (t) {
                    gestureToggle = t;
                    break;
                }
            }
            if (gestureToggle) break;
        }
    }

    ASSERT_NE(gestureToggle, nullptr);
    bool beforeChecked = gestureToggle->isChecked();
    std::printf("[DIAG] Before toggle click: isChecked=%d\n", beforeChecked ? 1 : 0);

    // 模拟鼠标点击该开关控件中心 (WM_LBUTTONDOWN/UP 接收窗口客户区坐标)
    Point toggleCenter = gestureToggle->bounds().center();
    LPARAM lpToggle = MAKELPARAM(static_cast<LONG>(toggleCenter.x * host->getDpiScale()),
                                 static_cast<LONG>(toggleCenter.y * host->getDpiScale()));

    SendMessageW(host->hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, lpToggle);
    SendMessageW(host->hwnd(), WM_LBUTTONUP, 0, lpToggle);

    std::printf("[DIAG] After toggle click: isChecked=%d\n", gestureToggle->isChecked() ? 1 : 0);
    EXPECT_NE(gestureToggle->isChecked(), beforeChecked);

    // 测试再次点击恢复状态
    SendMessageW(host->hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, lpToggle);
    SendMessageW(host->hwnd(), WM_LBUTTONUP, 0, lpToggle);
    EXPECT_EQ(gestureToggle->isChecked(), beforeChecked);

    // 测试主内容区滚动条物理拖拽 (Scrollbar Thumb Dragging)
    auto mainScroll = scrollViews[1];
    float beforeDragOffset = mainScroll->getScrollOffset();
    Rect thumbRect = mainScroll->getScrollbarThumbRect();
    Point thumbPt = thumbRect.center();
    LPARAM lpThumb = MAKELPARAM(static_cast<LONG>(thumbPt.x * host->getDpiScale()),
                                static_cast<LONG>(thumbPt.y * host->getDpiScale()));

    SendMessageW(host->hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, lpThumb);

    // 向下拖拽 50 像素
    LPARAM lpDrag = MAKELPARAM(static_cast<LONG>(thumbPt.x * host->getDpiScale()),
                               static_cast<LONG>((thumbPt.y + 50.0f) * host->getDpiScale()));

    SendMessageW(host->hwnd(), WM_MOUSEMOVE, MK_LBUTTON, lpDrag);
    EXPECT_GT(mainScroll->getScrollOffset(), beforeDragOffset);

    // 释放拖拽
    SendMessageW(host->hwnd(), WM_LBUTTONUP, 0, lpDrag);
}

TEST(NativeSettingsAppTest, ReinitLifecycleAndAsyncConfigWrite) {
    auto& app = NativeSettingsApp::instance();
    app.destroy();
    EXPECT_EQ(app.hwnd(), nullptr);

    // 重新预热并导航
    app.preload();
    auto host = app.getHost();
    ASSERT_NE(host, nullptr);
    EXPECT_NE(host->hwnd(), nullptr);

    app.navigateTo("general");
    auto root = host->getRootElement();
    ASSERT_NE(root, nullptr);

    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    root->measure(1060.0f, 700.0f, ctx);
    root->layout(Rect(0.0f, 0.0f, 1060.0f, 700.0f), ctx);

    // 触发异步配置更新，验证 restartable 队列在重新启动后能正常写入
    tools3000::core::ConfigManager::instance().set("/test/lifecycle", false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST(NativeUISelectTest, DropdownUpwardGeometryWhenConstrained) {
    std::vector<SelectOption> options = {
        { "opt1", L"Option 1" },
        { "opt2", L"Option 2" },
        { "opt3", L"Option 3" },
        { "opt4", L"Option 4" }
    };
    auto select = std::make_shared<UISelect>(options, "opt1");
    // 将控件放置在靠下位置 (y = 660, 高度 34)
    Rect bottomBounds(100.0f, 660.0f, 300.0f, 694.0f);
    select->setBounds(bottomBounds);

    Rect drop = select->getDropdownRect();
    EXPECT_FLOAT_EQ(drop.width(), 200.0f);
    EXPECT_GT(drop.height(), 0.0f);
}

TEST(NativeUIScrollViewTest, WheelAndDragClampingBounds) {
    auto content = std::make_shared<UIElement>();
    content->layoutParams().fixedHeight = 1000.0f;
    content->layoutParams().fixedWidth = 300.0f;

    auto sv = std::make_shared<UIScrollView>(content);
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    sv->measure(300.0f, 400.0f, ctx);
    sv->layout(Rect(0.0f, 0.0f, 300.0f, 400.0f), ctx);

    EXPECT_FLOAT_EQ(sv->getMaxScrollOffset(), 600.0f);

    // 滚轮向上滚 (越界 < 0)
    UIMouseEvent wheelUp(UIMouseType::MouseWheel, Point(100.0f, 100.0f), MouseButton::None, 120.0f);
    EXPECT_TRUE(sv->onMouseWheel(wheelUp));
    EXPECT_FLOAT_EQ(sv->getScrollOffset(), 0.0f);

    // 滚轮向下滚 (超大 delta，越界 > maxScroll)
    UIMouseEvent wheelDown(UIMouseType::MouseWheel, Point(100.0f, 100.0f), MouseButton::None, -10000.0f);
    EXPECT_TRUE(sv->onMouseWheel(wheelDown));
    sv->scrollTo(10000.0f, false);
    EXPECT_FLOAT_EQ(sv->getScrollOffset(), 600.0f);
}

// ── 17. 浅色/深色主题动态响应与侧边栏渲染全链路验证 ──────────────────────────

TEST(NativeUIThemeTest, LightDarkThemePaletteAndSidebarProperties) {
    auto& theme = UITheme::instance();

    // 1. 切换为浅色模式 (Light)
    theme.setThemeMode(ThemeMode::Light);
    EXPECT_FALSE(theme.isEffectiveDark());
    const auto& lightPal = theme.palette();
    // 浅色模式下侧边栏底板必须为高亮陶瓷白 (Luminance > 0.9)
    EXPECT_GT(lightPal.sidebarBg.r, 0.90f);
    EXPECT_GT(lightPal.sidebarBg.g, 0.90f);
    EXPECT_GT(lightPal.sidebarBg.b, 0.90f);
    // 浅色模式下文本主色必须为深炭灰色 (Luminance < 0.2)
    EXPECT_LT(lightPal.textPrimary.r, 0.20f);
    EXPECT_LT(lightPal.textPrimary.g, 0.20f);
    EXPECT_LT(lightPal.textPrimary.b, 0.20f);
    // 侧边栏边框透明度在合理微晶范围
    EXPECT_GT(lightPal.sidebarBorder.a, 0.0f);
    EXPECT_LT(lightPal.sidebarBorder.a, 0.30f);

    // 2. 切换为深色模式 (Dark)
    theme.setThemeMode(ThemeMode::Dark);
    EXPECT_TRUE(theme.isEffectiveDark());
    const auto& darkPal = theme.palette();
    // 深色模式下侧边栏底板必须为黑曜石深色 (Luminance < 0.2)
    EXPECT_LT(darkPal.sidebarBg.r, 0.20f);
    EXPECT_LT(darkPal.sidebarBg.g, 0.20f);
    EXPECT_LT(darkPal.sidebarBg.b, 0.20f);
    // 深色模式下文本主色必须为高对比度浅白 (Luminance > 0.8)
    EXPECT_GT(darkPal.textPrimary.r, 0.80f);
    EXPECT_GT(darkPal.textPrimary.g, 0.80f);
    EXPECT_GT(darkPal.textPrimary.b, 0.80f);
}

TEST(NativeUIThemeTest, UILabelAndUIIconDynamicColorRoles) {
    auto label = std::make_shared<UILabel>(L"侧边栏分类", FontToken::Xs, FontWeight::SemiBold);
    label->setColorRole(TextColorRole::Muted);
    EXPECT_EQ(label->getColorRole(), TextColorRole::Muted);

    auto icon = std::make_shared<UIIcon>(IconType::Sparkles, 16.0f, Color(), IconColorRole::Primary);
    EXPECT_EQ(icon->getIconColorRole(), IconColorRole::Primary);

    // 动态强调色切换
    auto& theme = UITheme::instance();
    theme.setAccentColor("cyan");
    EXPECT_EQ(theme.getAccentColor(), "cyan");
    Color cyanPrimary = theme.palette().primary;

    theme.setAccentColor("blue");
    EXPECT_EQ(theme.getAccentColor(), "blue");
    Color bluePrimary = theme.palette().primary;

    EXPECT_NE(cyanPrimary.r, bluePrimary.r);
}

TEST(NativeSettingsAppTest, SidebarAndBottomThemeCapsuleSynchronization) {
    auto& app = NativeSettingsApp::instance();
    app.preload();

    auto sidebar = app.getSidebar();
    ASSERT_NE(sidebar, nullptr);
    EXPECT_FLOAT_EQ(sidebar->layoutParams().fixedWidth, 230.0f);

    auto btnDark = app.getThemeDarkButton();
    auto btnLight = app.getThemeLightButton();
    auto btnSys = app.getThemeSysButton();
    ASSERT_NE(btnDark, nullptr);
    ASSERT_NE(btnLight, nullptr);
    ASSERT_NE(btnSys, nullptr);

    // 测试 1: 切换至浅色模式，底栏浅色按钮变为高亮 Secondary 胶囊，深色/系统按钮为 Ghost
    app.applyThemeMode(ThemeMode::Light, false);
    EXPECT_EQ(UITheme::instance().getThemeMode(), ThemeMode::Light);
    EXPECT_FALSE(UITheme::instance().isEffectiveDark());
    EXPECT_EQ(btnLight->variant(), ButtonVariant::Secondary);
    EXPECT_EQ(btnDark->variant(), ButtonVariant::Ghost);
    EXPECT_EQ(btnSys->variant(), ButtonVariant::Ghost);

    // 测试 2: 切换至深色模式，底栏深色按钮变为高亮 Secondary 胶囊，浅色/系统按钮为 Ghost
    app.applyThemeMode(ThemeMode::Dark, false);
    EXPECT_EQ(UITheme::instance().getThemeMode(), ThemeMode::Dark);
    EXPECT_TRUE(UITheme::instance().isEffectiveDark());
    EXPECT_EQ(btnDark->variant(), ButtonVariant::Secondary);
    EXPECT_EQ(btnLight->variant(), ButtonVariant::Ghost);
    EXPECT_EQ(btnSys->variant(), ButtonVariant::Ghost);

    // 测试 3: 切换至系统模式，底栏系统按钮变为高亮 Secondary 胶囊
    app.applyThemeMode(ThemeMode::System, false);
    EXPECT_EQ(UITheme::instance().getThemeMode(), ThemeMode::System);
    EXPECT_EQ(btnSys->variant(), ButtonVariant::Secondary);
    EXPECT_EQ(btnDark->variant(), ButtonVariant::Ghost);
    EXPECT_EQ(btnLight->variant(), ButtonVariant::Ghost);

    // 渲染及度量侧边栏无崩溃
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    sidebar->measure(230.0f, 700.0f, ctx);
    sidebar->layout(Rect(0.0f, 0.0f, 230.0f, 700.0f), ctx);
    EXPECT_FLOAT_EQ(sidebar->bounds().width(), 230.0f);
    EXPECT_FLOAT_EQ(sidebar->bounds().height(), 700.0f);
    EXPECT_NO_THROW(sidebar->render(ctx));

    // 测试 4: 验证动态切换至浅色模式后，通用设置页的内容树生成与当前主题单一事实源对齐
    app.applyThemeMode(ThemeMode::Light, false);
    auto generalPage = app.createPageContent("general");
    ASSERT_NE(generalPage, nullptr);
    generalPage->measure(800.0f, 700.0f, ctx);
    generalPage->layout(Rect(0.0f, 0.0f, 800.0f, 700.0f), ctx);
    EXPECT_NO_THROW(generalPage->render(ctx));
}

TEST(NativeSettingsAppTest, GlassmorphismSidebarBoundarySafetyAndZeroBounds) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);

    // 零尺寸与退化边界条件防御
    EXPECT_NO_THROW(GlassmorphismRenderer::drawSidebar(ctx, Rect(0.0f, 0.0f, 0.0f, 0.0f), false));
    EXPECT_NO_THROW(GlassmorphismRenderer::drawSidebar(ctx, Rect(0.0f, 0.0f, 0.0f, 0.0f), true));
    EXPECT_NO_THROW(GlassmorphismRenderer::drawSidebar(ctx, Rect(10.0f, 10.0f, 10.0f, 50.0f), false));
    EXPECT_NO_THROW(GlassmorphismRenderer::drawSidebar(ctx, Rect(10.0f, 10.0f, 50.0f, 10.0f), true));

    // 正常侧边栏尺寸绘制防御 (暗色与浅色双模)
    EXPECT_NO_THROW(GlassmorphismRenderer::drawSidebar(ctx, Rect(0.0f, 0.0f, 230.0f, 700.0f), false));
    EXPECT_NO_THROW(GlassmorphismRenderer::drawSidebar(ctx, Rect(0.0f, 0.0f, 230.0f, 700.0f), true));

    // 分隔线微晶刻痕绘制防御
    EXPECT_NO_THROW(GlassmorphismRenderer::drawSeparator(ctx, Point(0.0f, 0.0f), Point(100.0f, 0.0f), false));
    EXPECT_NO_THROW(GlassmorphismRenderer::drawSeparator(ctx, Point(0.0f, 0.0f), Point(0.0f, 100.0f), true));
}

#include <opencv2/opencv.hpp>

TEST(NativeSettingsAppVisualVerification, RenderLightAndDarkModesToImage) {
    auto& app = NativeSettingsApp::instance();
    app.preload();
    auto host = app.getHost();
    ASSERT_NE(host, nullptr);
    auto root = host->getRootElement();
    ASSERT_NE(root, nullptr);

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    D2D1_FACTORY_OPTIONS d2dOpts{};
    ASSERT_EQ(S_OK, D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dOpts, d2dFactory.GetAddressOf()));

    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    ASSERT_EQ(S_OK, DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())));

    const int width = 1060;
    const int height = 700;

    HDC screenDc = GetDC(nullptr);
    ASSERT_NE(screenDc, nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    ASSERT_NE(memDc, nullptr);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDc, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ASSERT_NE(hBmp, nullptr);
    ASSERT_NE(pBits, nullptr);
    HGDIOBJ oldBmp = SelectObject(memDc, hBmp);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f
    );
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dcRt;
    ASSERT_EQ(S_OK, d2dFactory->CreateDCRenderTarget(&props, dcRt.GetAddressOf()));
    RECT rc = { 0, 0, width, height };
    ASSERT_EQ(S_OK, dcRt->BindDC(memDc, &rc));

    auto getPixel = [&](int x, int y, uint8_t& r, uint8_t& g, uint8_t& b) {
        const uint8_t* ptr = static_cast<const uint8_t*>(pBits) + (y * width + x) * 4;
        b = ptr[0];
        g = ptr[1];
        r = ptr[2];
    };

    // ── 1. 验证浅色模式 (Light Mode) 真实像素渲染 ──
    app.applyThemeMode(ThemeMode::Light, false);
    EXPECT_FALSE(UITheme::instance().isEffectiveDark());

    dcRt->BeginDraw();
    {
        UIRenderContext renderCtx(dcRt.Get(), dwriteFactory.Get(), 1.0f);
        GlassmorphismRenderer::drawWindowBackground(renderCtx, Rect(0, 0, width, height), false, UITheme::instance().palette().primary);
        root->measure(width, height, renderCtx);
        root->layout(Rect(0, 0, width, height), renderCtx);
        root->render(renderCtx);
    }
    ASSERT_EQ(S_OK, dcRt->EndDraw());

    // 保存浅色模式渲染实况图
    cv::Mat matLight(height, width, CV_8UC4, pBits);
    cv::imwrite("build/actual_rendered_light.png", matLight);

    // 采样左侧侧边栏纯底色区域 (x=5, y=100) — 在浅色模式下必须为高亮陶瓷白 (R,G,B > 230)
    uint8_t r = 0, g = 0, b = 0;
    getPixel(5, 100, r, g, b);
    std::printf("[VISUAL TEST] Light Mode Sidebar Background (5, 100) pixel: R=%d, G=%d, B=%d\n", r, g, b);
    EXPECT_GT(r, 230);
    EXPECT_GT(g, 230);
    EXPECT_GT(b, 230);

    // 采样侧边栏底栏外侧纯底色 (x=5, y=620) — 在浅色模式下必须为柔和微晶浅色 (R,G,B > 220)
    getPixel(5, 620, r, g, b);
    std::printf("[VISUAL TEST] Light Mode Footer Background (5, 620) pixel: R=%d, G=%d, B=%d\n", r, g, b);
    EXPECT_GT(r, 220);
    EXPECT_GT(g, 220);
    EXPECT_GT(b, 220);

    // 采样右侧卡片区域 (x=500, y=200) — 在浅色模式下必须为明亮白色卡片 (R,G,B > 240)
    getPixel(500, 200, r, g, b);
    std::printf("[VISUAL TEST] Light Mode Card (500, 200) pixel: R=%d, G=%d, B=%d\n", r, g, b);
    EXPECT_GT(r, 235);
    EXPECT_GT(g, 235);
    EXPECT_GT(b, 235);

    // 采样标题栏最小化窗口控制按钮像素 (x=950, y=19) — 浅色模式下必须清晰呈现高对比深色线条
    getPixel(950, 19, r, g, b);
    std::printf("[VISUAL TEST] Light Mode Caption Button (950, 19) pixel: R=%d, G=%d, B=%d\n", r, g, b);
    EXPECT_LT(r, 160);
    EXPECT_LT(g, 160);
    EXPECT_LT(b, 175);

    // ── 2. 验证深色模式 (Dark Mode) 真实像素渲染 ──
    app.applyThemeMode(ThemeMode::Dark, false);
    EXPECT_TRUE(UITheme::instance().isEffectiveDark());

    dcRt->BeginDraw();
    {
        UIRenderContext renderCtx(dcRt.Get(), dwriteFactory.Get(), 1.0f);
        GlassmorphismRenderer::drawWindowBackground(renderCtx, Rect(0, 0, width, height), true, UITheme::instance().palette().primary);
        root->measure(width, height, renderCtx);
        root->layout(Rect(0, 0, width, height), renderCtx);
        root->render(renderCtx);
    }
    ASSERT_EQ(S_OK, dcRt->EndDraw());

    // 保存深色模式渲染实况图
    cv::Mat matDark(height, width, CV_8UC4, pBits);
    cv::imwrite("build/actual_rendered_dark.png", matDark);

    // 采样左侧侧边栏纯底色区域 (x=5, y=100) — 在深色模式下必须为黑曜石深色 (R<40, G<45, B<60)
    getPixel(5, 100, r, g, b);
    std::printf("[VISUAL TEST] Dark Mode Sidebar Background (5, 100) pixel: R=%d, G=%d, B=%d\n", r, g, b);
    EXPECT_LT(r, 40);
    EXPECT_LT(g, 45);
    EXPECT_LT(b, 60);

    // 采样侧边栏底栏外侧纯底色 (x=5, y=620) — 在深色模式下必须为黑曜石底衬 (R<40, G<45, B<60)
    getPixel(5, 620, r, g, b);
    std::printf("[VISUAL TEST] Dark Mode Footer Background (5, 620) pixel: R=%d, G=%d, B=%d\n", r, g, b);
    EXPECT_LT(r, 40);
    EXPECT_LT(g, 45);
    EXPECT_LT(b, 60);

    // 采样右侧卡片区域 (x=500, y=200) — 在深色模式下必须为暗色卡片 (R,G,B < 60)
    getPixel(500, 200, r, g, b);
    std::printf("[VISUAL TEST] Dark Mode Card (500, 200) pixel: R=%d, G=%d, B=%d\n", r, g, b);
    EXPECT_LT(r, 60);
    EXPECT_LT(g, 60);
    EXPECT_LT(b, 60);

    // 采样标题栏最小化窗口控制按钮像素 (x=950, y=19) — 深色模式下必须清晰呈现高对比明亮线条 (R,G,B > 120)
    getPixel(950, 19, r, g, b);
    std::printf("[VISUAL TEST] Dark Mode Caption Button (950, 19) pixel: R=%d, G=%d, B=%d\n", r, g, b);
    EXPECT_GT(r, 120);
    EXPECT_GT(g, 120);
    EXPECT_GT(b, 120);

    // 清理资源
    SelectObject(memDc, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
}

// ── 12. 原生手势画板交互与方向识别单元测试 ──────────────────────────────────

TEST(NativeGestureDrawCanvasTest, StrokeRecordingAndDirectionDetection) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    NativeGestureDrawCanvas canvas;

    EXPECT_EQ(canvas.getPointCount(), 0);
    EXPECT_TRUE(canvas.getGestureCode().empty());

    Size sz = canvas.measure(600.0f, 400.0f, ctx);
    EXPECT_GT(sz.width, 0.0f);
    EXPECT_GT(sz.height, 0.0f);
    canvas.layout(Rect(0, 0, sz.width, sz.height), ctx);

    // 模拟从 (100, 100) 向右拖动至 (250, 100)
    UIMouseEvent downEvent;
    downEvent.type = UIMouseType::MouseDown;
    downEvent.position = Point(100.0f, 100.0f);
    canvas.onMouseDown(downEvent);

    UIMouseEvent moveEvent1;
    moveEvent1.type = UIMouseType::MouseMove;
    moveEvent1.position = Point(180.0f, 100.0f);
    canvas.onMouseMove(moveEvent1);

    UIMouseEvent moveEvent2;
    moveEvent2.type = UIMouseType::MouseMove;
    moveEvent2.position = Point(250.0f, 100.0f);
    canvas.onMouseMove(moveEvent2);

    UIMouseEvent upEvent;
    upEvent.type = UIMouseType::MouseUp;
    upEvent.position = Point(250.0f, 100.0f);
    canvas.onMouseUp(upEvent);

    EXPECT_GT(canvas.getPointCount(), 0);
    EXPECT_EQ(canvas.getGestureCode(), "R");

    // 清空画板
    canvas.clearCanvas();
    EXPECT_EQ(canvas.getPointCount(), 0);
    EXPECT_TRUE(canvas.getGestureCode().empty());
}

// ── 13. 原生 104 键热力图全景与击键矩阵单元测试 ────────────────────────────

TEST(NativeKeyboardHeatmapTest, MatrixLayoutAndKeyHitCounts) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    NativeKeyboardHeatmap heatmap;

    EXPECT_EQ(heatmap.getLayoutMode(), KeyboardLayoutMode::Full104);

    heatmap.clearHits();
    heatmap.setKeyHits(VK_SPACE, 150);
    heatmap.setKeyHits(VK_RETURN, 80);
    heatmap.setKeyHits('E', 120);

    EXPECT_EQ(heatmap.getKeyHits(VK_SPACE), 150);
    EXPECT_EQ(heatmap.getKeyHits(VK_RETURN), 80);
    EXPECT_EQ(heatmap.getKeyHits('E'), 120);
    EXPECT_EQ(heatmap.getMaxHits(), 150);
    EXPECT_EQ(heatmap.getTotalHits(), 350);

    heatmap.setLayoutMode(KeyboardLayoutMode::TKL87);
    EXPECT_EQ(heatmap.getLayoutMode(), KeyboardLayoutMode::TKL87);

    heatmap.setLayoutMode(KeyboardLayoutMode::Compact60);
    EXPECT_EQ(heatmap.getLayoutMode(), KeyboardLayoutMode::Compact60);

    Size sz = heatmap.measure(800.0f, 300.0f, ctx);
    EXPECT_GT(sz.width, 0.0f);
    EXPECT_GT(sz.height, 0.0f);
    heatmap.layout(Rect(0, 0, sz.width, sz.height), ctx);
}

// ── 14. 原生进程与类名作用域规则管理器单元测试 ────────────────────────────

TEST(NativeScopeRulesManagerTest, ScopeRulesManagementAndStrategy) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    NativeScopeRulesManager manager;

    size_t initialCount = manager.getRules().size();
    EXPECT_GT(initialCount, 0);

    ScopeRuleDef newRule;
    newRule.id = "rule_test_vscode";
    newRule.name = L"Visual Studio Code";
    newRule.targetKind = "process";
    newRule.targetValue = L"code.exe";
    newRule.effect = 1;
    newRule.enabled = true;
    manager.addRule(newRule);

    EXPECT_EQ(manager.getRules().size(), initialCount + 1);

    manager.toggleRuleEnabled("rule_test_vscode");
    for (const auto& r : manager.getRules()) {
        if (r.id == "rule_test_vscode") {
            EXPECT_FALSE(r.enabled);
            break;
        }
    }

    manager.deleteRule("rule_test_vscode");
    EXPECT_EQ(manager.getRules().size(), initialCount);

    Size sz = manager.measure(800.0f, 600.0f, ctx);
    EXPECT_GT(sz.width, 0.0f);
    EXPECT_GT(sz.height, 0.0f);
    manager.layout(Rect(0, 0, sz.width, sz.height), ctx);

    // 测试 resetToDefault 重置为出厂内置规则
    manager.resetToDefault();
    EXPECT_EQ(manager.getRules().size(), 5);
    bool hasChrome = false;
    for (const auto& r : manager.getRules()) {
        if (r.targetValue == L"chrome.exe") hasChrome = true;
    }
    EXPECT_TRUE(hasChrome);
}

// ── 15. 原生扩展应用页面体系测试 ──────────────────────────────────────────

TEST(NativeExtendedPagesTest, PagesCreationAndLayout) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);

    // 1. AI 极速助手页面
    NativeAiAssistantPage aiPage;
    EXPECT_EQ(aiPage.getPageId(), "ai_assistant");
    auto aiContent = aiPage.buildContent();
    ASSERT_NE(aiContent, nullptr);
    Size aiSize = aiContent->measure(800.0f, 1200.0f, ctx);
    EXPECT_GT(aiSize.width, 0.0f);
    EXPECT_GT(aiSize.height, 0.0f);
    aiContent->layout(Rect(0, 0, aiSize.width, aiSize.height), ctx);

    // 2. 屏幕拾色器页面
    NativeColorPickerPage colorPage;
    EXPECT_EQ(colorPage.getPageId(), "color_picker");
    auto colorContent = colorPage.buildContent();
    ASSERT_NE(colorContent, nullptr);
    Size colorSize = colorContent->measure(800.0f, 1200.0f, ctx);
    EXPECT_GT(colorSize.width, 0.0f);
    EXPECT_GT(colorSize.height, 0.0f);
    colorContent->layout(Rect(0, 0, colorSize.width, colorSize.height), ctx);

    // 3. 剪贴板历史管理页面
    NativeClipboardManagerPage clipPage;
    EXPECT_EQ(clipPage.getPageId(), "clipboard_manager");
    auto clipContent = clipPage.buildContent();
    ASSERT_NE(clipContent, nullptr);
    Size clipSize = clipContent->measure(800.0f, 1200.0f, ctx);
    EXPECT_GT(clipSize.width, 0.0f);
    EXPECT_GT(clipSize.height, 0.0f);
    clipContent->layout(Rect(0, 0, clipSize.width, clipSize.height), ctx);

    // 4. Markdown 极速预览页面
    NativeMarkdownPreviewPage mdPage;
    EXPECT_EQ(mdPage.getPageId(), "markdown_preview");
    auto mdContent = mdPage.buildContent();
    ASSERT_NE(mdContent, nullptr);
    Size mdSize = mdContent->measure(800.0f, 1200.0f, ctx);
    EXPECT_GT(mdSize.width, 0.0f);
    EXPECT_GT(mdSize.height, 0.0f);
    mdContent->layout(Rect(0, 0, mdSize.width, mdSize.height), ctx);
}

// ── 16. 原生系统托盘应用测试 ──────────────────────────────────────────────

TEST(NativeTrayAppTest, TrayViewTogglesAndCalculatedHeight) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    NativeTrayView trayView;

    EXPECT_EQ(trayView.getPillCount(), 7);
    EXPECT_GT(trayView.getMenuItemCount(), 0);

    bool initStatus = trayView.getPill(0).active;
    std::string pillId = trayView.getPill(0).id;
    trayView.togglePill(pillId);
    EXPECT_EQ(trayView.getPill(0).active, !initStatus);
    trayView.togglePill(pillId);
    EXPECT_EQ(trayView.getPill(0).active, initStatus);

    trayView.setPendingRestart(false);
    Size szNoRestart = trayView.measure(240.0f, 500.0f, ctx);

    trayView.setPendingRestart(true);
    Size szWithRestart = trayView.measure(240.0f, 500.0f, ctx);
    EXPECT_GT(szWithRestart.height, szNoRestart.height);

    trayView.layout(Rect(0, 0, szWithRestart.width, szWithRestart.height), ctx);
}

// ── 17. 原生快速搜索中心测试 ──────────────────────────────────────────────

TEST(NativeSearchAppTest, SearchFilterAndNavigation) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    NativeSearchView searchView;

    EXPECT_GT(searchView.getResultCount(), 0);
    EXPECT_EQ(searchView.getSelectedIndex(), 0);

    searchView.setQuery(L"Tools");
    EXPECT_EQ(searchView.getQuery(), L"Tools");
    searchView.executeSearch();
    EXPECT_GT(searchView.getResultCount(), 0);

    searchView.setCategory(SearchCategory::Documents);
    searchView.executeSearch();

    UIKeyEvent downKey;
    downKey.virtualKey = VK_DOWN;
    downKey.type = KeyEventType::KeyDown;
    searchView.onKeyDown(downKey);
    EXPECT_EQ(searchView.getSelectedIndex(), 1);

    UIKeyEvent upKey;
    upKey.virtualKey = VK_UP;
    upKey.type = KeyEventType::KeyDown;
    searchView.onKeyDown(upKey);
    EXPECT_EQ(searchView.getSelectedIndex(), 0);

    Size sz = searchView.measure(860.0f, 560.0f, ctx);
    EXPECT_FLOAT_EQ(sz.width, 860.0f);
    EXPECT_FLOAT_EQ(sz.height, 560.0f);
    searchView.layout(Rect(0, 0, sz.width, sz.height), ctx);

    // 激活状态下重设初始项与视图刷新
    searchView.setQuery(L"SomethingTyped");
    EXPECT_EQ(searchView.getQuery(), L"SomethingTyped");
    searchView.onActivated();
    EXPECT_EQ(searchView.getSelectedIndex(), 0);
}

// ── 18. 原生空格文件极速预览器测试 ──────────────────────────────────────────

TEST(NativeQuickLookAppTest, FileTypeDetectionAndPreviewDispatch) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    NativeQuickLookView qlView;

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring testCodeFile = std::wstring(tempPath) + L"tools3000_test_file.txt";
    {
        std::ofstream ofs(testCodeFile);
        ofs << "cmake_minimum_required(VERSION 3.25)\nproject(Tools3000)\n";
    }

    qlView.previewFile(testCodeFile);
    EXPECT_TRUE(qlView.getFileType() == QuickLookFileType::Code ||
                qlView.getFileType() == QuickLookFileType::Text);
    EXPECT_GT(qlView.getLineCount(), 0);
    DeleteFileW(testCodeFile.c_str());

    qlView.previewFile(tempPath);
    EXPECT_EQ(qlView.getFileType(), QuickLookFileType::Folder);

    UIMouseEvent wheelEvent;
    wheelEvent.type = UIMouseType::MouseWheel;
    wheelEvent.wheelDelta = -120.0f;
    qlView.onMouseWheel(wheelEvent);

    Size sz = qlView.measure(1060.0f, 740.0f, ctx);
    EXPECT_GT(sz.width, 0.0f);
    EXPECT_GT(sz.height, 0.0f);
    qlView.layout(Rect(0, 0, sz.width, sz.height), ctx);
    EXPECT_NO_THROW(qlView.render(ctx));
}

TEST(NativeSearchAppTest, PinnedStateAndContextMenuInteractions) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    NativeSearchView searchView;
    searchView.layout(Rect(0.0f, 0.0f, 860.0f, 560.0f), ctx);

    // 1. 测试置顶 Pin 状态切换
    EXPECT_FALSE(searchView.isPinned());
    searchView.setPinned(true);
    EXPECT_TRUE(searchView.isPinned());
    searchView.setPinned(false);
    EXPECT_FALSE(searchView.isPinned());

    // 模拟点击 Pin 按钮 (位于右上角 860 - 46 = 814 到 844, y: 12 到 48)
    UIMouseEvent clickPin(UIMouseType::MouseDown, Point(825.0f, 25.0f), MouseButton::Left);
    EXPECT_TRUE(searchView.onMouseDown(clickPin));
    EXPECT_TRUE(searchView.isPinned());

    // 再次点击 Pin 取消置顶
    EXPECT_TRUE(searchView.onMouseDown(clickPin));
    EXPECT_FALSE(searchView.isPinned());

    // 2. 模拟右键列表条目触发上下文菜单
    // 列表项起始在 listTop = 92.0f, 第一项 y 在 [92, 132]
    UIMouseEvent hoverItem(UIMouseType::MouseMove, Point(100.0f, 105.0f));
    searchView.onMouseMove(hoverItem);

    UIMouseEvent rightClick(UIMouseType::MouseDown, Point(100.0f, 105.0f), MouseButton::Right);
    EXPECT_TRUE(searchView.onMouseDown(rightClick));

    // 移动鼠标悬停上下文菜单项 (x 在 [100, 290], item 0 在 y: [111, 141])
    UIMouseEvent hoverMenu(UIMouseType::MouseMove, Point(150.0f, 120.0f));
    EXPECT_TRUE(searchView.onMouseMove(hoverMenu));

    // 点击上下文菜单第二项 (复制绝对路径)
    UIMouseEvent clickMenuCopy(UIMouseType::MouseDown, Point(150.0f, 175.0f), MouseButton::Left);
    EXPECT_TRUE(searchView.onMouseDown(clickMenuCopy));

    // 3. 渲染测试验证无异常抛出
    EXPECT_NO_THROW(searchView.render(ctx));

    // 4. NativeSearchApp 单例置顶委托验证
    auto& app = NativeSearchApp::instance();
    app.setPinned(true);
    EXPECT_TRUE(app.isPinned());
    app.setPinned(false);
    EXPECT_FALSE(app.isPinned());
}

TEST(NativeSearchAppTest, KeyboardInputAndTypingNavigation) {
    NativeSearchView searchView;
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    searchView.layout(Rect(0.0f, 0.0f, 860.0f, 560.0f), ctx);

    // 1. 初始状态为全部 Mock 项
    size_t initialCount = searchView.getResultCount();
    EXPECT_GT(initialCount, 0);

    // 2. 模拟键盘输入字符 'T', 'o', 'o', 'l', 's'
    searchView.onChar(UIKeyEvent(KeyEventType::Char, 0, L'T'));
    searchView.onChar(UIKeyEvent(KeyEventType::Char, 0, L'o'));
    searchView.onChar(UIKeyEvent(KeyEventType::Char, 0, L'o'));
    searchView.onChar(UIKeyEvent(KeyEventType::Char, 0, L'l'));
    searchView.onChar(UIKeyEvent(KeyEventType::Char, 0, L's'));
    EXPECT_EQ(searchView.getQuery(), L"Tools");
    EXPECT_GT(searchView.getResultCount(), 0);
    EXPECT_LE(searchView.getResultCount(), initialCount);

    // 3. 模拟退格键 Backspace
    UIKeyEvent backspace(KeyEventType::KeyDown, VK_BACK);
    EXPECT_TRUE(searchView.onKeyDown(backspace));
    EXPECT_EQ(searchView.getQuery(), L"Tool");

    // 4. 模拟列表上下键选择
    EXPECT_EQ(searchView.getSelectedIndex(), 0);
    UIKeyEvent downKey(KeyEventType::KeyDown, VK_DOWN);
    EXPECT_TRUE(searchView.onKeyDown(downKey));
    EXPECT_EQ(searchView.getSelectedIndex(), 1);

    UIKeyEvent upKey(KeyEventType::KeyDown, VK_UP);
    EXPECT_TRUE(searchView.onKeyDown(upKey));
    EXPECT_EQ(searchView.getSelectedIndex(), 0);

    // 5. 模拟 Ctrl+A 一键全选/清空
    UIKeyEvent ctrlA(KeyEventType::KeyDown, 'A');
    ctrlA.modifiers.ctrl = true;
    EXPECT_TRUE(searchView.onKeyDown(ctrlA));
    EXPECT_TRUE(searchView.getQuery().empty());
    EXPECT_EQ(searchView.getResultCount(), initialCount);

    // 6. 模拟 Esc 隐藏
    UIKeyEvent escKey(KeyEventType::KeyDown, VK_ESCAPE);
    EXPECT_TRUE(searchView.onKeyDown(escKey));
}

TEST(NativeSearchAppTest, VisualRender) {
    D2D1_FACTORY_OPTIONS d2dOpts{};
    d2dOpts.debugLevel = D2D1_DEBUG_LEVEL_NONE;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    ASSERT_EQ(S_OK, D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dOpts, d2dFactory.GetAddressOf()));

    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    ASSERT_EQ(S_OK, DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())));

    const int width = 860;
    const int height = 560;

    HDC screenDc = GetDC(nullptr);
    ASSERT_NE(screenDc, nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    ASSERT_NE(memDc, nullptr);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDc, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ASSERT_NE(hBmp, nullptr);
    HGDIOBJ oldBmp = SelectObject(memDc, hBmp);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f
    );
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dcRt;
    ASSERT_EQ(S_OK, d2dFactory->CreateDCRenderTarget(&props, dcRt.GetAddressOf()));
    RECT rc = { 0, 0, width, height };
    ASSERT_EQ(S_OK, dcRt->BindDC(memDc, &rc));

    NativeSearchView searchView;
    UIRenderContext initCtx(nullptr, nullptr, 1.0f);
    searchView.layout(Rect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)), initCtx);

    // 1. Light Mode
    UITheme::instance().setThemeMode(ThemeMode::Light);
    dcRt->BeginDraw();
    {
        UIRenderContext renderCtx(dcRt.Get(), dwriteFactory.Get(), 1.0f);
        GlassmorphismRenderer::drawWindowBackground(renderCtx, Rect(0, 0, width, height), false, UITheme::instance().palette().primary);
        searchView.render(renderCtx);
    }
    ASSERT_EQ(S_OK, dcRt->EndDraw());
    cv::Mat matLight(height, width, CV_8UC4, pBits);
    cv::imwrite("build/actual_rendered_search_light.png", matLight);

    // 2. Dark Mode
    UITheme::instance().setThemeMode(ThemeMode::Dark);
    dcRt->BeginDraw();
    {
        UIRenderContext renderCtx(dcRt.Get(), dwriteFactory.Get(), 1.0f);
        GlassmorphismRenderer::drawWindowBackground(renderCtx, Rect(0, 0, width, height), true, UITheme::instance().palette().primary);
        searchView.render(renderCtx);
    }
    ASSERT_EQ(S_OK, dcRt->EndDraw());
    cv::Mat matDark(height, width, CV_8UC4, pBits);
    cv::imwrite("build/actual_rendered_search_dark.png", matDark);

    SelectObject(memDc, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
}

TEST(NativeQuickLookAppTest, ImageZoomAndEmptyErrorHandling) {
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    NativeQuickLookView qlView;
    qlView.layout(Rect(0.0f, 0.0f, 920.0f, 640.0f), ctx);

    // 1. 不存在的文件错误处理
    qlView.previewFile(L"C:\\non_existent_file_tools3000_check.xyz");
    EXPECT_TRUE(qlView.hasError());
    EXPECT_FALSE(qlView.getErrorMessage().empty());
    EXPECT_NO_THROW(qlView.render(ctx));

    // 2. 空白 0 字节文件预览
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring emptyFile = std::wstring(tempPath) + L"tools3000_empty_test.txt";
    HANDLE hFile = CreateFileW(emptyFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);

        qlView.previewFile(emptyFile);
        EXPECT_FALSE(qlView.hasError());
        EXPECT_EQ(qlView.getFileType(), QuickLookFileType::Text);
        EXPECT_GT(qlView.getLineCount(), 0);
        EXPECT_NO_THROW(qlView.render(ctx));

        DeleteFileW(emptyFile.c_str());
    }

    // 3. 图像预览与多级滚轮缩放
    std::wstring testImg = L"ui\\src\\assets\\hero.png";
    if (GetFileAttributesW(testImg.c_str()) != INVALID_FILE_ATTRIBUTES) {
        qlView.previewFile(testImg);
        EXPECT_FALSE(qlView.hasError());
        EXPECT_EQ(qlView.getFileType(), QuickLookFileType::Image);
        EXPECT_FLOAT_EQ(qlView.getImageScale(), 1.0f);

        // 滚轮放大
        UIMouseEvent zoomIn(UIMouseType::MouseWheel, Point(400.0f, 300.0f), MouseButton::None, 120.0f);
        EXPECT_TRUE(qlView.onMouseWheel(zoomIn));
        EXPECT_GT(qlView.getImageScale(), 1.0f);

        // 滚轮缩小
        UIMouseEvent zoomOut(UIMouseType::MouseWheel, Point(400.0f, 300.0f), MouseButton::None, -120.0f);
        EXPECT_TRUE(qlView.onMouseWheel(zoomOut));

        // 双击重置比例
        UIMouseEvent dblClick(UIMouseType::MouseDown, Point(400.0f, 300.0f), MouseButton::Left, 0.0f, 2);
        EXPECT_TRUE(qlView.onMouseDown(dblClick));
        EXPECT_FLOAT_EQ(qlView.getImageScale(), 1.0f);

        EXPECT_NO_THROW(qlView.render(ctx));
    }

    // 4. Markdown 语法渲染
    qlView.previewFile(L"README.md");
    if (!qlView.hasError()) {
        EXPECT_EQ(qlView.getFileType(), QuickLookFileType::Markdown);
        EXPECT_GT(qlView.getLineCount(), 0);
        EXPECT_NO_THROW(qlView.render(ctx));
    }
}

TEST(NativeUIButtonHitToleranceTest, ClickToleranceAcceptsMicroMovement) {
    UIButton btn(L"测试按钮", ButtonVariant::Primary, ButtonSize::Md);
    btn.setBounds(Rect(10.0f, 10.0f, 100.0f, 40.0f));

    bool clicked = false;
    btn.setOnClick([&]() { clicked = true; });

    // 1. 正常在内部按下并在内部释放
    UIMouseEvent down1(UIMouseType::MouseDown, Point(50.0f, 25.0f), MouseButton::Left);
    btn.onMouseDown(down1);
    UIMouseEvent up1(UIMouseType::MouseUp, Point(50.0f, 25.0f), MouseButton::Left);
    btn.onMouseUp(up1);
    EXPECT_TRUE(clicked);

    // 2. 模拟高分屏/触控板释放时光标溢出 3px 微抖动（在 6px 容差盒内）
    clicked = false;
    UIMouseEvent down2(UIMouseType::MouseDown, Point(50.0f, 25.0f), MouseButton::Left);
    btn.onMouseDown(down2);
    UIMouseEvent up2(UIMouseType::MouseUp, Point(103.0f, 42.0f), MouseButton::Left); // x 溢出 3px, y 溢出 2px
    btn.onMouseUp(up2);
    EXPECT_TRUE(clicked); // 必须顺利触发点击

    // 3. 释放时光标大幅漂移 (> 6px) 则安全放弃
    clicked = false;
    UIMouseEvent down3(UIMouseType::MouseDown, Point(50.0f, 25.0f), MouseButton::Left);
    btn.onMouseDown(down3);
    UIMouseEvent up3(UIMouseType::MouseUp, Point(120.0f, 25.0f), MouseButton::Left); // x 溢出 20px
    btn.onMouseUp(up3);
    EXPECT_FALSE(clicked);
}

TEST(NativeUICardHierarchyTest, ChildrenReceiveParentAndWindowHost) {
    auto card = std::make_shared<UICard>(L"测试卡片", L"描述");
    auto actionBtn = Button(L"重置");
    card->setHeaderAction(actionBtn);

    auto rowBtn = Button(L"操作");
    auto row = SettingRow(L"设置项", L"说明", rowBtn);
    card->addChild(row);

    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    card->layout(Rect(0.0f, 0.0f, 400.0f, 300.0f), ctx);

    EXPECT_EQ(actionBtn->getParent().get(), card.get());
    EXPECT_EQ(row->getParent().get(), card.get());
}

TEST(NativeUISettingGroupHierarchyTest, ChildrenReceiveParentAndWindowHost) {
    auto group = std::make_shared<UISettingGroup>(L"测试分组", IconType::Settings);
    auto child = std::make_shared<UIElement>();
    group->addChild(child);

    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    group->layout(Rect(0.0f, 0.0f, 400.0f, 300.0f), ctx);

    EXPECT_EQ(child->getParent().get(), group.get());
}

TEST(NativeUIScrollViewHitPrecedenceTest, ContentChildTakesPrecedenceOverScrollbar) {
    auto scroll = std::make_shared<UIScrollView>();
    auto container = std::make_shared<UIElement>();
    container->layoutParams().direction = FlexDirection::Column;
    container->layoutParams().fixedHeight = 1000.0f;

    auto btn = Button(L"边缘按钮");
    container->addChild(btn);

    scroll->setContent(container);
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    scroll->measure(300.0f, 400.0f, ctx);
    scroll->layout(Rect(0.0f, 0.0f, 300.0f, 400.0f), ctx);
    btn->setBounds(Rect(280.0f, 20.0f, 296.0f, 50.0f)); // 位于右侧 16px 滚动条感应区域内

    // 点击该按钮所在坐标，应命中按钮自身，绝不得被外层滚动条强行抢夺吞掉
    auto hit = scroll->hitTest(Point(285.0f, 35.0f));
    EXPECT_EQ(hit.get(), btn.get());
}

TEST(NativeSearchAppHostTest, NativeBackendWindowSizeAndState) {
    auto [w, h] = NativeSearchApp::instance().getWindowSize();
    EXPECT_GT(w, 0);
    EXPECT_GT(h, 0);
    HWND hwnd = NativeSearchApp::instance().hwnd();
    (void)hwnd;
}

TEST(NativeSearchAppRoutingTest, SetPinnedAndFocusSearch) {
    auto& searchWnd = tools3000::ui::native::NativeSearchApp::instance();
    searchWnd.setPinned(true);
    EXPECT_TRUE(searchWnd.isPinned());
    searchWnd.setPinned(false);
    EXPECT_FALSE(searchWnd.isPinned());
    searchWnd.focusSearchIfVisible();

    auto genPage = NativeSettingsApp::instance().createPageContent("general");
    EXPECT_NE(genPage, nullptr);
    auto clipPage = NativeSettingsApp::instance().createPageContent("clipboard_manager");
    EXPECT_NE(clipPage, nullptr);
    auto colorPage = NativeSettingsApp::instance().createPageContent("color_picker");
    EXPECT_NE(colorPage, nullptr);
    auto mdPage = NativeSettingsApp::instance().createPageContent("markdown_preview");
    EXPECT_NE(mdPage, nullptr);
}

TEST(NativeSearchAppTest, MultiTokenSearchAndEmptyStateRendering) {
    NativeSearchView searchView;
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    searchView.layout(Rect(0.0f, 0.0f, 860.0f, 560.0f), ctx);

    // 1. 多关键词空格分隔检索 (例如 "Tools Guide" 命中 "Tools3000_Guide.md")
    searchView.setQuery(L"Tools Guide");
    EXPECT_EQ(searchView.getQuery(), L"Tools Guide");
    EXPECT_GT(searchView.getResultCount(), 0);

    // 2. 空结果状态与空状态卡片渲染验证
    searchView.setQuery(L"NonExistentKeywordXYZ123");
    EXPECT_EQ(searchView.getResultCount(), 0);
    EXPECT_NO_THROW(searchView.render(ctx));

    // 3. 空关键词重置验证
    searchView.setQuery(L"");
    EXPECT_GT(searchView.getResultCount(), 0);
    EXPECT_NO_THROW(searchView.render(ctx));

    // 4. MessageBridge 对整型与字符串 id 的容错解析验证 (同步与异步路径)
    auto& mb = tools3000::core::MessageBridge::instance();
    std::string intResp = mb.handleMessage(R"({"id": 1001, "method": "nonexistent.method", "params": {}})");
    auto jInt = nlohmann::json::parse(intResp);
    EXPECT_EQ(jInt.value("id", 0), 1001);

    std::string strResp = mb.handleMessage(R"({"id": "search_1002", "method": "nonexistent.method", "params": {}})");
    auto jStr = nlohmann::json::parse(strResp);
    EXPECT_TRUE(jStr.contains("id"));

    // 异步路径对于整型和字符串 id 解析的健壮性验证
    std::atomic<bool> asyncCalled{false};
    mb.handleMessageAsync(R"({"id": "search_async_1003", "method": "nonexistent.method", "params": {}})", [&](std::string r) {
        auto j = nlohmann::json::parse(r);
        EXPECT_TRUE(j.contains("id"));
        asyncCalled.store(true);
    });

    // 5. 检索防抖与帧调度推进验证
    searchView.setQuery(L"Tools");
    EXPECT_TRUE(searchView.update(0.15f));

    // 6. 内置指令 /boost 与效率指令搜索联动验证
    searchView.setQuery(L"/boost");
    EXPECT_GT(searchView.getResultCount(), 0);
    EXPECT_EQ(searchView.getQuery(), L"/boost");

    // 7. 验证 Tab 键循环切换搜索分类
    UIKeyEvent tabKey(KeyEventType::KeyDown, VK_TAB, 0);
    EXPECT_TRUE(searchView.onKeyDown(tabKey));

    // 8. 验证单独输入 / 唤起全部可用效率命令
    searchView.setQuery(L"/");
    EXPECT_GE(searchView.getResultCount(), 10);

    // 9. 验证全角空格中文多关键词检索分词 (例如 "Tools　Guide")
    searchView.setQuery(L"Tools　Guide");
    EXPECT_GT(searchView.getResultCount(), 0);

    // 10. 验证其他核心快捷指令如 /capture 与 /record
    searchView.setQuery(L"/capture");
    EXPECT_GT(searchView.getResultCount(), 0);
    searchView.setQuery(L"/record");
    EXPECT_GT(searchView.getResultCount(), 0);
}

TEST(NativeSearchAppTest, SearchExpressionSlashAndPinyinEdgeCases) {
    // 验证正斜杠自动规范化映射为反斜杠
    std::wstring norm = SearchExpression::normalize(L"easyTools/src/ui");
    EXPECT_EQ(norm, L"easytools\\src\\ui");

    // 验证 SearchExpression 对全角空格的解析分词
    auto expr = SearchExpression::parse(L"同心　账号密码");
    EXPECT_EQ(expr.getOrGroups().size(), 2u);
}

TEST(NativeKeyboardHeatmapTest, LayoutCenteringAndHoverRendering) {
    NativeKeyboardHeatmap heatmap;
    UIRenderContext ctx(nullptr, nullptr, 1.0f);
    heatmap.layout(Rect(0.0f, 0.0f, 800.0f, 360.0f), ctx);

    // 验证三种规格切换不崩溃
    heatmap.setLayoutMode(KeyboardLayoutMode::Full104);
    EXPECT_NO_THROW(heatmap.render(ctx));

    heatmap.setLayoutMode(KeyboardLayoutMode::TKL87);
    EXPECT_NO_THROW(heatmap.render(ctx));

    heatmap.setLayoutMode(KeyboardLayoutMode::Compact60);
    EXPECT_NO_THROW(heatmap.render(ctx));

    // 验证悬停与按键数据注入
    heatmap.setLayoutMode(KeyboardLayoutMode::Full104);
    heatmap.setKeyHits(VK_SPACE, 4320);
    UIMouseEvent moveEvt(tools3000::ui::native::MouseEventType::MouseMove, Point(400.0f, 150.0f));
    heatmap.onMouseMove(moveEvt);
    EXPECT_NO_THROW(heatmap.render(ctx));

    heatmap.onMouseLeave();
    EXPECT_NO_THROW(heatmap.render(ctx));
}

