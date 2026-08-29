// ─────────────────────────────────────────────────────────────────────────────
// test_dialog.inc — DIALOG 模块化单元测试集
// ─────────────────────────────────────────────────────────────────────────────

TEST(PathMemoryManagerTest, AppMemoryAndHistory) {
    auto& mgr = easy::dialog::PathMemoryManager::instance();
    mgr.setEnabled(true);
    mgr.setPerAppMemoryEnabled(true);
    mgr.setQuickSwitchEnabled(true);
    mgr.setRibbonEnabled(true);
    mgr.setRibbonPosition("top-right");

    EXPECT_TRUE(mgr.isEnabled());
    EXPECT_TRUE(mgr.isPerAppMemoryEnabled());
    EXPECT_TRUE(mgr.isQuickSwitchEnabled());
    EXPECT_TRUE(mgr.isRibbonEnabled());
    EXPECT_EQ(mgr.getRibbonPosition(), "top-right");

    // 记录应用专属路径
    mgr.clearAppMemories();
    mgr.recordAppPath("Code.exe", "D:\\repo\\easyTools");
    EXPECT_EQ(mgr.getAppPath("Code.exe"), "D:\\repo\\easyTools");
    EXPECT_EQ(mgr.getAppPath("CODE.EXE"), "D:\\repo\\easyTools"); // 同一 EXE 多实例共享键

    mgr.recordAppPath("chrome.exe", "C:\\Downloads");
    EXPECT_EQ(mgr.getAppPath("chrome.exe"), "C:\\Downloads");

    // 固定母工作区优先级
    mgr.setAppFixedWorkspace("Code.exe", "C:\\repo", true);
    EXPECT_EQ(mgr.getEffectiveAppPath("Code.exe"), "C:\\repo");
    mgr.recordAppPath("Code.exe", "C:\\repo\\OpenERP"); // 动态变更不影响固定母工作区
    EXPECT_EQ(mgr.getEffectiveAppPath("Code.exe"), "C:\\repo");
    EXPECT_EQ(mgr.getAppPath("Code.exe"), "C:\\repo\\OpenERP");

    mgr.setAppFixedWorkspace("Code.exe", "C:\\repo", false);
    // 未固定时必须精确恢复上一次目录，不得擅自提升到收藏的“母工作区”。
    EXPECT_EQ(mgr.getEffectiveAppPath("Code.exe"), "C:\\repo\\OpenERP");
    mgr.recordAppPath("Code.exe", "E:\\StandaloneProject");
    EXPECT_EQ(mgr.getEffectiveAppPath("Code.exe"), "E:\\StandaloneProject");

    mgr.removeAppMemory("new-app.exe");
    EXPECT_TRUE(mgr.getEffectiveAppPath("new-app.exe").empty()); // 首次使用保持系统原生目录

    auto memories = mgr.getAllAppMemories();
    EXPECT_GE(memories.size(), 2u);


    mgr.removeAppMemory("chrome.exe");
    EXPECT_TRUE(mgr.getAppPath("chrome.exe").empty());

    // 最近使用文件夹 (MRU)
    mgr.clearRecentPaths();
    mgr.recordRecentPath("C:\\FolderA");
    mgr.recordRecentPath("C:\\FolderB");
    mgr.recordRecentPath("C:\\FolderA"); // 重复置顶
    auto recent = mgr.getRecentPaths(5);
    EXPECT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0], "C:\\FolderA");
    EXPECT_EQ(recent[1], "C:\\FolderB");

    // 收藏夹
    mgr.setFavorites({"C:\\repo", "D:\\Workspace"});
    auto favs = mgr.getFavorites();
    EXPECT_EQ(favs.size(), 2u);
    mgr.addFavorite("E:\\Projects");
    EXPECT_EQ(mgr.getFavorites().size(), 3u);
    mgr.removeFavorite("D:\\Workspace");
    EXPECT_EQ(mgr.getFavorites().size(), 2u);

    // 黑名单
    mgr.setBlacklist({"game.exe", "industrial.exe"});
    EXPECT_TRUE(mgr.isProcessBlacklisted("game.exe"));
    EXPECT_TRUE(mgr.isProcessBlacklisted("GAME.EXE"));
    EXPECT_FALSE(mgr.isProcessBlacklisted("code.exe"));
}


TEST(PathMemoryManagerTest, SelectionPathNormalization) {
    auto base = std::filesystem::temp_directory_path() /
                ("easytools-dialog-memory-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    ASSERT_TRUE(std::filesystem::create_directories(base / "ChosenFolder", ec));

    const auto selectedFile = base / "photo.psd";
    {
        std::ofstream file(selectedFile, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.good());
    }

    auto asPath = [](const std::string& utf8) {
        return std::filesystem::path(easy::core::WinUtils::utf8ToWstring(utf8)).lexically_normal();
    };
    auto asUtf8 = [](const std::filesystem::path& path) {
        return easy::core::WinUtils::wstringToUtf8(path.lexically_normal().native());
    };

    // 没有当前浏览目录上下文时，文件夹本身是唯一可靠结果。
    EXPECT_EQ(asPath(easy::dialog::PathMemoryManager::directoryForSelection(
                  asUtf8(base / "ChosenFolder"))),
              (base / "ChosenFolder").lexically_normal());

    // 文件夹选择器中高亮当前目录的子文件夹时，记忆它所在的当前目录，
    // 下次仍停留在同一选择层级，而不是钻入上次选择的子文件夹。
    EXPECT_EQ(asPath(easy::dialog::PathMemoryManager::directoryForSelection(
                  asUtf8(base / "ChosenFolder"), asUtf8(base))),
              base.lexically_normal());

    // 选择已有文件、保存尚不存在的文件：均记忆所在目录。
    EXPECT_EQ(asPath(easy::dialog::PathMemoryManager::directoryForSelection(
                  asUtf8(selectedFile))), base.lexically_normal());
    EXPECT_EQ(asPath(easy::dialog::PathMemoryManager::directoryForSelection(
                  asUtf8(base / "future-save.png"))), base.lexically_normal());
    EXPECT_EQ(asPath(easy::dialog::PathMemoryManager::directoryForSelection(
                  "relative-save.txt", asUtf8(base))), base.lexically_normal());

    // Windows 多选文件框会把底部输入显示为多个带引号的文件名。该文本不是
    // 单一路径；确认时应可靠回落到当前浏览目录，不能沿用更旧的 EXE 记忆。
    EXPECT_EQ(asPath(easy::dialog::PathMemoryManager::directoryForSelection(
                  "\"first.txt\" \"second.txt\"", asUtf8(base))),
              base.lexically_normal());

    std::filesystem::remove_all(base, ec);
}

TEST(ExplorerTrackerTest, PathNormalization) {
    EXPECT_EQ(easy::dialog::ExplorerTracker::normalizeFolderPath(L""), "");
    EXPECT_EQ(easy::dialog::ExplorerTracker::normalizeFolderPath(L"C:\\Windows"), "C:\\Windows");
    
    // URL 格式路径还原
    EXPECT_FALSE(easy::dialog::ExplorerTracker::normalizeFolderPath(L"file:///C:/Windows").empty());
}

TEST(DialogNavigatorTest, NullSafetyAndValidation) {
    EXPECT_FALSE(easy::dialog::DialogNavigator::isFileDialog(nullptr));
    EXPECT_EQ(easy::dialog::DialogNavigator::findPathEditControl(nullptr), nullptr);
    EXPECT_EQ(easy::dialog::DialogNavigator::findAddressBandControl(nullptr), nullptr);
    EXPECT_EQ(easy::dialog::DialogNavigator::findShellViewControl(nullptr), nullptr);
    EXPECT_EQ(easy::dialog::DialogNavigator::getCurrentDialogFolder(nullptr), "");
    EXPECT_FALSE(easy::dialog::DialogNavigator::instance().navigateToFolder(nullptr, ""));
    EXPECT_FALSE(easy::dialog::DialogNavigator::instance().navigateToFolder(nullptr, "C:\\NonExistentPath_12345"));
}

TEST(DialogNavigatorTest, NonFileDialogExclusion) {
    // 验证自身创建的普通窗口绝不会被误判为文件对话框
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"TestDialog", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    if (hwnd) {
        EXPECT_FALSE(easy::dialog::DialogNavigator::isFileDialog(hwnd));
        DestroyWindow(hwnd);
    }
}

// -----------------------------------------------------------------------------
// 56. 选区圆角手柄交互与实时调节测试套件
// -----------------------------------------------------------------------------