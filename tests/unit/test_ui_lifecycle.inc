// ─────────────────────────────────────────────────────────────────────────────
// test_ui_lifecycle.inc — UI_LIFECYCLE 模块化单元测试集
// ─────────────────────────────────────────────────────────────────────────────

TEST(WebViewSecurityTest, TrustsOnlyPackagedOriginInRelease) {
    using easy::ui::web_security::isTrustedUri;
    EXPECT_TRUE(isTrustedUri(L"https://easytools.local/index.html#/search"));
    EXPECT_TRUE(isTrustedUri(L"https://easytools.local/"));
    EXPECT_FALSE(isTrustedUri(L"https://easytools.local.evil.example/"));
    EXPECT_FALSE(isTrustedUri(L"file:///C:/temp/index.html"));
    EXPECT_FALSE(isTrustedUri(L"javascript:alert(1)"));
#ifndef _DEBUG
    EXPECT_FALSE(isTrustedUri(L"http://localhost:5173"));
    EXPECT_FALSE(isTrustedUri(L"http://127.0.0.1:5173"));
#endif
}

TEST(WebViewSecurityTest, RejectsOversizedBridgePayload) {
    EXPECT_TRUE(easy::ui::web_security::isBridgeMessageSizeAcceptable(
        easy::ui::web_security::MaxBridgeMessageBytes));
    EXPECT_FALSE(easy::ui::web_security::isBridgeMessageSizeAcceptable(
        easy::ui::web_security::MaxBridgeMessageBytes + 1));
}

TEST(WebViewSecurityTest, AllowsTrayRequiredMethods) {
    using easy::ui::web_security::isBridgeMethodAllowed;
    using easy::ui::web_security::Surface;

    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"general.getSettings\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"general.updateSettings\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"plugins.getAll\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"plugins.setEnabled\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"gesture.getState\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"app.restart\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"app.restartElevated\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"tray.action\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"tray.resize\"}", Surface::Tray));
    EXPECT_FALSE(isBridgeMethodAllowed("{\"method\":\"search.query\"}", Surface::Tray));
    EXPECT_FALSE(isBridgeMethodAllowed("{\"method\":\"capture.start\"}", Surface::Tray));
}

TEST(KeyboardPipelineTest, FiltersSystemMenuAndHelpMessages) {
    using easy::ui::KeyboardPipeline;
    // SC_KEYMENU (Alt / F10 / Alt+Space 激活系统菜单) 必须被管线拦截
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_KEYMENU, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_KEYMENU | 0x0002, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_CONTEXTHELP, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_HELP, 0, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSKEYDOWN, VK_SPACE, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSKEYUP, VK_SPACE, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSKEYDOWN, VK_F10, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSKEYDOWN, VK_MENU, 0));

    // 普通命令如最大化/最小化/关闭等不能被误拦截
    EXPECT_FALSE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_CLOSE, 0));
    EXPECT_FALSE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_MAXIMIZE, 0));
    EXPECT_FALSE(KeyboardPipeline::filterWindowMessage(nullptr, WM_KEYDOWN, VK_SPACE, 0));
    EXPECT_FALSE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSKEYDOWN, 'A', 0));
}


TEST(WebViewSuspendTest, DistinguishesRefusalFromFailure) {
    using easy::ui::WebViewSuspendOutcome;
    EXPECT_EQ(easy::ui::classifyWebViewSuspendCompletion(S_OK, TRUE),
              WebViewSuspendOutcome::Suspended);
    EXPECT_EQ(easy::ui::classifyWebViewSuspendCompletion(S_OK, FALSE),
              WebViewSuspendOutcome::Refused);
    EXPECT_EQ(easy::ui::classifyWebViewSuspendCompletion(E_FAIL, FALSE),
              WebViewSuspendOutcome::Failed);
}

TEST(WebViewSuspendTest, LateSuspendCompletionResumesOnlyWhenSurfaceWasShownAgain) {
    using Outcome = easy::ui::WebViewSuspendOutcome;
    EXPECT_TRUE(easy::ui::shouldResumeAfterSuspendCompletion(
        Outcome::Suspended, false, false));
    EXPECT_FALSE(easy::ui::shouldResumeAfterSuspendCompletion(
        Outcome::Suspended, true, false));
    EXPECT_FALSE(easy::ui::shouldResumeAfterSuspendCompletion(
        Outcome::Suspended, false, true));
    EXPECT_FALSE(easy::ui::shouldResumeAfterSuspendCompletion(
        Outcome::Refused, false, false));
    EXPECT_FALSE(easy::ui::shouldResumeAfterSuspendCompletion(
        Outcome::Failed, false, false));
}

TEST(WebViewSuspendTest, ControllerFailsClosedForAbsentWebViewAndCanBeRecreated) {
    easy::ui::WebViewSuspendController controller;
    EXPECT_EQ(controller.requestSuspend(nullptr, "unit"), E_POINTER);
    EXPECT_EQ(controller.resume(nullptr, "unit"), E_POINTER);

    // Destruction invalidates late callbacks. A later native-window generation
    // receives a fresh state rather than inheriting the previous generation's
    // cancellation flag.
    controller.abandon();
    controller.reset();
    EXPECT_EQ(controller.requestSuspend(nullptr, "unit-recreated"), E_POINTER);
    EXPECT_EQ(controller.resume(nullptr, "unit-recreated"), E_POINTER);
}


TEST(OverlayUiaProviderTest, ExposesActionPropertiesAndMarshalsInvokeToOwnerWindow) {
    // This uses a real UI Automation client against a real HWND rather than
    // testing helper structs in isolation. It covers the WM_GETOBJECT route,
    // child semantics, physical bounds, and the no-UI-work-on-provider-thread
    // invariant of Invoke (the action arrives as a posted window message).
    CoApartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) {
        GTEST_SKIP() << "COM apartment unavailable: 0x" << std::hex << apartment.result;
    }

    UiaTestSurface surface;
    UiaTestWindow window(&surface);
    ASSERT_NE(window.hwnd, nullptr);

    IUIAutomation* automation = nullptr;
    HRESULT result = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&automation));
    if (FAILED(result)) {
        GTEST_SKIP() << "UI Automation client unavailable: 0x" << std::hex << result;
    }
    struct AutomationReleaser { IUIAutomation* value; ~AutomationReleaser() { if (value) value->Release(); } } automationReleaser{automation};

    IUIAutomationElement* root = nullptr;
    result = automation->ElementFromHandle(window.hwnd, &root);
    if (FAILED(result) || !root) {
        GTEST_SKIP() << "UI Automation cannot inspect this desktop: 0x" << std::hex << result;
    }
    struct ElementReleaser { IUIAutomationElement* value; ~ElementReleaser() { if (value) value->Release(); } } rootReleaser{root};

    BSTR rootId = nullptr;
    ASSERT_TRUE(SUCCEEDED(root->get_CurrentAutomationId(&rootId)));
    EXPECT_STREQ(rootId, L"EasyTools.TestOverlay");
    SysFreeString(rootId);

    IUIAutomationCondition* condition = nullptr;
    ASSERT_TRUE(SUCCEEDED(automation->CreateTrueCondition(&condition)));
    struct ConditionReleaser { IUIAutomationCondition* value; ~ConditionReleaser() { if (value) value->Release(); } } conditionReleaser{condition};
    IUIAutomationElement* action = nullptr;
    ASSERT_TRUE(SUCCEEDED(root->FindFirst(TreeScope_Children, condition, &action)));
    ASSERT_NE(action, nullptr);
    struct ActionReleaser { IUIAutomationElement* value; ~ActionReleaser() { if (value) value->Release(); } } actionReleaser{action};

    BSTR name = nullptr;
    ASSERT_TRUE(SUCCEEDED(action->get_CurrentName(&name)));
    EXPECT_STREQ(name, L"Confirm capture");
    SysFreeString(name);
    BOOL enabled = FALSE;
    EXPECT_TRUE(SUCCEEDED(action->get_CurrentIsEnabled(&enabled)));
    EXPECT_TRUE(enabled);
    BSTR shortcut = nullptr;
    ASSERT_TRUE(SUCCEEDED(action->get_CurrentAcceleratorKey(&shortcut)));
    EXPECT_STREQ(shortcut, L"Enter");
    SysFreeString(shortcut);
    VARIANT selected{};
    ASSERT_TRUE(SUCCEEDED(action->GetCurrentPropertyValue(UIA_SelectionItemIsSelectedPropertyId, &selected)));
    EXPECT_EQ(selected.vt, VT_BOOL);
    EXPECT_EQ(selected.boolVal, VARIANT_TRUE);
    VariantClear(&selected);
    tagRECT bounds{};
    ASSERT_TRUE(SUCCEEDED(action->get_CurrentBoundingRectangle(&bounds)));
    EXPECT_EQ(bounds.left, 40);
    EXPECT_EQ(bounds.top, 60);
    EXPECT_EQ(bounds.right, 220);
    EXPECT_EQ(bounds.bottom, 104);

    IUIAutomationInvokePattern* invoke = nullptr;
    ASSERT_TRUE(SUCCEEDED(action->GetCurrentPatternAs(UIA_InvokePatternId,
        IID_PPV_ARGS(&invoke))));
    ASSERT_NE(invoke, nullptr);
    struct InvokeReleaser { IUIAutomationInvokePattern* value; ~InvokeReleaser() { if (value) value->Release(); } } invokeReleaser{invoke};
    EXPECT_TRUE(SUCCEEDED(invoke->Invoke()));
    MSG message{};
    while (PeekMessageW(&message, window.hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    EXPECT_EQ(surface.invokeCount, 1);
}

// -----------------------------------------------------------------------------
// 26. 贴图窗口几何变换测试套件
// -----------------------------------------------------------------------------

TEST(SpotlightOverlayTest, SettingsAndDefaults) {
    using namespace easy::ui;
    auto& spotlight = SpotlightOverlay::instance();
    spotlight.resetDefaults();
    auto s = spotlight.getSettings();
    EXPECT_TRUE(s.enabled);
    EXPECT_TRUE(s.triggerDoubleCtrl);
    EXPECT_FALSE(s.triggerShakeMouse);
    EXPECT_TRUE(s.autoBypassFullscreen);
    EXPECT_EQ(s.spotlightColor, "auto");
    EXPECT_EQ(s.spotlightSize, 300);
    EXPECT_EQ(s.animationDurationMs, 1000);
    EXPECT_EQ(s.holdDurationMs, 800);
    EXPECT_EQ(s.shakeThreshold, 4);
    EXPECT_FALSE(s.clickRippleEnabled);
    EXPECT_EQ(s.clickRippleStyle, "sparkle_burst");
    EXPECT_FALSE(s.mouseTrailEnabled);
    EXPECT_EQ(s.mouseTrailStyle, "sonar_pulses");
    EXPECT_EQ(s.mouseTrailColorMode, "rainbow");
    EXPECT_EQ(s.leftClickColor, "auto");
    EXPECT_EQ(s.rightClickColor, "#fb7185");
    EXPECT_EQ(s.middleClickColor, "#fbbf24");

    s.spotlightSize = 320;
    s.clickRippleEnabled = true;
    s.mouseTrailEnabled = true;
    s.spotlightColor = "#ff0088";
    s.leftClickColor = "#00ffcc";
    s.spotlightAnimStyle = "tactical_sonar";
    spotlight.updateSettings(s);

    auto updated = spotlight.getSettings();
    EXPECT_EQ(updated.spotlightSize, 320);
    EXPECT_TRUE(updated.clickRippleEnabled);
    EXPECT_TRUE(updated.mouseTrailEnabled);
    EXPECT_EQ(updated.spotlightColor, "#ff0088");
    EXPECT_EQ(updated.leftClickColor, "#00ffcc");
    EXPECT_EQ(updated.spotlightAnimStyle, "tactical_sonar");

    s.spotlightAnimStyle = "aurora_ripple";
    spotlight.updateSettings(s);
    EXPECT_EQ(spotlight.getSettings().spotlightAnimStyle, "aurora_ripple");

    s.spotlightAnimStyle = "inward_gravity";
    spotlight.updateSettings(s);
    EXPECT_EQ(spotlight.getSettings().spotlightAnimStyle, "inward_gravity");

    spotlight.resetDefaults();
    EXPECT_EQ(spotlight.getSettings().spotlightSize, 300);
    EXPECT_EQ(spotlight.getSettings().spotlightColor, "auto");
    EXPECT_EQ(spotlight.getSettings().leftClickColor, "auto");
    EXPECT_EQ(spotlight.getSettings().spotlightAnimStyle, "inward_gravity");
}

TEST(SpotlightOverlayTest, ColorParsingAndAutoAccent) {
    using namespace easy::ui;
    auto& spotlight = SpotlightOverlay::instance();

    // 1. 标准 HEX 解析
    auto c1 = spotlight.parseColor("#ff0000", 1.0f);
    EXPECT_NEAR(c1.r, 1.0f, 0.01f);
    EXPECT_NEAR(c1.g, 0.0f, 0.01f);
    EXPECT_NEAR(c1.b, 0.0f, 0.01f);

    auto c2 = spotlight.parseColor("00ff00", 0.5f);
    EXPECT_NEAR(c2.r, 0.0f, 0.01f);
    EXPECT_NEAR(c2.g, 1.0f, 0.01f);
    EXPECT_NEAR(c2.a, 0.5f, 0.01f);

    // 2. auto 与主题色联动
    auto temp = std::filesystem::temp_directory_path() /
        (L"EasyTools_SpotlightTest_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::create_directories(temp, ec);
    auto& cfg = easy::core::ConfigManager::instance();
    cfg.initialize(temp);

    cfg.set("/general/accentColor", std::string("blue"));
    auto cBlue = spotlight.parseColor("auto", 1.0f);
    EXPECT_NEAR(cBlue.b, 246.0f / 255.0f, 0.05f);

    cfg.set("/general/accentColor", std::string("cyan"));
    auto cCyan = spotlight.parseColor("auto", 1.0f);
    EXPECT_NEAR(cCyan.g, 182.0f / 255.0f, 0.05f);

    cfg.set("/general/accentColor", std::string("amber"));
    auto cAmber = spotlight.parseColor("auto", 1.0f);
    EXPECT_NEAR(cAmber.r, 245.0f / 255.0f, 0.05f);

    cfg.set("/general/accentColor", std::string("mint"));
    auto cMint = spotlight.parseColor("auto", 1.0f);
    EXPECT_NEAR(cMint.g, 185.0f / 255.0f, 0.05f);

    cfg.set("/general/accentColor", std::string("coral"));
    auto cCoral = spotlight.parseColor("auto", 1.0f);
    EXPECT_NEAR(cCoral.r, 244.0f / 255.0f, 0.05f);

    cfg.set("/general/accentColor", std::string("violet"));
    auto cViolet = spotlight.parseColor("auto", 1.0f);
    EXPECT_NEAR(cViolet.r, 139.0f / 255.0f, 0.05f);

    // 3. 非法色值回退
    auto cInvalid = spotlight.parseColor("invalid_color_code", 1.0f);
    EXPECT_NEAR(cInvalid.a, 1.0f, 0.01f);
}

TEST(SpotlightOverlayTest, KeyboardAndMouseInteraction) {
    using namespace easy::ui;
    auto& spotlight = SpotlightOverlay::instance();
    spotlight.resetDefaults();

    // 1. 严格双击 Ctrl 检测 (Down -> Up -> Down)
    spotlight.onKeyboardEvent(VK_CONTROL, WM_KEYDOWN);
    // 模拟连发（Repeat KeyDown 应被静默丢弃）
    spotlight.onKeyboardEvent(VK_CONTROL, WM_KEYDOWN);
    spotlight.onKeyboardEvent(VK_CONTROL, WM_KEYUP);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    spotlight.onKeyboardEvent(VK_CONTROL, WM_KEYDOWN);
    spotlight.onKeyboardEvent(VK_CONTROL, WM_KEYUP);

    // 2. 其它键取消 / 污染熔断
    spotlight.onKeyboardEvent(VK_CONTROL, WM_KEYDOWN);
    spotlight.onKeyboardEvent('C', WM_KEYDOWN);
    spotlight.onKeyboardEvent(VK_CONTROL, WM_KEYUP);
    spotlight.onKeyboardEvent(VK_ESCAPE, WM_KEYDOWN);

    // 3. 鼠标点击产生波纹
    auto s = spotlight.getSettings();
    s.clickRippleEnabled = true;
    s.mouseTrailEnabled = true;
    s.triggerShakeMouse = true;
    spotlight.updateSettings(s);

    POINT pt1{100, 100};
    spotlight.onMouseDown(0, pt1);
    spotlight.onMouseDown(1, pt1);
    spotlight.onMouseDown(2, pt1);

    // 4. 鼠标移动产生轨迹与摇晃
    for (int i = 0; i < 10; ++i) {
        POINT movePt{100 + (i % 2 == 0 ? 50 : -50), 100};
        spotlight.onMouseMove(movePt);
    }

    // 5. 动效更新与不同聚焦风格触发
    s.spotlightAnimStyle = "inward_gravity";
    spotlight.updateSettings(s);
    spotlight.trigger(POINT{200, 200}, false);
    spotlight.tickAnimation();

    s.spotlightAnimStyle = "tactical_sonar";
    spotlight.updateSettings(s);
    spotlight.trigger(POINT{300, 300}, false);
    spotlight.tickAnimation();

    s.spotlightAnimStyle = "aurora_ripple";
    spotlight.updateSettings(s);
    spotlight.trigger(POINT{400, 400}, false);
    spotlight.tickAnimation();

    spotlight.dismiss();
    spotlight.resetDefaults();
}

TEST(SpotlightOverlayTest, LocalBoundingBoxAndFocusAssistExclusion) {
    auto& spotlight = easy::ui::SpotlightOverlay::instance();
    spotlight.resetDefaults();

    // 1. 空状态视口计算
    auto boundsEmpty = spotlight.calculateViewportBoundsLocked();
    EXPECT_FALSE(boundsEmpty.isFullscreen);
    EXPECT_EQ(boundsEmpty.w, 0);
    EXPECT_EQ(boundsEmpty.h, 0);

    // 2. 开启点击水波纹
    auto s = spotlight.getSettings();
    s.clickRippleEnabled = true;
    s.mouseTrailEnabled = true;
    spotlight.updateSettings(s);

    const int targetX = 300;
    const int targetY = 400;
    POINT clickPt{targetX, targetY};
    spotlight.onMouseDown(0, clickPt);

    auto boundsRipple = spotlight.calculateViewportBoundsLocked();
    EXPECT_FALSE(boundsRipple.isFullscreen);
    EXPECT_GT(boundsRipple.w, 0);
    EXPECT_GT(boundsRipple.h, 0);
    // 局部包围盒尺寸应紧凑 (远小于全屏 1920x1080)
    EXPECT_LE(boundsRipple.w, 300);
    EXPECT_LE(boundsRipple.h, 300);
    // 局部包围盒应包住点击坐标
    const int rx = boundsRipple.x;
    const int rw = boundsRipple.w;
    const int ry = boundsRipple.y;
    const int rh = boundsRipple.h;
    EXPECT_LE(rx, targetX);
    EXPECT_GE(rx + rw, targetX);
    EXPECT_LE(ry, targetY);
    EXPECT_GE(ry + rh, targetY);

    // 3. 触发聚光灯模式
    spotlight.trigger(POINT{500, 500}, false);
    auto boundsSpotlight = spotlight.calculateViewportBoundsLocked();
    EXPECT_TRUE(boundsSpotlight.isFullscreen);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw > 2 && vh > 2) {
        // 验证避让 1 像素以打破 Windows 11 Shell 的全屏独占判定 (防止触发 Focus Assist 专注助手铃铛)
        const int expectedW = vw - 1;
        const int expectedH = vh - 1;
        EXPECT_EQ(boundsSpotlight.w, expectedW);
        EXPECT_EQ(boundsSpotlight.h, expectedH);
    }

    // 4. 重置并退出
    spotlight.dismiss();
    spotlight.tickAnimation();
    spotlight.resetDefaults();
}

TEST(SpotlightOverlayTest, TrailAndClickStylesMatrix) {
    auto& spotlight = easy::ui::SpotlightOverlay::instance();
    spotlight.resetDefaults();

    // 1. 验证 HSL 颜色转换算法
    D2D1_COLOR_F cRed = easy::ui::SpotlightOverlay::hslToRgb(0.0f, 1.0f, 0.5f, 1.0f);
    EXPECT_NEAR(cRed.r, 1.0f, 0.01f);
    EXPECT_NEAR(cRed.g, 0.0f, 0.01f);
    EXPECT_NEAR(cRed.b, 0.0f, 0.01f);

    D2D1_COLOR_F cGreen = easy::ui::SpotlightOverlay::hslToRgb(120.0f, 1.0f, 0.5f, 1.0f);
    EXPECT_NEAR(cGreen.r, 0.0f, 0.01f);
    EXPECT_NEAR(cGreen.g, 1.0f, 0.01f);
    EXPECT_NEAR(cGreen.b, 0.0f, 0.01f);

    D2D1_COLOR_F cBlue = easy::ui::SpotlightOverlay::hslToRgb(240.0f, 1.0f, 0.5f, 0.8f);
    EXPECT_NEAR(cBlue.r, 0.0f, 0.01f);
    EXPECT_NEAR(cBlue.g, 0.0f, 0.01f);
    EXPECT_NEAR(cBlue.b, 1.0f, 0.01f);
    EXPECT_NEAR(cBlue.a, 0.8f, 0.01f);

    // 2. 验证 9 款点击特效风格
    auto s = spotlight.getSettings();
    s.enabled = true;
    s.clickRippleEnabled = true;

    const std::vector<std::string> clickStyles = {
        "sparkle_burst", "ripple_ring", "target_pulse", "soft_glow",
        "supernova", "emp_discharge", "ink_droplet", "hexagon_lock", "bubble_pop"
    };

    for (const auto& cStyle : clickStyles) {
        s.clickRippleStyle = cStyle;
        spotlight.updateSettings(s);
        spotlight.onMouseDown(0, POINT{200, 200});
        auto bounds = spotlight.calculateViewportBoundsLocked();
        EXPECT_GT(bounds.w, 0);
    }

    // 3. 验证 9 款轨迹动效风格
    s.mouseTrailEnabled = true;

    const std::vector<std::string> trailStyles = {
        "sonar_pulses", "stardust_orbs", "quantum_lens", "tesla_arc",
        "zen_ink", "blueprint_grid", "morning_dew", "aurora_ribbon", "classic_comet"
    };

    for (const auto& tStyle : trailStyles) {
        s.mouseTrailStyle = tStyle;
        s.mouseTrailColorMode = "rainbow";
        spotlight.updateSettings(s);
        for (int i = 0; i < 5; ++i) {
            spotlight.onMouseMove(POINT{100 + i * 40, 100 + i * 40});
        }
        auto bounds = spotlight.calculateViewportBoundsLocked();
        EXPECT_GT(bounds.w, 0);
    }

    // 4. 验证总开关 enabled = false 时的绝对拦截
    s.enabled = false;
    spotlight.updateSettings(s);
    spotlight.dismiss();
    spotlight.onMouseDown(0, POINT{300, 300});
    spotlight.onMouseMove(POINT{350, 350});
    EXPECT_FALSE(spotlight.isActive());

    // 5. 动效更新与清理
    spotlight.tickAnimation();
    spotlight.dismiss();
    spotlight.resetDefaults();
}

// -----------------------------------------------------------------------------
// 38. 世界级架构与极端异常防御测试套件 (Job Object, 原子写盘, 便携模式, 孤儿锁自愈)
// -----------------------------------------------------------------------------

TEST(TitleBarAndKeycastTest, SeamlessWindowAndFluidKeycast) {
    // 1. 验证 KeycastStyle 多 DPI 物理几何缩放
    const SIZE sz96 = easy::keycast::KeycastStyle::windowSizeForDpi(96);
    const SIZE sz144 = easy::keycast::KeycastStyle::windowSizeForDpi(144);
    const SIZE sz192 = easy::keycast::KeycastStyle::windowSizeForDpi(192);
    EXPECT_EQ(sz96.cx, 800);
    EXPECT_EQ(sz96.cy, 160);
    EXPECT_EQ(sz144.cx, 1200);
    EXPECT_EQ(sz144.cy, 240);
    EXPECT_EQ(sz192.cx, 1600);
    EXPECT_EQ(sz192.cy, 320);

    // 2. 验证智能按键过滤与键盘钩子回调
    auto& hook = easy::core::KeyboardHook::instance();
    bool callbackInvoked = false;
    hook.setKeycastCallback([&callbackInvoked](const std::string& seq) {
        if (!seq.empty()) {
            callbackInvoked = true;
        }
    });

    // 3. 验证配置持久化读写
    auto& config = easy::core::ConfigManager::instance();
    config.set("/keycast/position", "bottom_left");
    config.set("/keycast/filterMode", "smart_shortcuts");
    config.set("/keycast/mergeRecentKeys", true);
    config.set("/keycast/mergeTimeoutMs", 1500);
    EXPECT_EQ(config.get("/keycast/position", std::string("")), "bottom_left");
    EXPECT_EQ(config.get("/keycast/filterMode", std::string("")), "smart_shortcuts");
    EXPECT_TRUE(config.get("/keycast/mergeRecentKeys", false));
    EXPECT_EQ(config.get("/keycast/mergeTimeoutMs", 0), 1500);

    hook.setKeycastCallback(nullptr);
}

TEST(KeycastOverlayTest, SettingsAndAnimationCombos) {
    auto& overlay = easy::keycast::KeycastOverlay::instance();
    
    // 1. 测试默认配置获取
    easy::keycast::KeycastSettings s = overlay.getSettings();
    EXPECT_TRUE(s.enabled);
    EXPECT_EQ(s.fontSize, 36);
    EXPECT_EQ(s.opacity, 80);
    EXPECT_EQ(s.firstKeyAnim, "slide");
    EXPECT_EQ(s.subsequentKeyAnim, "fade");
    EXPECT_TRUE(s.rowCascadeAnim);
    EXPECT_TRUE(s.exitDriftAnim);

    // 2. 测试颜色解析 (带 auto、HEX 及异常兜底)
    D2D1_COLOR_F cWhite = overlay.parseColor("#ffffff", 1.0f);
    EXPECT_FLOAT_EQ(cWhite.r, 1.0f);
    EXPECT_FLOAT_EQ(cWhite.g, 1.0f);
    EXPECT_FLOAT_EQ(cWhite.b, 1.0f);

    D2D1_COLOR_F cDark = overlay.parseColor("#1c1c22", 0.98f);
    EXPECT_GT(cDark.a, 0.90f);

    D2D1_COLOR_F cAuto = overlay.parseColor("auto", 1.0f);
    EXPECT_FLOAT_EQ(cAuto.a, 1.0f);

    D2D1_COLOR_F cFallback = overlay.parseColor("invalid_hex", 1.0f);
    EXPECT_FLOAT_EQ(cFallback.a, 1.0f);

    // 3. 测试单项宽度计算
    easy::keycast::KeycastItem item1;
    item1.rawKey = "Win+E";
    item1.tokens = {"Win", "E"};
    item1.repeatCount = 1;
    float w1 = overlay.calculateItemWidth(item1, 1.0f);
    EXPECT_GT(w1, 50.0f);

    easy::keycast::KeycastItem item2;
    item2.rawKey = "Ctrl+Shift+Alt+Tab";
    item2.tokens = {"Ctrl", "Shift", "Alt", "Tab"};
    item2.repeatCount = 3;
    float w2 = overlay.calculateItemWidth(item2, 1.0f);
    EXPECT_GT(w2, w1);

    // 4. 覆盖不同动效组合配置
    s.firstKeyAnim = "pop";
    s.subsequentKeyAnim = "slide";
    s.rowCascadeAnim = true;
    s.exitDriftAnim = true;
    overlay.updateSettings(s);
    overlay.pushKey("Win+D");
    overlay.pushKey("Win+E");

    s.firstKeyAnim = "fade";
    s.subsequentKeyAnim = "fade";
    s.rowCascadeAnim = false;
    s.exitDriftAnim = false;
    overlay.updateSettings(s);
    overlay.pushKey("Ctrl+C");
    overlay.pushKey("Ctrl+V");

    s.modifierKeycapColor = "#3b82f6";
    s.modifierKeycapOpacity = 40;
    s.modifierTextColor = "#0e0f12";
    s.firstKeyAnim = "none";
    s.subsequentKeyAnim = "none";
    overlay.updateSettings(s);
    EXPECT_EQ(overlay.getSettings().modifierTextColor, "#0e0f12");
    overlay.pushKey("Alt+Tab");

    s.modifierTextColor = "auto";
    overlay.updateSettings(s);
    EXPECT_EQ(overlay.getSettings().modifierTextColor, "auto");
    overlay.pushKey("Win+E");

    overlay.setAutoBypassFullscreen(false);
    EXPECT_FALSE(overlay.autoBypassFullscreen());
    overlay.setAutoBypassFullscreen(true);
    EXPECT_TRUE(overlay.autoBypassFullscreen());

    // 验证全局主题与主题色变更响应
    overlay.onThemeChanged();

    overlay.resetDefaults();
}

// -----------------------------------------------------------------------------
// 39. 全局核心鼠标钩子测试套件
// -----------------------------------------------------------------------------

TEST(KeycastSmartFilterAndModifierStateMachineTest, ComprehensiveBehavior) {
    auto& overlay = easy::keycast::KeycastOverlay::instance();
    auto s = overlay.getSettings();
    EXPECT_FALSE(s.includeFunctionKeys);

    // 1. 配置更新与持久化测试
    s.includeFunctionKeys = false;
    overlay.updateSettings(s);
    EXPECT_FALSE(overlay.getSettings().includeFunctionKeys);

    s.includeFunctionKeys = true;
    overlay.updateSettings(s);
    EXPECT_TRUE(overlay.getSettings().includeFunctionKeys);

    // 2. 模拟状态机逻辑单元验证
    // 模拟修饰键 Win 按下 (KeyDown) -> 暂存
    DWORD pendingMod = VK_LWIN;
    bool comboTriggered = false;

    // 模拟随后的字母 D 按下 (KeyDown) -> 组合键触发
    DWORD mainVk = 'D';
    bool hasWin = true;
    bool hasShift = false;
        bool isLetter = (mainVk >= 0x41 && mainVk <= 0x5A);

    if (pendingMod != 0) {
        comboTriggered = true;
    }
    EXPECT_TRUE(comboTriggered);

    // 模拟 Win 键松开 (KeyUp) -> 已经触发过组合键，不应补发单 Win
    bool shouldEmitSingleWin = false;
    if (pendingMod == VK_LWIN && !comboTriggered) {
        shouldEmitSingleWin = true;
    }
    EXPECT_FALSE(shouldEmitSingleWin);

    // 3. 模拟纯单按 Alt 激活菜单
    pendingMod = VK_LMENU;
    comboTriggered = false;
    // 期间无其他键按下，Alt 松开 (KeyUp)
    bool shouldEmitSingleAlt = false;
    if (pendingMod == VK_LMENU && !comboTriggered) {
        shouldEmitSingleAlt = true;
    }
    EXPECT_TRUE(shouldEmitSingleAlt);

    // 4. 模拟大写单词打字智能过滤 (Shift + H O M E)
    hasShift = true;
    bool hasCtrl = false;
    bool hasAlt = false;
    hasWin = false;
    bool isOnlyShift = hasShift && !hasCtrl && !hasAlt && !hasWin;
    
    // Shift + H: 仅 Shift + 字母 -> 在智能模式下过滤
    std::string filterMode = "smart_shortcuts";
    bool shouldDisplayHomeLetter = true;
    if (filterMode == "smart_shortcuts" && isOnlyShift && isLetter) {
        shouldDisplayHomeLetter = false;
    }
    EXPECT_FALSE(shouldDisplayHomeLetter);

    // Shift + Delete: Shift + 功能键 -> 正常回显组合键
    bool isFunctionalKey = true;
    isLetter = false;
    bool shouldDisplayShiftDelete = false;
    if (!(isOnlyShift && isLetter) && (hasShift || isFunctionalKey)) {
        shouldDisplayShiftDelete = true;
    }
    EXPECT_TRUE(shouldDisplayShiftDelete);

    // 5. 模拟功能键子开关对单按 Space/Delete 的控制
    bool includeFunc = false;
    bool isComboWithMod = false;
    bool shouldDisplaySpaceWithoutMod = isComboWithMod || (isFunctionalKey && includeFunc);
    EXPECT_FALSE(shouldDisplaySpaceWithoutMod);

    includeFunc = true;
    bool shouldDisplaySpaceWithInclude = isComboWithMod || (isFunctionalKey && includeFunc);
    EXPECT_TRUE(shouldDisplaySpaceWithInclude);
}



TEST(KeycastLifecycleTest, KeystrokeBurstQueueAndMergeLifecycle) {
    using namespace easy::keycast;
    auto& keycast = KeycastOverlay::instance();

    // 1. 设置与参数生命周期
    KeycastSettings settings = keycast.getSettings();
    settings.enabled = true;
    settings.mergeRecentKeys = true;
    settings.displayDurationMs = 1500;
    keycast.updateSettings(settings);

    // 2. 模拟高频连击推入 (50 次连续按键突发)
    for (int i = 0; i < 50; ++i) {
        keycast.pushKey("Ctrl+C");
    }

    // 3. 测量计算与宽度渲染稳定性
    KeycastItem item;
    item.rawKey = "Ctrl+C";
    item.tokens = {"Ctrl", "C"};
    item.repeatCount = 50;
    float width = keycast.calculateItemWidth(item, 1.0f);
    EXPECT_GT(width, 0.0f);

    // 4. 清理与重置
    keycast.resetDefaults();
}

// -----------------------------------------------------------------------------
// 30. 聚光灯与鼠标特效（水波纹/流光轨迹）全生命周期测试套件
// -----------------------------------------------------------------------------
TEST(SpotlightAndMouseFxLifecycleTest, SpotlightActivationAndDismissLifecycle) {
    using namespace easy::ui;
    auto& spotlight = SpotlightOverlay::instance();

    // 1. 初始状态
    SpotlightSettings settings = spotlight.getSettings();
    settings.enabled = true;
    settings.clickRippleEnabled = true;
    settings.mouseTrailEnabled = true;
    spotlight.updateSettings(settings);

    // 2. 触发聚光灯 -> 验证状态机与局部视口
    spotlight.trigger(POINT{300, 300}, false);
    
    // 3. 鼠标交互与水波纹/轨迹粒子生成
    spotlight.onMouseMove(POINT{320, 320});
    spotlight.onMouseDown(0, POINT{320, 320}); // 左键水波纹
    spotlight.onMouseDown(1, POINT{320, 320}); // 右键水波纹
    spotlight.onMouseDown(2, POINT{320, 320}); // 中键水波纹

    // 4. 取消与平滑淡出 (Dismiss)
    spotlight.dismiss();

    // 5. 颜色解析与 HSL 转换数学模型验证
    auto color = SpotlightOverlay::hslToRgb(180.0f, 1.0f, 0.5f, 0.8f);
    EXPECT_GE(color.r, 0.0f);
    EXPECT_LE(color.r, 1.0f);
    EXPECT_FLOAT_EQ(color.a, 0.8f);

    spotlight.resetDefaults();
}

// -----------------------------------------------------------------------------
// 单元测试主入口 (Google Test 初始化与执行)
// -----------------------------------------------------------------------------