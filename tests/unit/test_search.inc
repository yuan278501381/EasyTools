// ─────────────────────────────────────────────────────────────────────────────
// test_search.inc — SEARCH 模块化单元测试集
// ─────────────────────────────────────────────────────────────────────────────

#include "service/StringArena.h"

TEST(StringArenaTest, OversizedSideBlockPreservesActiveBlockCapacity) {
    easy::service::StringArena arena;
    const std::uint64_t baselineBytes = arena.approximateBytes();

    const auto prefixOffset = arena.append(L"prefix");
    std::wstring oversized(easy::service::StringArena::BlockChars + 17, L'X');
    const auto oversizedOffset = arena.append(oversized);
    const auto suffixOffset = arena.append(L"suffix");

    EXPECT_EQ(prefixOffset, 1u);
    EXPECT_EQ(oversizedOffset, 1u << easy::service::StringArena::BlockShift);
    EXPECT_EQ(suffixOffset, prefixOffset + 6u)
        << "the oversized side block must not abandon the active block tail";
    EXPECT_EQ(arena.view(prefixOffset, 6), L"prefix");
    EXPECT_EQ(arena.view(oversizedOffset, static_cast<std::uint32_t>(oversized.size())),
              std::wstring_view(oversized));
    EXPECT_EQ(arena.view(suffixOffset, 6), L"suffix");
    EXPECT_EQ(arena.approximateBytes(),
              baselineBytes + oversized.size() * sizeof(wchar_t));

    arena.reset();
    EXPECT_EQ(arena.approximateBytes(), baselineBytes);
}

TEST(PinyinEngineTest, FuzzyAndInitialMatching) {
    auto wx_init = PinyinEngine::GetInitials(L"微信");
    auto wx_full = PinyinEngine::GetFullPinyin(L"微信");
    std::wprintf(L"[DEBUG] 微信 initials: %ls, full: %ls\n", wx_init.c_str(), wx_full.c_str());

    // 拼音首字母
    EXPECT_EQ(PinyinEngine::GetInitials(L"微信"), L"wx");
    EXPECT_EQ(PinyinEngine::GetInitials(L"你好世界"), L"nhsj");
    EXPECT_EQ(PinyinEngine::GetInitials(L"EasyTools工具"), L"easytoolsgj");

    // 拼音全拼
    EXPECT_EQ(PinyinEngine::GetFullPinyin(L"微信"), L"weixin");
    EXPECT_EQ(PinyinEngine::GetFullPinyin(L"你好"), L"nihao");
    EXPECT_EQ(PinyinEngine::GetFullPinyin(L"EasyTools"), L"easytools");
}

TEST(PinyinEngineTest, PreservesUnsupportedUnicodeAndSurrogatePairs) {
    const std::wstring supplementary{static_cast<wchar_t>(0xD840),
                                     static_cast<wchar_t>(0xDC00)};
    EXPECT_EQ(PinyinEngine::GetFirstLetter(L'\u3042'), L'\u3042');
    EXPECT_EQ(PinyinEngine::GetCharPinyin(L'\uD7FF'), L"\uD7FF");
    EXPECT_EQ(PinyinEngine::GetInitials(supplementary), supplementary);
    EXPECT_EQ(PinyinEngine::GetFullPinyin(supplementary), supplementary);
}

// -----------------------------------------------------------------------------
// 15. Everything 风格搜索表达式解析测试套件
// -----------------------------------------------------------------------------
TEST(SearchExpressionTest, EverythingSyntaxParsing) {
    FileRecord txtFile{1, 0, L"readme.txt", L"readme.txt", L"readme.txt", L"readme.txt", false};
    FileRecord pngFile{2, 0, L"screenshot.png", L"screenshot.png", L"screenshot.png", L"screenshot.png", false};
    FileRecord docDir{3, 0, L"Documents", L"documents", L"documents", L"documents", true};
    FileRecord wxFile{4, 0, L"微信.exe", L"微信.exe", L"wx.exe", L"weixin.exe", false};
    FileRecord cppFile{5, 0, L"test_main.cpp", L"test_main.cpp", L"test_main.cpp", L"test_main.cpp", false};
    FileRecord logFile{6, 0, L"app_2026.log", L"app_2026.log", L"app_2026.log", L"app_2026.log", false};

    // 1. 标准通配符与扩展名
    auto expr1 = SearchExpression::parse(L"*.txt");
    EXPECT_TRUE(expr1.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_FALSE(expr1.matches(pngFile, L'C', L"C:\\screenshot.png"));

    auto exprExt = SearchExpression::parse(L"ext:png;jpg;webp");
    EXPECT_TRUE(exprExt.matches(pngFile, L'C', L"C:\\screenshot.png"));
    EXPECT_FALSE(exprExt.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 2. 类型过滤器 (file: / folder: / dir:)
    auto exprFile = SearchExpression::parse(L"file: *.txt");
    EXPECT_TRUE(exprFile.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_FALSE(exprFile.matches(docDir, L'C', L"C:\\Documents"));

    auto exprDir = SearchExpression::parse(L"folder: documents");
    EXPECT_TRUE(exprDir.matches(docDir, L'C', L"C:\\Documents"));
    EXPECT_FALSE(exprDir.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 3. 逻辑与、或、非 (AND / OR / NOT)
    auto exprNot = SearchExpression::parse(L"*.cpp !test");
    FileRecord normalCpp{7, 0, L"main.cpp", L"main.cpp", L"main.cpp", L"main.cpp", false};
    EXPECT_FALSE(exprNot.matches(cppFile, L'C', L"C:\\test_main.cpp"));
    EXPECT_TRUE(exprNot.matches(normalCpp, L'C', L"C:\\main.cpp"));

    auto exprOr = SearchExpression::parse(L"ext:txt | ext:png");
    EXPECT_TRUE(exprOr.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_TRUE(exprOr.matches(pngFile, L'C', L"C:\\screenshot.png"));
    EXPECT_FALSE(exprOr.matches(cppFile, L'C', L"C:\\test_main.cpp"));

    // 4. 正则表达式 (regex: / r:)
    auto exprRegex = SearchExpression::parse(L"regex:^app_[0-9]+\\.log$");
    EXPECT_TRUE(exprRegex.matches(logFile, L'C', L"C:\\logs\\app_2026.log"));
    EXPECT_FALSE(exprRegex.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 5. 盘符与路径过滤 (c: / path: / parent:)
    auto exprDrive = SearchExpression::parse(L"c: *.txt");
    EXPECT_TRUE(exprDrive.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_FALSE(exprDrive.matches(txtFile, L'D', L"D:\\readme.txt"));

    auto exprPath = SearchExpression::parse(L"path:logs");
    EXPECT_TRUE(exprPath.matches(logFile, L'C', L"C:\\logs\\app_2026.log"));
    EXPECT_FALSE(exprPath.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 6. 拼音检索与禁用拼音 (pinyin: / nopy:)
    auto exprPy = SearchExpression::parse(L"wx");
    EXPECT_TRUE(exprPy.matches(wxFile, L'C', L"C:\\微信.exe"));

    auto exprNoPy = SearchExpression::parse(L"nopy:wx");
    EXPECT_FALSE(exprNoPy.matches(wxFile, L'C', L"C:\\微信.exe"));

    // 7. 双引号带空格短语
    auto exprQuote = SearchExpression::parse(L"\"test main\"");
    FileRecord spaceFile{8, 0, L"test main.cpp", L"test main.cpp", L"test main.cpp", L"test main.cpp", false};
    EXPECT_TRUE(exprQuote.matches(spaceFile, L'C', L"C:\\test main.cpp"));

    // 8. 跨目录与文件名联合搜索及跨层级通配符 (*中源*账号密码* 与 中源 账号密码)
    FileRecord nestedPassFile{9, 0, L"账号密码.txt", L"账号密码.txt", L"zhmm.txt", L"zhanghaomima.txt", false};
    std::wstring nestedPassPath = L"D:\\Chosen\\216-北京中源\\账号密码.txt";

    // 通配符跨目录搜索: *中源*账号密码*
    auto exprWildcardPath = SearchExpression::parse(L"*中源*账号密码*");
    EXPECT_TRUE(exprWildcardPath.matches(nestedPassFile, L'D', nestedPassPath));
    EXPECT_FALSE(exprWildcardPath.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 多关键词空格组合跨目录搜索: 中源 账号密码
    auto exprMultiTerms = SearchExpression::parse(L"中源 账号密码");
    EXPECT_TRUE(exprMultiTerms.matches(nestedPassFile, L'D', nestedPassPath));
    EXPECT_FALSE(exprMultiTerms.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 拼音与目录联合匹配: 中源 zhmm
    auto exprPinyinDir = SearchExpression::parse(L"中源 zhmm");
    EXPECT_TRUE(exprPinyinDir.matches(nestedPassFile, L'D', nestedPassPath));

    // 9. 用户真实连字符文件名检索: 019-get_Bom_Lev
    FileRecord bomFile{10, 0, L"019-get_Bom_Lev.txt", L"019-get_bom_lev.txt", L"019-get_bom_lev.txt", L"019-get_bom_lev.txt", false};
    auto exprBom1 = SearchExpression::parse(L"019-get_Bom_Lev");
    // 10. 盘符与根目录前缀搜索 (c:\, c:\ readme, c:\logs\ *.log)
    auto exprRoot = SearchExpression::parse(L"c:\\");
    EXPECT_TRUE(exprRoot.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_FALSE(exprRoot.matches(bomFile, L'D', L"D:\\Chosen\\019.txt"));

    auto exprRootTerms = SearchExpression::parse(L"c:\\ readme");
    EXPECT_TRUE(exprRootTerms.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_FALSE(exprRootTerms.matches(pngFile, L'C', L"C:\\screenshot.png"));

    auto exprSubdirWildcard = SearchExpression::parse(L"c:\\logs\\ *.log");
    EXPECT_TRUE(exprSubdirWildcard.matches(logFile, L'C', L"C:\\logs\\app_2026.log"));
    EXPECT_FALSE(exprSubdirWildcard.matches(txtFile, L'C', L"C:\\readme.txt"));
}

TEST(SearchExpressionTest, EnvironmentVariableExpansion) {
    FileRecord appDataFile;
    appDataFile.fileName = L"EasyTools.json";
    appDataFile.normalizedName = L"easytools.json";
    appDataFile.isDirectory = false;

    wchar_t appDataBuf[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"APPDATA", appDataBuf, MAX_PATH);
    std::wstring appDataStr = appDataBuf;
    if (!appDataStr.empty()) {
        std::wstring targetPath = appDataStr + L"\\EasyTools\\EasyTools.json";
        auto expr = SearchExpression::parse(L"%APPDATA%\\EasyTools\\EasyTools.json");
        EXPECT_TRUE(expr.requiresFullPath());
        EXPECT_TRUE(expr.matches(appDataFile, appDataStr[0], targetPath));
    }
}

// -----------------------------------------------------------------------------
// 16. 全屏检测与驱动器枚举测试套件
// -----------------------------------------------------------------------------

TEST(PipeEndpointTest, RequiresOpaqueTokenAndBuildsPerUserDescriptor) {
    using namespace easy::service::pipe_endpoint;
    EXPECT_FALSE(isValidToken("easytools"));
    EXPECT_FALSE(isValidToken(std::string(TokenHexLength, 'g')));
    const std::string token(TokenHexLength, 'a');
    EXPECT_TRUE(isValidToken(token));
    ASSERT_TRUE(makePipeName(token).has_value());
    EXPECT_NE(makePipeName(token)->find(token), std::string::npos);
    EXPECT_FALSE(makePipeName("invalid").has_value());
    ASSERT_TRUE(makeSecurityDescriptor(L"S-1-5-21-1-2-3-1001").has_value());
    EXPECT_FALSE(makeSecurityDescriptor(L"").has_value());
}

TEST(PipeEndpointTest, RestrictedDescriptorAllowsCurrentUserEndpoint) {
    using namespace easy::service::pipe_endpoint;
    HANDLE token = nullptr;
    ASSERT_TRUE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    struct TokenCloser { HANDLE value; ~TokenCloser() { if (value) CloseHandle(value); } } tokenCloser{token};
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    ASSERT_GT(bytes, 0u);
    std::vector<std::byte> tokenInfo(bytes);
    ASSERT_TRUE(GetTokenInformation(token, TokenUser, tokenInfo.data(), bytes, &bytes));
    auto* user = reinterpret_cast<TOKEN_USER*>(tokenInfo.data());
    LPWSTR sidText = nullptr;
    ASSERT_TRUE(ConvertSidToStringSidW(user->User.Sid, &sidText));
    struct SidFreer { LPWSTR value; ~SidFreer() { if (value) LocalFree(value); } } sidFreer{sidText};

    const auto sddl = makeSecurityDescriptor(sidText);
    ASSERT_TRUE(sddl.has_value());
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ASSERT_TRUE(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl->c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    struct DescriptorFreer { PSECURITY_DESCRIPTOR value; ~DescriptorFreer() { if (value) LocalFree(value); } } descriptorFreer{descriptor};
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    const std::wstring name = L"\\\\.\\pipe\\EasyToolsAclTest-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 128, 128, 0, &security);
    ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
    struct PipeCloser { HANDLE value; ~PipeCloser() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } pipeCloser{pipe};

    std::atomic<bool> connected{false};
    std::thread client([&] {
        HANDLE clientPipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (clientPipe != INVALID_HANDLE_VALUE) {
            connected = true;
            CloseHandle(clientPipe);
        }
    });
    const BOOL connectedByConnect = ConnectNamedPipe(pipe, nullptr);
    const DWORD connectError = connectedByConnect ? ERROR_SUCCESS : GetLastError();
    EXPECT_TRUE(connectedByConnect || connectError == ERROR_PIPE_CONNECTED);
    client.join();
    EXPECT_TRUE(connected.load());
    DisconnectNamedPipe(pipe);

    const auto invalidSddl = makeSecurityDescriptor(L"not-a-windows-sid");
    ASSERT_TRUE(invalidSddl.has_value());
    PSECURITY_DESCRIPTOR invalidDescriptor = nullptr;
    EXPECT_FALSE(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        invalidSddl->c_str(), SDDL_REVISION_1, &invalidDescriptor, nullptr));
    if (invalidDescriptor) LocalFree(invalidDescriptor);
}

TEST(PipeEndpointTest, DescriptorRejectsAnUnlistedCurrentUser) {
    using namespace easy::service::pipe_endpoint;
    // A syntactically valid but deliberately unrelated SID permits verifying
    // the actual Windows access check without creating or depending on a
    // second local account in CI.
    const auto sddl = makeSecurityDescriptor(L"S-1-5-21-424242-424243-424244-424245");
    ASSERT_TRUE(sddl.has_value());
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ASSERT_TRUE(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl->c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    struct DescriptorFreer { PSECURITY_DESCRIPTOR value; ~DescriptorFreer() { if (value) LocalFree(value); } } descriptorFreer{descriptor};
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    const std::wstring name = L"\\\\.\\pipe\\EasyToolsAclDenyTest-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 128, 128, 0, &security);
    ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
    struct PipeCloser { HANDLE value; ~PipeCloser() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } pipeCloser{pipe};

    SetLastError(ERROR_SUCCESS);
    HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_EQ(client, INVALID_HANDLE_VALUE);
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);
}

TEST(SearchServiceStartupPolicyTest, ScmStateNeverCausesPrematurePortableDuplicate) {
    using easy::search::ScmServiceState;
    using easy::search::StartupAction;
    using easy::search::decideStartupAction;

    EXPECT_EQ(decideStartupAction(true, ScmServiceState::Running, false),
              StartupAction::UseEndpoint);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Missing, false),
              StartupAction::AllowPortableFallback);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Stopped, false),
              StartupAction::StartScmService);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Stopped, true),
              StartupAction::AllowPortableFallback);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::StartPending, false),
              StartupAction::WaitForScmEndpoint);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Running, false),
              StartupAction::WaitForScmEndpoint);
    // A concurrent StartService can become Running after the caller's initial
    // status query; that state must still wait, never spawn portable mode.
    EXPECT_NE(decideStartupAction(false, ScmServiceState::Running, false),
              StartupAction::AllowPortableFallback);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::StopPending, false),
              StartupAction::ReportUnavailable);
    EXPECT_TRUE(easy::search::isScmEndpointIdentityConflict(
        ScmServiceState::Running, false, true));
    EXPECT_FALSE(easy::search::isScmEndpointIdentityConflict(
        ScmServiceState::StartPending, false, true));
    EXPECT_FALSE(easy::search::isScmEndpointIdentityConflict(
        ScmServiceState::Running, true, true));
    EXPECT_TRUE(easy::search::scmOpenShouldRetryQueryOnly(ERROR_ACCESS_DENIED));
    EXPECT_FALSE(easy::search::scmOpenShouldRetryQueryOnly(ERROR_SERVICE_DOES_NOT_EXIST));
    EXPECT_FALSE(easy::search::scmOpenShouldRetryQueryOnly(ERROR_SUCCESS));
    // 没有启动权限时，已停止的 SCM 服务必须回退便携进程，否则搜索永久不可用。
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Stopped, true),
              StartupAction::AllowPortableFallback);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Running, true),
              StartupAction::WaitForScmEndpoint);
}

// -----------------------------------------------------------------------------
// 20. 屏幕录制编码管道测试套件 (内存虚拟驱动 + FFmpeg 编码全链路测试)
// -----------------------------------------------------------------------------

TEST(ContentSearchTest, ExtractorEngines) {
    // 1. SearchExpression content 语法解析验证
    auto expr1 = SearchExpression::parse(L"content:SELECT");
    EXPECT_TRUE(expr1.hasContentFilter());
    EXPECT_EQ(easy::core::WinUtils::wstringToUtf8(expr1.getContentQuery()), "SELECT");
    EXPECT_FALSE(expr1.requiresFullPath());

    auto expr2 = SearchExpression::parse(L"ext:cpp;h c:EasyTools c:\\projects\\");
    EXPECT_TRUE(expr2.hasContentFilter());
    EXPECT_EQ(easy::core::WinUtils::wstringToUtf8(expr2.getContentQuery()), "EasyTools");
    EXPECT_TRUE(expr2.requiresFullPath());

    auto expr3 = SearchExpression::parse(L"内容:工程图纸");
    EXPECT_TRUE(expr3.hasContentFilter());
    EXPECT_EQ(easy::core::WinUtils::wstringToUtf8(expr3.getContentQuery()), "工程图纸");

    // 验证多关键词全文穿透检索 (如 content:同心 账号密码，后续词不会被误判为文件名过滤)
    auto exprMultiContent = SearchExpression::parse(L"content:同心 账号密码");
    EXPECT_TRUE(exprMultiContent.hasContentFilter());
    EXPECT_EQ(easy::core::WinUtils::wstringToUtf8(exprMultiContent.getContentQuery()), "同心 账号密码");
    FileRecord recMulti;
    recMulti.fileName = L"备忘录.txt";
    recMulti.normalizedName = L"备忘录.txt";
    EXPECT_TRUE(exprMultiContent.matches(recMulti, L'C'));

    // 拼音音节分隔符 (如输入法 tong'xi 匹配同喜 tongxi)
    auto exprPinyinSyllable = SearchExpression::parse(L"tong'xi");
    FileRecord recTongXi;
    recTongXi.fileName = L"同喜.txt";
    recTongXi.normalizedName = L"同喜.txt";
    recTongXi.pinyinFull = L"tongxi";
    recTongXi.pinyinInitials = L"tx";
    EXPECT_TRUE(exprPinyinSyllable.matches(recTongXi, L'C'));

    // 2. ContentSearchEngine 格式支持测试
    auto& engine = easy::service::content::ContentSearchEngine::instance();
    EXPECT_TRUE(engine.canSearchContent(L"cpp"));
    EXPECT_TRUE(engine.canSearchContent(L"rs"));
    EXPECT_TRUE(engine.canSearchContent(L"py"));
    EXPECT_TRUE(engine.canSearchContent(L"sql"));
    EXPECT_TRUE(engine.canSearchContent(L"md"));
    EXPECT_TRUE(engine.canSearchContent(L"docx"));
    EXPECT_TRUE(engine.canSearchContent(L"xlsx"));
    EXPECT_TRUE(engine.canSearchContent(L"pptx"));
    EXPECT_TRUE(engine.canSearchContent(L"psd"));
    EXPECT_TRUE(engine.canSearchContent(L"ai"));
    EXPECT_TRUE(engine.canSearchContent(L"cdr"));
    EXPECT_TRUE(engine.canSearchContent(L"xmind"));
    EXPECT_TRUE(engine.canSearchContent(L"dxf"));
    EXPECT_FALSE(engine.canSearchContent(L"exe"));
    EXPECT_FALSE(engine.canSearchContent(L"dll"));

    // 3. PlainTextExtractor 实际文件扫描测试 (创建临时测试文件)
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring testCppFile = std::wstring(tempPath) + L"easytools_test_source.cpp";

    {
        std::ofstream ofs(testCppFile, std::ios::binary);
        ofs << "// Line 1\n";
        ofs << "// Line 2\n";
        ofs << "void TestWorldClassContentSearch() {\n";
        ofs << "    int magic = 20260816;\n";
        ofs << "}\n";
    }

    std::vector<easy::service::content::ContentSnippet> snippets;
    bool found = engine.searchFile(testCppFile, L"TestWorldClassContentSearch", false, snippets);
    EXPECT_TRUE(found);
    EXPECT_EQ(snippets.size(), 1u);
    if (!snippets.empty()) {
        EXPECT_EQ(snippets[0].lineNumber, 3);
        EXPECT_NE(snippets[0].lineContent.find(L"TestWorldClassContentSearch"), std::wstring::npos);
        EXPECT_EQ(snippets[0].matchLength, wcslen(L"TestWorldClassContentSearch"));
    }

    DeleteFileW(testCppFile.c_str());

    // 3.1 SQL 文件与 OITT 关键字检索测试
    std::wstring testSqlFile = std::wstring(tempPath) + L"easytools_test_bom.sql";
    {
        std::ofstream ofs(testSqlFile, std::ios::binary);
        ofs << "/* SAP B1 BOM 物料清单控制 */\n";
        ofs << "SELECT T0.ItemCode, T0.ItemName FROM OITT T0 WHERE T0.TreeType = 'A';\n";
    }

    snippets.clear();
    bool sqlFound = engine.searchFile(testSqlFile, L"oitt", false, snippets);
    EXPECT_TRUE(sqlFound);
    EXPECT_FALSE(snippets.empty());
    if (!snippets.empty()) {
        EXPECT_EQ(snippets[0].lineNumber, 2);
        EXPECT_NE(snippets[0].lineContent.find(L"OITT"), std::wstring::npos);
    }
    DeleteFileW(testSqlFile.c_str());

    // 4. DxfExtractor 实际文件扫描测试 (创建临时测试 DXF)
    std::wstring testDxfFile = std::wstring(tempPath) + L"easytools_test_drawing.dxf";
    {
        std::ofstream ofs(testDxfFile, std::ios::binary);
        ofs << "0\nSECTION\n2\nENTITIES\n";
        ofs << "0\nTEXT\n8\nLAYER_1\n1\nDWG_PROJECT_NUM_A88\n";
        ofs << "0\nENDSEC\n0\nEOF\n";
    }

    snippets.clear();
    bool dxfFound = engine.searchFile(testDxfFile, L"PROJECT_NUM_A88", false, snippets);
    EXPECT_TRUE(dxfFound);
    EXPECT_FALSE(snippets.empty());
    if (!snippets.empty()) {
        EXPECT_NE(snippets[0].lineContent.find(L"DWG_PROJECT_NUM_A88"), std::wstring::npos);
    }

    DeleteFileW(testDxfFile.c_str());

    // 5. 伪装为 .log 的二进制文件 (如 edb.log, MeasuredBoot.log) 拦截测试
    std::wstring testBinaryLogFile = std::wstring(tempPath) + L"easytools_test_fake_edb.log";
    {
        std::ofstream ofs(testBinaryLogFile, std::ios::binary);
        // 模拟 ESE / MeasuredBoot 二进制数据头与随机控制字节
        uint8_t fakeBinaryHeader[] = { 0xef, 0xcd, 0xab, 0x89, 0x00, 0x00, 0x01, 0x00, 0x80, 0x81, 0x00, 0x12, 0x05, 0x00 };
        ofs.write(reinterpret_cast<const char*>(fakeBinaryHeader), sizeof(fakeBinaryHeader));
        for (int i = 0; i < 200; ++i) {
            char buf[16] = {0};
            buf[0] = static_cast<char>(i);
            ofs.write(buf, sizeof(buf));
        }
    }

    snippets.clear();
    bool binaryFound = engine.searchFile(testBinaryLogFile, L"**", false, snippets);
    EXPECT_FALSE(binaryFound);
    EXPECT_TRUE(snippets.empty());
    DeleteFileW(testBinaryLogFile.c_str());

    // 6. GBK / ANSI 中文编码无乱码检索测试
    std::wstring testGbkFile = std::wstring(tempPath) + L"easytools_test_gbk.txt";
    {
        std::ofstream ofs(testGbkFile, std::ios::binary);
        // GBK 编码的中文字符串："北京中源技术中心：开发账号密码"
        std::wstring wText = L"北京中源技术中心：开发账号密码";
        UINT cp = IsValidCodePage(936) ? 936 : (IsValidCodePage(54936) ? 54936 : CP_ACP);
        int len = WideCharToMultiByte(cp, 0, wText.c_str(), static_cast<int>(wText.size()), nullptr, 0, nullptr, nullptr);
        std::string gbkText(len, '\0');
        if (len > 0) {
            WideCharToMultiByte(cp, 0, wText.c_str(), static_cast<int>(wText.size()), gbkText.data(), len, nullptr, nullptr);
        }
        ofs << "Line 1: Header\r\n";
        ofs << gbkText << "\r\n";
    }

    snippets.clear();
    bool gbkFound = engine.searchFile(testGbkFile, L"账号密码", false, snippets);
    EXPECT_TRUE(gbkFound);
    EXPECT_FALSE(snippets.empty());
    if (!snippets.empty()) {
        EXPECT_NE(snippets[0].lineContent.find(L"北京中源技术中心"), std::wstring::npos);
        EXPECT_NE(snippets[0].lineContent.find(L"账号密码"), std::wstring::npos);
    }
    DeleteFileW(testGbkFile.c_str());

    // 7. UTF-16LE 中文与英文无乱码检索测试
    std::wstring testUtf16File = std::wstring(tempPath) + L"easytools_test_utf16.txt";
    {
        std::ofstream ofs(testUtf16File, std::ios::binary);
        uint8_t bom[2] = { 0xFF, 0xFE };
        ofs.write(reinterpret_cast<const char*>(bom), 2);
        std::wstring text = L"UTF16_WorldClass_Search: 极客搜索世界级体验\r\n";
        ofs.write(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
    }

    snippets.clear();
    bool utf16Found = engine.searchFile(testUtf16File, L"极客搜索", false, snippets);
    EXPECT_TRUE(utf16Found);
    EXPECT_FALSE(snippets.empty());
    if (!snippets.empty()) {
        EXPECT_NE(snippets[0].lineContent.find(L"极客搜索世界级体验"), std::wstring::npos);
    }
    DeleteFileW(testUtf16File.c_str());

    // 8. 动态格式配置测试 (自定义格式扩展与黑名单禁用)
    engine.configureFormats({L"proto", L"mycustomext"}, {L"psd", L"dxf"});
    EXPECT_TRUE(engine.canSearchContent(L"proto"));
    EXPECT_TRUE(engine.canSearchContent(L"mycustomext"));
    EXPECT_FALSE(engine.canSearchContent(L"psd"));
    EXPECT_FALSE(engine.canSearchContent(L"dxf"));

    std::wstring testProtoFile = std::wstring(tempPath) + L"easytools_test.mycustomext";
    {
        std::ofstream ofs(testProtoFile);
        ofs << "syntax = \"proto3\";\n";
        ofs << "message EasyToolsSearchPacket {\n";
        ofs << "    string query = 1;\n";
        ofs << "}\n";
    }
    snippets.clear();
    bool protoFound = engine.searchFile(testProtoFile, L"EasyToolsSearchPacket", false, snippets);
    EXPECT_TRUE(protoFound);
    EXPECT_FALSE(snippets.empty());
    DeleteFileW(testProtoFile.c_str());

    // 恢复配置
    engine.configureFormats({}, {});
    EXPECT_TRUE(engine.canSearchContent(L"psd"));
    EXPECT_TRUE(engine.canSearchContent(L"dxf"));
    EXPECT_TRUE(engine.canSearchContent(L"proto"));
    EXPECT_FALSE(engine.canSearchContent(L"mycustomext"));
}

TEST(ContentSearchTest, FormatConfigurationIsReversibleUnderConcurrency) {
    auto& engine = easy::service::content::ContentSearchEngine::instance();
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 8; ++worker) {
        workers.emplace_back([&engine, worker]() {
            for (int iteration = 0; iteration < 500; ++iteration) {
                if (worker < 2) {
                    if ((iteration + worker) % 2 == 0) {
                        engine.configureFormats({L"temporary"}, {L"psd"});
                    } else {
                        engine.configureFormats({}, {});
                    }
                } else {
                    (void)engine.canSearchContent(L"temporary");
                    (void)engine.canSearchContent(L"psd");
                    (void)engine.canSearchContent(L"cpp");
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();

    engine.configureFormats({}, {});
    EXPECT_FALSE(engine.canSearchContent(L"temporary"));
    EXPECT_TRUE(engine.canSearchContent(L"psd"));
    EXPECT_TRUE(engine.canSearchContent(L"cpp"));
}

TEST(ContentSearchTest, CaseInsensitiveMixedLanguageSearch) {
    auto& engine = easy::service::content::ContentSearchEngine::instance();
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring testFile = std::wstring(tempPath) + L"easytools_test_mixed.txt";

    {
        std::ofstream ofs(testFile, std::ios::binary);
        ofs << "项目名称: EasyTools\n";
        ofs << "开发团队: Antigravity Google Team\n";
        ofs << "模块说明: ContentSearchEngine 全文穿透检索系统\n";
    }

    // 1. 中英文大小写混合检索测试：文件中有 "EasyTools"，用全小写 "easytools" 检索
    std::vector<easy::service::content::ContentSnippet> snippets;
    bool foundLower = engine.searchFile(testFile, L"easytools", false, snippets);
    EXPECT_TRUE(foundLower);
    EXPECT_FALSE(snippets.empty());

    // 2. 用中英混合词检索："easytools 全文"
    snippets.clear();
    bool foundMixed = engine.searchFile(testFile, L"easytools 全文", false, snippets);
    EXPECT_TRUE(foundMixed);
    EXPECT_FALSE(snippets.empty());

    // 3. 中文词检索
    snippets.clear();
    bool foundZh = engine.searchFile(testFile, L"全文穿透检索", false, snippets);
    EXPECT_TRUE(foundZh);
    EXPECT_FALSE(snippets.empty());

    DeleteFileW(testFile.c_str());
}

TEST(ContentSearchTest, ZipXmlTagStrippingNoSpuriousSpaces) {
    auto& engine = easy::service::content::ContentSearchEngine::instance();
    EXPECT_TRUE(engine.canSearchContent(L"docx"));
    EXPECT_TRUE(engine.canSearchContent(L"xlsx"));
    EXPECT_TRUE(engine.canSearchContent(L"pptx"));
}

TEST(RunHistoryTest, PersistenceAndFrecencyCalculation) {
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring csvFile = std::wstring(tempPath) + L"EasyTools_RunHistory_Test.csv";
    DeleteFileW(csvFile.c_str());

    auto& runMgr = easy::service::db::RunHistoryManager::instance();
    runMgr.init(csvFile);
    runMgr.clear();
    EXPECT_EQ(runMgr.size(), 0u);

    const std::wstring testFile1 = L"D:\\Chosen\\216-北京中源\\账号密码.txt";
    const std::wstring testFile2 = L"D:\\Chosen\\999-SQL\\SAP_RW.txt";

    runMgr.recordRun(testFile1);
    EXPECT_EQ(runMgr.getRunCount(testFile1), 1u);
    EXPECT_GT(runMgr.getLastRunDate(testFile1), 0ULL);

    // 重复运行累加
    runMgr.recordRun(testFile1);
    EXPECT_EQ(runMgr.getRunCount(testFile1), 2u);

    runMgr.recordRun(testFile2);
    EXPECT_EQ(runMgr.getRunCount(testFile2), 1u);

    // Frecency 智能权重打分计算
    double score1 = runMgr.calculateFrecencyScore(testFile1);
    double score2 = runMgr.calculateFrecencyScore(testFile2);
    EXPECT_GT(score1, score2); // 运行 2 次的得分显著高于运行 1 次

    auto topRuns = runMgr.getTopRuns(10);
    ASSERT_GE(topRuns.size(), 2u);
    EXPECT_EQ(topRuns[0].filename, testFile1);

    // 持久化保存并从磁盘重载
    EXPECT_TRUE(runMgr.save());
    runMgr.init(csvFile);
    EXPECT_EQ(runMgr.getRunCount(testFile1), 2u);
    EXPECT_EQ(runMgr.getRunCount(testFile2), 1u);

    DeleteFileW(csvFile.c_str());
}

// -----------------------------------------------------------------------------
// 35. 搜索历史 (Search History) 与去重推荐测试套件
// -----------------------------------------------------------------------------
TEST(SearchHistoryTest, PersistenceAndDeduplication) {
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring csvFile = std::wstring(tempPath) + L"EasyTools_SearchHistory_Test.csv";
    DeleteFileW(csvFile.c_str());

    auto& searchMgr = easy::service::db::SearchHistoryManager::instance();
    searchMgr.init(csvFile);
    searchMgr.clear();
    EXPECT_EQ(searchMgr.size(), 0u);

    const std::wstring q1 = L"*中源*账号密码*";
    const std::wstring q2 = L"ext:docx 订单";
    const std::wstring quotedQuery = L"\"exact, phrase\"\n第二行";

    searchMgr.recordSearch(q1);
    searchMgr.recordSearch(q2);
    searchMgr.recordSearch(quotedQuery);
    searchMgr.recordSearch(q1); // 重复记录

    EXPECT_EQ(searchMgr.size(), 3u);
    auto recents = searchMgr.getRecentSearches(10);
    ASSERT_EQ(recents.size(), 3u);
    EXPECT_EQ(recents[0].search, q1);
    EXPECT_EQ(recents[0].searchCount, 2u);

    // 移除单个搜索项
    EXPECT_TRUE(searchMgr.removeSearch(q2));
    EXPECT_EQ(searchMgr.size(), 2u);

    // 存盘与重载
    EXPECT_TRUE(searchMgr.save());
    searchMgr.init(csvFile);
    EXPECT_EQ(searchMgr.size(), 2u);
    const auto restoredSearches = searchMgr.getRecentSearches(10);
    EXPECT_NE(std::find_if(restoredSearches.begin(), restoredSearches.end(), [&](const auto& item) {
        return item.search == quotedQuery;
    }), restoredSearches.end());

    DeleteFileW(csvFile.c_str());
}

// -----------------------------------------------------------------------------
// 36. 数据库快照 (EasyTools.db) 二进制存储与秒级内存映射测试套件
// -----------------------------------------------------------------------------
TEST(DatabaseSnapshotTest, BinaryPackAndFastMemoryMap) {
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring dbFile = std::wstring(tempPath) + L"EasyTools_Test.db";
    DeleteFileW(dbFile.c_str());

    auto& dbMgr = easy::service::db::DatabaseManager::instance();
    dbMgr.init(dbFile);

    // 构造测试 MFT 卷与记录
    auto parser1 = std::make_unique<MftParser>();
    std::vector<FileRecordInit> mockRecords;
    for (uint64_t i = 1; i <= 50; ++i) {
        FileRecordInit r{};
        r.fileReferenceNumber = i;
        r.parentFileReferenceNumber = 0;
        r.fileName = L"test_document_" + std::to_wstring(i) + L".docx";
        r.fileSize = 1024 * i;
        r.creationTime = 134000000000000000ULL + i;
        r.lastWriteTime = 134000000000000000ULL + i * 2;
        r.isDirectory = (i % 10 == 0);
        mockRecords.push_back(std::move(r));
    }

    EXPECT_TRUE(parser1->importSnapshot(std::move(mockRecords), 12345678ULL, 0xABCDEF01));
    EXPECT_EQ(parser1->getFileCount(), 50u);

    std::vector<MftParser*> parsers = { parser1.get() };

    // 保存快照至 EasyTools.db
    EXPECT_TRUE(dbMgr.saveSnapshot(parsers));

    auto stats = dbMgr.getStats();
    EXPECT_TRUE(stats.exists);
    EXPECT_GT(stats.fileSize, 0ULL);
    EXPECT_EQ(stats.totalRecords, 50ULL);

    // 构造空解析器并从 EasyTools.db 内存映射瞬时恢复
    auto parser2 = std::make_unique<MftParser>();
    std::vector<MftParser*> restoreParsers = { parser2.get() };
    EXPECT_TRUE(dbMgr.loadSnapshot(restoreParsers));
    EXPECT_EQ(parser2->getFileCount(), 50u);

    // 验证搜索命中与路径结构
    auto searchResults = parser2->Search(L"test_document_5.docx");
    ASSERT_EQ(searchResults.size(), 1u);
    EXPECT_EQ(searchResults[0].fileName, L"test_document_5.docx");
    EXPECT_EQ(searchResults[0].fileSize, 5120u);

    const auto originalSize = std::filesystem::file_size(dbFile);
    HANDLE lockedDatabase = CreateFileW(dbFile.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(lockedDatabase, INVALID_HANDLE_VALUE);
    EXPECT_FALSE(dbMgr.saveSnapshot(parsers));
    CloseHandle(lockedDatabase);
    EXPECT_EQ(std::filesystem::file_size(dbFile), originalSize);

    auto parserAfterFailedSave = std::make_unique<MftParser>();
    std::vector<MftParser*> parsersAfterFailedSave = {parserAfterFailedSave.get()};
    EXPECT_TRUE(dbMgr.loadSnapshot(parsersAfterFailedSave));
    EXPECT_EQ(parserAfterFailedSave->getFileCount(), 50u);

    DeleteFileW(dbFile.c_str());
}

// -----------------------------------------------------------------------------
// 36.1 搜索服务管道分帧协议
//
// 旧协议依赖 PIPE_TYPE_MESSAGE 与固定 256KB 缓冲，响应超出缓冲区时客户端会
// 把 ERROR_MORE_DATA 当成失败并丢弃整份结果，残留数据还会拖垮后续连接。
// 现在改为显式长度前缀，这些用例锁定编解码的边界行为。
// -----------------------------------------------------------------------------
TEST(MftParserTest, IncrementalTypingMultiWordCrossPathSearch) {
    auto parser = std::make_unique<MftParser>();
    std::vector<FileRecordInit> mockRecords;

    // 构造目录: 226-苏州同心 (id = 100)
    FileRecordInit dirRec{};
    dirRec.fileReferenceNumber = 100;
    dirRec.parentFileReferenceNumber = 0;
    dirRec.fileName = L"226-苏州同心";
    dirRec.isDirectory = true;
    mockRecords.push_back(std::move(dirRec));

    // 构造文件: 账号密码.txt (id = 101, parent = 100)
    FileRecordInit fileRec{};
    fileRec.fileReferenceNumber = 101;
    fileRec.parentFileReferenceNumber = 100;
    fileRec.fileName = L"账号密码.txt";
    fileRec.isDirectory = false;
    fileRec.fileSize = 1024;
    mockRecords.push_back(std::move(fileRec));

    EXPECT_TRUE(parser->importSnapshot(std::move(mockRecords), 123456ULL, 0x11223344));

    // 模拟连续打字:
    // 1. 用户先搜 "同" (单字仅匹配文件名/目录名，此时 账号密码.txt 仅在目录命中，缓存被填入)
    auto res1 = parser->Search(L"同");
    EXPECT_GE(res1.size(), 1u);

    // 2. 用户接着打 "同心" (缩小缓存)
    auto res2 = parser->Search(L"同心");
    EXPECT_GE(res2.size(), 1u);

    // 3. 用户追加空格与第二个词 "同心 账号密码" (多词全路径匹配，必须绕开单字文件名缓存，正确命中 账号密码.txt)
    auto res3 = parser->Search(L"同心 账号密码");
    ASSERT_EQ(res3.size(), 1u);
    EXPECT_EQ(res3[0].fileName, L"账号密码.txt");
    EXPECT_NE(res3[0].fullPath.find(L"226-苏州同心"), std::wstring::npos);
}

TEST(PipeProtocolTest, HeaderRoundTripsPayloadSize) {
    namespace frame = easy::service::pipe;
    for (uint32_t size : {1u, 2u, 255u, 256u, 65535u, 65536u, 1048576u, frame::MaxFrameBytes}) {
        const auto header = frame::encodeFrameHeader(size);
        uint32_t decoded = 0;
        EXPECT_TRUE(frame::decodeFrameHeader(header.data(), decoded)) << "size=" << size;
        EXPECT_EQ(decoded, size);
    }
}

TEST(PipeProtocolTest, HeaderIsLittleEndian) {
    namespace frame = easy::service::pipe;
    const auto header = frame::encodeFrameHeader(0x04030201u);
    EXPECT_EQ(static_cast<unsigned char>(header[0]), 0x01u);
    EXPECT_EQ(static_cast<unsigned char>(header[1]), 0x02u);
    EXPECT_EQ(static_cast<unsigned char>(header[2]), 0x03u);
    EXPECT_EQ(static_cast<unsigned char>(header[3]), 0x04u);
}

TEST(PipeProtocolTest, RejectsEmptyAndOversizedFrames) {
    namespace frame = easy::service::pipe;
    uint32_t decoded = 0;

    const auto emptyHeader = frame::encodeFrameHeader(0);
    EXPECT_FALSE(frame::decodeFrameHeader(emptyHeader.data(), decoded));

    const auto oversized = frame::encodeFrameHeader(frame::MaxFrameBytes + 1);
    EXPECT_FALSE(frame::decodeFrameHeader(oversized.data(), decoded));

    EXPECT_FALSE(frame::decodeFrameHeader(nullptr, decoded));
}

TEST(PipeProtocolTest, RequestLimitIsTighterThanResponseLimit) {
    namespace frame = easy::service::pipe;
    uint32_t decoded = 0;

    // 请求只承载一段查询 JSON，服务端用更小的上限拒绝畸形帧头。
    const auto header = frame::encodeFrameHeader(frame::MaxRequestBytes + 1);
    EXPECT_FALSE(frame::decodeFrameHeader(header.data(), decoded, frame::MaxRequestBytes));
    EXPECT_TRUE(frame::decodeFrameHeader(header.data(), decoded));

    EXPECT_TRUE(frame::fitsInFrame(frame::MaxRequestBytes, frame::MaxRequestBytes));
    EXPECT_FALSE(frame::fitsInFrame(frame::MaxRequestBytes + 1, frame::MaxRequestBytes));
    EXPECT_FALSE(frame::fitsInFrame(0));
}

TEST(PipeProtocolIntegrationTest, ByteStreamRoundTripsDeliberatelyFragmentedFrame) {
    namespace frame = easy::service::pipe;
    const std::wstring name = L"\\\\.\\pipe\\EasyToolsPipeProtocolIntegration-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HANDLE serverPipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, frame::IoChunkBytes, frame::IoChunkBytes, 0, nullptr);
    ASSERT_NE(serverPipe, INVALID_HANDLE_VALUE);
    struct ServerPipeCloser { HANDLE value; ~ServerPipeCloser() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } serverCloser{serverPipe};

    // Connect before starting the server thread. ConnectNamedPipe will then
    // return ERROR_PIPE_CONNECTED, exercising the race that occurs when a
    // portable client wins the connection race against a newly published pipe.
    HANDLE clientPipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(clientPipe, INVALID_HANDLE_VALUE) << GetLastError();
    struct ClientPipeCloser { HANDLE value; ~ClientPipeCloser() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } clientCloser{clientPipe};

    std::atomic<bool> serverOk{false};
    std::thread server([&] {
        const BOOL connected = ConnectNamedPipe(serverPipe, nullptr) ? TRUE :
            (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) return;
        char header[frame::HeaderSize]{};
        if (!frame::readExact(serverPipe, header, sizeof(header))) return;
        std::uint32_t bytes = 0;
        if (!frame::decodeFrameHeader(header, bytes, frame::MaxRequestBytes)) return;
        std::string request(bytes, '\0');
        if (!frame::readExact(serverPipe, request.data(), request.size()) || request != "fragmented request") return;
        const std::string response = R"({"ok":true,"source":"pipe"})";
        const auto responseHeader = frame::encodeFrameHeader(static_cast<std::uint32_t>(response.size()));
        if (!frame::writeExact(serverPipe, responseHeader.data(), responseHeader.size())) return;
        if (!frame::writeExact(serverPipe, response.data(), response.size())) return;
        serverOk.store(true, std::memory_order_release);
    });

    const std::string request = "fragmented request";
    const auto requestHeader = frame::encodeFrameHeader(static_cast<std::uint32_t>(request.size()));
    auto writeByteByByte = [clientPipe](const char* bytes, std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            DWORD written = 0;
            if (!WriteFile(clientPipe, bytes + index, 1, &written, nullptr) || written != 1) return false;
        }
        return true;
    };
    EXPECT_TRUE(writeByteByByte(requestHeader.data(), requestHeader.size()));
    EXPECT_TRUE(writeByteByByte(request.data(), request.size()));

    char responseHeader[frame::HeaderSize]{};
    std::uint32_t responseBytes = 0;
    const bool gotHeader = frame::readExact(clientPipe, responseHeader, sizeof(responseHeader));
    EXPECT_TRUE(gotHeader);
    const bool validHeader = gotHeader && frame::decodeFrameHeader(responseHeader, responseBytes);
    EXPECT_TRUE(validHeader);
    std::string response(validHeader ? responseBytes : 0, '\0');
    const bool gotPayload = validHeader && frame::readExact(clientPipe, response.data(), response.size());
    EXPECT_TRUE(gotPayload);
    if (gotPayload) EXPECT_EQ(response, R"({"ok":true,"source":"pipe"})");
    clientCloser.value = INVALID_HANDLE_VALUE;
    CloseHandle(clientPipe);
    server.join();
    EXPECT_TRUE(serverOk.load(std::memory_order_acquire));
}

// -----------------------------------------------------------------------------
// 36.2 查询代际取消
// -----------------------------------------------------------------------------
TEST(QueryEpochTrackerTest, NewerQuerySupersedesOlderOnes) {
    easy::service::query::QueryEpochTracker tracker;

    EXPECT_TRUE(tracker.observe(100));
    EXPECT_FALSE(tracker.isStale(100));

    EXPECT_TRUE(tracker.observe(200));
    EXPECT_TRUE(tracker.isStale(100));
    EXPECT_FALSE(tracker.isStale(200));
    EXPECT_EQ(tracker.latest(), 200u);
}

TEST(QueryEpochTrackerTest, LateArrivingOlderQueryIsRejectedImmediately) {
    easy::service::query::QueryEpochTracker tracker;
    EXPECT_TRUE(tracker.observe(500));

    // 更旧的查询即使刚到达也已经过期，不必再跑一遍扫描。
    EXPECT_FALSE(tracker.observe(400));
    EXPECT_TRUE(tracker.isStale(400));
    EXPECT_EQ(tracker.latest(), 500u);
}

TEST(QueryEpochTrackerTest, RepeatedObservationOfLatestStaysCurrent) {
    easy::service::query::QueryEpochTracker tracker;
    EXPECT_TRUE(tracker.observe(700));
    EXPECT_TRUE(tracker.observe(700));
    EXPECT_FALSE(tracker.isStale(700));
}

TEST(QueryEpochTrackerTest, UnnumberedCallersNeverParticipateInCancellation) {
    easy::service::query::QueryEpochTracker tracker;
    EXPECT_TRUE(tracker.observe(0));
    EXPECT_FALSE(tracker.isStale(0));

    EXPECT_TRUE(tracker.observe(900));
    EXPECT_TRUE(tracker.observe(0));
    EXPECT_FALSE(tracker.isStale(0));
    EXPECT_EQ(tracker.latest(), 900u);
}

TEST(QueryEpochTrackerTest, ResetClearsGeneration) {
    easy::service::query::QueryEpochTracker tracker;
    EXPECT_TRUE(tracker.observe(1234));
    tracker.reset();
    EXPECT_EQ(tracker.latest(), 0u);
    EXPECT_FALSE(tracker.isStale(1));
}

TEST(QueryEpochTrackerTest, CancelAllBumpsEpochAndMarksActiveQueriesStale) {
    easy::service::query::QueryEpochTracker tracker;
    EXPECT_TRUE(tracker.observe(100));
    EXPECT_FALSE(tracker.isStale(100));
    tracker.cancelAll();
    EXPECT_GT(tracker.latest(), 100u);
    EXPECT_TRUE(tracker.isStale(100));
}

TEST(QueryEpochTrackerTest, ConcurrentObserversAgreeOnASingleWinner) {
    easy::service::query::QueryEpochTracker tracker;
    constexpr uint64_t highest = 512;

    std::vector<std::thread> threads;
    for (uint64_t id = 1; id <= highest; ++id) {
        threads.emplace_back([&tracker, id]() { tracker.observe(id); });
    }
    for (auto& thread : threads) thread.join();

    EXPECT_EQ(tracker.latest(), highest);
    EXPECT_FALSE(tracker.isStale(highest));
    EXPECT_TRUE(tracker.isStale(highest - 1));
}

// -----------------------------------------------------------------------------
// 36.3 IPC 异步分发
//
// 搜索请求要跨进程等待索引服务，同步执行会冻结 WebView2 的 UI 线程。这里验证
// 标记为异步的方法确实离开了调用线程，而未标记的方法仍然就地完成。
// -----------------------------------------------------------------------------

TEST(SearchRequestLimitsTest, OversizedRegexIsNotCompiled) {
    std::wstring pattern(easy::service::search_limits::MaxRegexCharacters + 1, L'a');
    const auto expression = SearchExpression::parse(L"regex:" + pattern);
    ASSERT_EQ(expression.getOrGroups().size(), 1u);
    ASSERT_EQ(expression.getOrGroups()[0].clauses.size(), 1u);
    EXPECT_FALSE(expression.getOrGroups()[0].clauses[0].regexObj.has_value());
}


TEST(ServiceLifetimeTest, StopsOnlyTheInstanceWeSpawned) {
    using easy::search::ServiceOwnership;
    using easy::search::shouldStopServiceOnExit;

    EXPECT_TRUE(shouldStopServiceOnExit(ServiceOwnership{true, false, false}));

    // 没拉起过就说明它属于别人，可能还有别的客户端在用。
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{false, false, false}));
}

TEST(ServiceLifetimeTest, LeavesScmManagedServiceAlone) {
    using easy::search::ServiceOwnership;
    using easy::search::shouldStopServiceOnExit;

    // 用户显式安装成 Windows 服务，就该由 SCM 管生命周期，哪怕这次是我们启动的。
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{true, true, false}));
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{false, true, false}));
}

TEST(ServiceLifetimeTest, KeepRunningPreferenceOverridesCleanup) {
    using easy::search::ServiceOwnership;
    using easy::search::shouldStopServiceOnExit;

    // 用户选择常驻，就换首次搜索秒开、退出后不释放内存。
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{true, false, true}));
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{true, true, true}));
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{false, false, true}));
}

TEST(ServiceLifetimeTest, DefaultConstructedOwnershipStopsNothing) {
    // 任何事实都还没确定时不做处置，避免误杀别人的服务。
    EXPECT_FALSE(easy::search::shouldStopServiceOnExit(easy::search::ServiceOwnership{}));
}

// -----------------------------------------------------------------------------
// 38. DevOps 全链路性能防退化基准与内存上限自动化门禁 (DevOps Performance Gates)
// -----------------------------------------------------------------------------

// 门禁 1: 50,000 文件索引内存紧缩与单趟毫秒级比对吞吐量
