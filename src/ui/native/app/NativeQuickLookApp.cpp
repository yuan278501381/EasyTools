#include "ui/native/app/NativeQuickLookApp.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include "core/utils/WinUtils.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <shellapi.h>
#include <unordered_set>

#pragma comment(lib, "windowscodecs.lib")

namespace tools3000::ui::native {

// 语法关键字集合
static bool isCodeKeyword(const std::wstring& word) {
    static const std::unordered_set<std::wstring> s_keywords = {
        L"auto", L"break", L"case", L"char", L"const", L"continue", L"default", L"do",
        L"double", L"else", L"enum", L"extern", L"float", L"for", L"goto", L"if",
        L"inline", L"int", L"long", L"register", L"restrict", L"return", L"short",
        L"signed", L"sizeof", L"static", L"struct", L"switch", L"typedef", L"union",
        L"unsigned", L"void", L"volatile", L"while", L"class", L"public", L"protected",
        L"private", L"virtual", L"override", L"final", L"new", L"delete", L"this",
        L"friend", L"operator", L"template", L"typename", L"namespace", L"using",
        L"try", L"catch", L"throw", L"constexpr", L"nullptr", L"true", L"false",
        L"bool", L"import", L"export", L"from", L"function", L"let", L"var", L"async",
        L"await", L"type", L"interface", L"as", L"def", L"elif", L"lambda", L"pass",
        L"yield", L"fn", L"mut", L"pub", L"impl", L"trait", L"self", L"select",
        L"where", L"insert", L"update", L"table", L"local"
    };
    return s_keywords.find(word) != s_keywords.end();
}

static void renderCodeLine(UIRenderContext& ctx, const std::wstring& lineText, float startX, float ly, const ThemePalette& p, bool isDark) {
    size_t firstNonSpace = lineText.find_first_not_of(L" \t");
    if (firstNonSpace != std::wstring::npos) {
        if (lineText.compare(firstNonSpace, 2, L"//") == 0 || lineText[firstNonSpace] == L';') {
            Color commentColor = isDark ? Color(0.45f, 0.70f, 0.48f, 1.0f) : Color(0.25f, 0.55f, 0.30f, 1.0f);
            ctx.drawText(lineText, Point(startX, ly), FontToken::Sm, FontWeight::Regular, commentColor, FontFamilyType::Mono);
            return;
        }
        if (lineText[firstNonSpace] == L'#' && lineText.find_first_of(L"abcdefghijklmnopqrstuvwxyz", firstNonSpace + 1) == firstNonSpace + 1) {
            Color prepColor = isDark ? Color(0.85f, 0.48f, 0.78f, 1.0f) : Color(0.70f, 0.30f, 0.65f, 1.0f);
            ctx.drawText(lineText, Point(startX, ly), FontToken::Sm, FontWeight::SemiBold, prepColor, FontFamilyType::Mono);
            return;
        }
    }

    float curX = startX;
    size_t i = 0;
    size_t len = lineText.size();

    while (i < len) {
        wchar_t ch = lineText[i];

        if (ch == L'"' || ch == L'\'') {
            wchar_t quote = ch;
            size_t start = i++;
            while (i < len && lineText[i] != quote) {
                if (lineText[i] == L'\\' && i + 1 < len) i += 2;
                else i++;
            }
            if (i < len) i++;
            std::wstring token = lineText.substr(start, i - start);
            Color strColor = isDark ? Color(0.92f, 0.65f, 0.45f, 1.0f) : Color(0.80f, 0.45f, 0.20f, 1.0f);
            ctx.drawText(token, Point(curX, ly), FontToken::Sm, FontWeight::Regular, strColor, FontFamilyType::Mono);
            curX += ctx.measureText(token, FontToken::Sm, FontWeight::Regular, FontFamilyType::Mono).width;
            continue;
        }

        if (iswdigit(ch)) {
            size_t start = i++;
            while (i < len && (iswalnum(lineText[i]) || lineText[i] == L'.' || lineText[i] == L'x' || lineText[i] == L'X')) {
                i++;
            }
            std::wstring token = lineText.substr(start, i - start);
            Color numColor = isDark ? Color(0.40f, 0.82f, 0.82f, 1.0f) : Color(0.20f, 0.65f, 0.65f, 1.0f);
            ctx.drawText(token, Point(curX, ly), FontToken::Sm, FontWeight::Regular, numColor, FontFamilyType::Mono);
            curX += ctx.measureText(token, FontToken::Sm, FontWeight::Regular, FontFamilyType::Mono).width;
            continue;
        }

        if (iswalpha(ch) || ch == L'_') {
            size_t start = i++;
            while (i < len && (iswalnum(lineText[i]) || lineText[i] == L'_')) {
                i++;
            }
            std::wstring token = lineText.substr(start, i - start);
            if (isCodeKeyword(token)) {
                Color kwColor = isDark ? Color(0.38f, 0.68f, 0.98f, 1.0f) : Color(0.18f, 0.48f, 0.88f, 1.0f);
                ctx.drawText(token, Point(curX, ly), FontToken::Sm, FontWeight::SemiBold, kwColor, FontFamilyType::Mono);
            } else {
                ctx.drawText(token, Point(curX, ly), FontToken::Sm, FontWeight::Regular, p.text, FontFamilyType::Mono);
            }
            curX += ctx.measureText(token, FontToken::Sm, FontWeight::Regular, FontFamilyType::Mono).width;
            continue;
        }

        size_t start = i++;
        while (i < len && !iswalpha(lineText[i]) && !iswdigit(lineText[i]) && lineText[i] != L'_' && lineText[i] != L'"' && lineText[i] != L'\'') {
            i++;
        }
        std::wstring token = lineText.substr(start, i - start);
        ctx.drawText(token, Point(curX, ly), FontToken::Sm, FontWeight::Regular, p.textSecondary, FontFamilyType::Mono);
        curX += ctx.measureText(token, FontToken::Sm, FontWeight::Regular, FontFamilyType::Mono).width;
    }
}

static void renderMarkdownView(UIRenderContext& ctx, const std::vector<std::wstring>& lines, const Rect& body, float scrollOffset, const ThemePalette& p, bool isDark) {
    float curY = body.top + 14.0f - scrollOffset;
    float leftX = body.left + 24.0f;
    float contentW = body.width() - 48.0f;
    bool inCodeFence = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];

        if (line.rfind(L"```", 0) == 0) {
            inCodeFence = !inCodeFence;
            curY += 6.0f;
            continue;
        }

        if (inCodeFence) {
            if (curY >= body.top - 24.0f && curY <= body.bottom + 24.0f) {
                Rect fenceBg(leftX, curY - 2.0f, leftX + contentW, curY + 20.0f);
                Color codeBg = isDark ? Color(0.12f, 0.13f, 0.17f, 0.90f) : Color(0.93f, 0.94f, 0.96f, 0.90f);
                ctx.fillRoundedRect(fenceBg, 3.0f, codeBg);
                ctx.drawText(line, Point(leftX + 8.0f, curY), FontToken::Sm, FontWeight::Regular, Color(0.85f, 0.50f, 0.35f, 1.0f), FontFamilyType::Mono);
            }
            curY += 22.0f;
            continue;
        }

        if (line.rfind(L"# ", 0) == 0) {
            if (curY >= body.top - 30.0f && curY <= body.bottom + 30.0f) {
                std::wstring text = line.substr(2);
                ctx.drawText(text, Point(leftX, curY), FontToken::Lg, FontWeight::Bold, p.primary);
                ctx.drawLine(Point(leftX, curY + 26.0f), Point(leftX + contentW, curY + 26.0f), p.border, 1.0f);
            }
            curY += 34.0f;
            continue;
        }

        if (line.rfind(L"## ", 0) == 0) {
            if (curY >= body.top - 26.0f && curY <= body.bottom + 26.0f) {
                std::wstring text = line.substr(3);
                ctx.drawText(text, Point(leftX, curY), FontToken::Base, FontWeight::Bold, p.text);
            }
            curY += 28.0f;
            continue;
        }

        if (line.rfind(L"### ", 0) == 0) {
            if (curY >= body.top - 24.0f && curY <= body.bottom + 24.0f) {
                std::wstring text = line.substr(4);
                ctx.drawText(text, Point(leftX, curY), FontToken::Sm, FontWeight::SemiBold, p.primary);
            }
            curY += 24.0f;
            continue;
        }

        if (line.rfind(L"> ", 0) == 0) {
            if (curY >= body.top - 22.0f && curY <= body.bottom + 22.0f) {
                std::wstring text = line.substr(2);
                ctx.fillRect(Rect(leftX, curY, leftX + 3.5f, curY + 20.0f), p.primary);
                ctx.drawText(text, Point(leftX + 12.0f, curY + 2.0f), FontToken::Sm, FontWeight::Medium, p.textSecondary);
            }
            curY += 24.0f;
            continue;
        }

        if (line == L"---" || line == L"***") {
            if (curY >= body.top - 16.0f && curY <= body.bottom + 16.0f) {
                ctx.drawLine(Point(leftX, curY + 8.0f), Point(leftX + contentW, curY + 8.0f), p.border, 1.0f);
            }
            curY += 20.0f;
            continue;
        }

        if (line.rfind(L"- [x] ", 0) == 0 || line.rfind(L"- [ ] ", 0) == 0) {
            bool checked = (line[3] == L'x' || line[3] == L'X');
            if (curY >= body.top - 22.0f && curY <= body.bottom + 22.0f) {
                Rect cb(leftX + 2.0f, curY + 4.0f, leftX + 16.0f, curY + 18.0f);
                ctx.drawRoundedRect(cb, 3.0f, p.border, 1.2f);
                if (checked) {
                    ctx.fillRoundedRect(cb.inset(Thickness(2.0f)), 2.0f, p.primary);
                }
                std::wstring text = line.substr(6);
                ctx.drawText(text, Point(leftX + 24.0f, curY + 2.0f), FontToken::Sm, FontWeight::Regular, checked ? p.textMuted : p.text);
            }
            curY += 22.0f;
            continue;
        }

        if (line.rfind(L"- ", 0) == 0 || line.rfind(L"* ", 0) == 0) {
            if (curY >= body.top - 22.0f && curY <= body.bottom + 22.0f) {
                ctx.fillCircle(Point(leftX + 6.0f, curY + 10.0f), 2.5f, p.primary);
                std::wstring text = line.substr(2);
                ctx.drawText(text, Point(leftX + 18.0f, curY + 2.0f), FontToken::Sm, FontWeight::Regular, p.text);
            }
            curY += 22.0f;
            continue;
        }

        if (curY >= body.top - 22.0f && curY <= body.bottom + 22.0f) {
            ctx.drawText(line, Point(leftX, curY), FontToken::Sm, FontWeight::Regular, p.text);
        }
        curY += 22.0f;
    }
}

NativeQuickLookView::NativeQuickLookView() = default;
NativeQuickLookView::~NativeQuickLookView() = default;

void NativeQuickLookView::previewFile(const std::wstring& filePath) {
    m_filePath = filePath;
    m_scrollOffset = 0.0f;
    m_imageScale = 1.0f;
    m_hasError = false;
    m_errorMessage.clear();
    m_textLines.clear();
    m_folderEntries.clear();
    m_hexLines.clear();
    m_wicConverter.Reset();
    m_d2dBitmap.Reset();

    loadFileInfo();

    if (!m_hasError) {
        switch (m_fileType) {
        case QuickLookFileType::Text:
        case QuickLookFileType::Code:
        case QuickLookFileType::Markdown:
            loadTextContent();
            break;
        case QuickLookFileType::Image:
            loadImageContent();
            break;
        case QuickLookFileType::Folder:
            loadFolderContent();
            break;
        case QuickLookFileType::Binary:
        case QuickLookFileType::Media:
            loadBinaryHexDump();
            break;
        }
    }

    markNeedsLayout();
    markNeedsPaint();
}

void NativeQuickLookView::loadFileInfo() {
    size_t lastSlash = m_filePath.find_last_of(L"\\/");
    m_fileName = (lastSlash != std::wstring::npos) ? m_filePath.substr(lastSlash + 1) : m_filePath;

    DWORD attrs = GetFileAttributesW(m_filePath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        m_hasError = true;
        m_errorMessage = L"找不到目标文件或文件已被删除";
        m_fileType = QuickLookFileType::Text;
        m_fileSize = 0;
        return;
    }

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        m_fileType = QuickLookFileType::Folder;
        m_fileSize = 0;
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(m_filePath.c_str(), GetFileExInfoStandard, &fad)) {
        m_fileSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    }

    std::wstring ext;
    size_t dot = m_fileName.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext = m_fileName.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    }

    if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".gif" || ext == L".webp" || ext == L".ico") {
        m_fileType = QuickLookFileType::Image;
    } else if (ext == L".md" || ext == L".markdown") {
        m_fileType = QuickLookFileType::Markdown;
    } else if (ext == L".cpp" || ext == L".h" || ext == L".c" || ext == L".hpp" || ext == L".cs" ||
               ext == L".py" || ext == L".js" || ext == L".ts" || ext == L".tsx" || ext == L".json" ||
               ext == L".xml" || ext == L".html" || ext == L".css" || ext == L".sql" || ext == L".ps1" ||
               ext == L".rs" || ext == L".go" || ext == L".lua") {
        m_fileType = QuickLookFileType::Code;
    } else if (ext == L".txt" || ext == L".log" || ext == L".ini" || ext == L".yaml" || ext == L".yml") {
        m_fileType = QuickLookFileType::Text;
    } else if (ext == L".mp3" || ext == L".wav" || ext == L".flac" || ext == L".mp4" || ext == L".mkv") {
        m_fileType = QuickLookFileType::Media;
    } else {
        m_fileType = QuickLookFileType::Binary;
    }
}

void NativeQuickLookView::loadTextContent() {
    if (m_fileSize == 0) {
        m_textLines.push_back(L"(空文件，0 字节)");
        return;
    }

    std::ifstream file(m_filePath, std::ios::binary);
    if (!file) {
        m_hasError = true;
        m_errorMessage = L"无法读取目标文件或文件受系统独占锁定";
        return;
    }

    std::string content;
    content.resize(std::min<size_t>(1024 * 512, static_cast<size_t>(m_fileSize)));
    file.read(&content[0], content.size());
    content.resize(static_cast<size_t>(file.gcount()));

    std::wstring wcontent = tools3000::core::WinUtils::utf8ToWstring(content);
    std::wstringstream ss(wcontent);
    std::wstring line;
    while (std::getline(ss, line)) {
        m_textLines.push_back(line);
        if (m_textLines.size() >= 5000) {
            m_textLines.push_back(L"... (仅预览前 5000 行)");
            break;
        }
    }
}

void NativeQuickLookView::loadImageContent() {
    m_wicConverter.Reset();
    m_d2dBitmap.Reset();
    m_imageWidth = 0;
    m_imageHeight = 0;

    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(m_wicFactory.GetAddressOf())
    );
    if (hr == CO_E_NOTINITIALIZED) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        hr = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(m_wicFactory.GetAddressOf())
        );
    }
    if (SUCCEEDED(hr)) {
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        hr = m_wicFactory->CreateDecoderFromFilename(
            m_filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
            decoder.GetAddressOf()
        );
        if (SUCCEEDED(hr)) {
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            if (SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf()))) {
                frame->GetSize(&m_imageWidth, &m_imageHeight);
                hr = m_wicFactory->CreateFormatConverter(m_wicConverter.GetAddressOf());
                if (SUCCEEDED(hr)) {
                    m_wicConverter->Initialize(
                        frame.Get(),
                        GUID_WICPixelFormat32bppPBGRA,
                        WICBitmapDitherTypeNone,
                        nullptr,
                        0.0f,
                        WICBitmapPaletteTypeCustom
                    );
                }
            }
        }
    }

    if (!m_wicConverter) {
        m_hasError = true;
        m_errorMessage = L"无法解码图像格式或图像已损坏";
    }
}

void NativeQuickLookView::loadFolderContent() {
    m_folderEntries.clear();
    std::wstring searchPattern = m_filePath + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            uint64_t sz = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            m_folderEntries.push_back({ fd.cFileName, isDir, sz });
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

void NativeQuickLookView::loadBinaryHexDump() {
    m_hexLines.clear();
    std::ifstream file(m_filePath, std::ios::binary);
    if (!file) {
        m_hasError = true;
        m_errorMessage = L"无法读取二进制文件内容";
        return;
    }

    std::vector<uint8_t> buf(65536);
    file.read(reinterpret_cast<char*>(buf.data()), buf.size());
    size_t count = static_cast<size_t>(file.gcount());

    if (count == 0) {
        m_hexLines.push_back(L"(空二进制文件，0 字节)");
        return;
    }

    for (size_t offset = 0; offset < count; offset += 16) {
        wchar_t lineBuf[128];
        size_t chunk = std::min<size_t>(16, count - offset);

        swprintf_s(lineBuf, L"%08X: ", static_cast<unsigned int>(offset));
        std::wstring s = lineBuf;

        for (size_t i = 0; i < 16; ++i) {
            if (i < chunk) {
                swprintf_s(lineBuf, L"%02X ", buf[offset + i]);
                s += lineBuf;
            } else {
                s += L"   ";
            }
            if (i == 7) s += L" ";
        }

        s += L" | ";

        for (size_t i = 0; i < chunk; ++i) {
            uint8_t b = buf[offset + i];
            s += (b >= 32 && b <= 126) ? static_cast<wchar_t>(b) : L'.';
        }

        m_hexLines.push_back(s);
    }
}

void NativeQuickLookView::openExternal() {
    if (!m_filePath.empty()) {
        ShellExecuteW(nullptr, L"open", m_filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (m_host) m_host->hide();
    }
}

void NativeQuickLookView::locateInFolder() {
    if (!m_filePath.empty()) {
        std::wstring param = L"/select,\"" + m_filePath + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
        if (m_host) m_host->hide();
    }
}

void NativeQuickLookView::copyPath() {
    if (!m_filePath.empty() && OpenClipboard(nullptr)) {
        EmptyClipboard();
        size_t bytes = (m_filePath.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* p = GlobalLock(hMem);
            if (p) {
                memcpy(p, m_filePath.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

Size NativeQuickLookView::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)ctx;
    float w = availableWidth > 0 ? availableWidth : 920.0f;
    float h = availableHeight > 0 ? availableHeight : 640.0f;
    m_desiredSize = Size(w, h);
    return m_desiredSize;
}

void NativeQuickLookView::layout(const Rect& bounds, UIRenderContext& ctx) {
    UIElement::layout(bounds, ctx);
}

bool NativeQuickLookView::update(float dt) {
    (void)dt;
    return false;
}

bool NativeQuickLookView::onKeyDown(const UIKeyEvent& e) {
    if (e.virtualKey == VK_SPACE || e.virtualKey == VK_ESCAPE) {
        if (m_host) m_host->hide();
        return true;
    }
    if (e.virtualKey == VK_RETURN) {
        openExternal();
        return true;
    }
    return false;
}

bool NativeQuickLookView::onMouseWheel(const UIMouseEvent& e) {
    if (m_fileType == QuickLookFileType::Image) {
        if (e.wheelDelta > 0) {
            m_imageScale = std::min(10.0f, m_imageScale * 1.15f);
        } else {
            m_imageScale = std::max(0.1f, m_imageScale * 0.87f);
        }
        markNeedsPaint();
        return true;
    }

    float maxScroll = 0.0f;
    if (m_fileType == QuickLookFileType::Code || m_fileType == QuickLookFileType::Text) {
        maxScroll = std::max(0.0f, m_textLines.size() * 20.0f - (m_bounds.height() - 120.0f));
    } else if (m_fileType == QuickLookFileType::Markdown) {
        maxScroll = std::max(0.0f, m_textLines.size() * 24.0f - (m_bounds.height() - 120.0f));
    } else if (m_fileType == QuickLookFileType::Binary || m_fileType == QuickLookFileType::Media) {
        maxScroll = std::max(0.0f, m_hexLines.size() * 20.0f - (m_bounds.height() - 120.0f));
    } else if (m_fileType == QuickLookFileType::Folder) {
        maxScroll = std::max(0.0f, m_folderEntries.size() * 26.0f - (m_bounds.height() - 120.0f));
    }

    m_scrollOffset = std::clamp(m_scrollOffset - e.wheelDelta * 0.4f, 0.0f, maxScroll);
    markNeedsPaint();
    return true;
}

bool NativeQuickLookView::onMouseDown(const UIMouseEvent& e) {
    // 检查双击重置图像缩放
    if (m_fileType == QuickLookFileType::Image && e.clickCount >= 2) {
        m_imageScale = 1.0f;
        markNeedsPaint();
        return true;
    }

    // 检查头部动作按钮点击
    Rect header(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + 48.0f);
    if (header.contains(e.position)) {
        float rx = header.right - 40.0f;

        // 关闭 X 按钮
        Rect closeBtn(rx, header.top + 10.0f, rx + 28.0f, header.top + 38.0f);
        if (closeBtn.contains(e.position)) {
            if (m_host) m_host->hide();
            return true;
        }
        rx -= 90.0f;

        // "打开" 按钮
        Rect openBtn(rx, header.top + 10.0f, rx + 80.0f, header.top + 38.0f);
        if (openBtn.contains(e.position)) {
            openExternal();
            return true;
        }
        rx -= 90.0f;

        // "定位" 按钮
        Rect locBtn(rx, header.top + 10.0f, rx + 80.0f, header.top + 38.0f);
        if (locBtn.contains(e.position)) {
            locateInFolder();
            return true;
        }
        rx -= 90.0f;

        // "复制路径" 按钮
        Rect copyBtn(rx, header.top + 10.0f, rx + 80.0f, header.top + 38.0f);
        if (copyBtn.contains(e.position)) {
            copyPath();
            return true;
        }
    }

    return false;
}

void NativeQuickLookView::render(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    bool isDark = theme.isEffectiveDark();

    // ── 1. 顶部文件信息与操作工具栏 (Header Bar) ─────────────────────────────
    Rect header(m_bounds.left + 16.0f, m_bounds.top + 10.0f, m_bounds.right - 16.0f, m_bounds.top + 48.0f);
    IconType fileIcon = (m_fileType == QuickLookFileType::Folder) ? IconType::Folder :
                        (m_fileType == QuickLookFileType::Image)  ? IconType::Crop :
                        (m_fileType == QuickLookFileType::Media)  ? IconType::Music :
                        (m_fileType == QuickLookFileType::Code)   ? IconType::Code : IconType::FileText;

    VectorIconRenderer::drawIcon(ctx, fileIcon, Rect(header.left + 4.0f, header.top + 8.0f, header.left + 24.0f, header.top + 28.0f), p.primary, 1.8f);

    ctx.drawText(m_fileName, Point(header.left + 32.0f, header.top + 8.0f), FontToken::Base, FontWeight::Bold, p.text);

    // 元数据 (大小)
    if (m_fileSize > 0) {
        wchar_t szBuf[64];
        if (m_fileSize < 1024) swprintf_s(szBuf, L"%llu B", m_fileSize);
        else if (m_fileSize < 1024 * 1024) swprintf_s(szBuf, L"%.1f KB", m_fileSize / 1024.0f);
        else swprintf_s(szBuf, L"%.2f MB", m_fileSize / (1024.0f * 1024.0f));

        ctx.drawText(szBuf, Point(header.left + 32.0f + m_fileName.size() * 9.0f + 12.0f, header.top + 10.0f),
                     FontToken::Xs, FontWeight::Medium, p.textMuted);
    }

    // 头部操作按钮组 (复制路径、定位、打开、关闭)
    float rx = header.right - 30.0f;
    VectorIconRenderer::drawIcon(ctx, IconType::X, Rect(rx, header.top + 10.0f, rx + 18.0f, header.top + 28.0f), p.textSecondary, 1.6f);
    rx -= 84.0f;

    auto drawActionBtn = [&](const wchar_t* label, IconType ico, float x, Color bgCol, Color textCol) {
        Rect btn(x, header.top + 6.0f, x + 76.0f, header.top + 32.0f);
        ctx.fillRoundedRect(btn, 4.0f, bgCol);
        ctx.drawRoundedRect(btn, 4.0f, p.border, 1.0f);
        VectorIconRenderer::drawIcon(ctx, ico, Rect(btn.left + 6.0f, btn.top + 6.0f, btn.left + 20.0f, btn.top + 20.0f), textCol, 1.5f);
        ctx.drawText(label, Point(btn.left + 24.0f, btn.top + 5.0f), FontToken::Xs, FontWeight::SemiBold, textCol);
    };

    drawActionBtn(L"打开", IconType::ExternalLink, rx, p.primary, Color(1.0f, 1.0f, 1.0f, 1.0f)); rx -= 84.0f;
    drawActionBtn(L"定位", IconType::FolderOpen, rx, isDark ? Color(1.0f, 1.0f, 1.0f, 0.05f) : Color(0.0f, 0.0f, 0.0f, 0.05f), p.textSecondary); rx -= 84.0f;
    drawActionBtn(L"复制路径", IconType::Copy, rx, isDark ? Color(1.0f, 1.0f, 1.0f, 0.05f) : Color(0.0f, 0.0f, 0.0f, 0.05f), p.textSecondary);

    // 分割线
    ctx.drawLine(Point(m_bounds.left + 16.0f, m_bounds.top + 50.0f), Point(m_bounds.right - 16.0f, m_bounds.top + 50.0f), p.border, 1.0f);

    // ── 2. 主内容渲染区 (Main Content View) ──────────────────────────────────
    Rect body(m_bounds.left + 16.0f, m_bounds.top + 56.0f, m_bounds.right - 16.0f, m_bounds.bottom - 40.0f);
    Color bodyBg = isDark ? Color(0.08f, 0.09f, 0.12f, 0.95f) : Color(0.98f, 0.98f, 0.99f, 0.95f);
    ctx.fillRoundedRect(body, 6.0f, bodyBg);
    ctx.drawRoundedRect(body, 6.0f, p.border, 1.0f);

    if (m_hasError) {
        float cx = body.left + body.width() * 0.5f;
        float cy = body.top + body.height() * 0.5f;
        VectorIconRenderer::drawIcon(ctx, IconType::Ban, Rect(cx - 24.0f, cy - 46.0f, cx + 24.0f, cy + 2.0f), Color(0.95f, 0.40f, 0.35f, 1.0f), 2.0f);
        ctx.drawText(m_errorMessage.empty() ? L"无法预览该文件" : m_errorMessage, Point(cx - 110.0f, cy + 14.0f), FontToken::Base, FontWeight::SemiBold, p.text);
        ctx.drawText(L"请检查文件路径是否存在或是否具有读取权限", Point(cx - 130.0f, cy + 38.0f), FontToken::Xs, FontWeight::Regular, p.textMuted);
    } else if (m_fileType == QuickLookFileType::Code) {
        float lineH = 20.0f;
        int startLine = std::max(0, static_cast<int>(m_scrollOffset / lineH));
        int endLine = std::min(static_cast<int>(m_textLines.size()),
                               static_cast<int>((m_scrollOffset + body.height()) / lineH) + 1);

        ctx.fillRect(Rect(body.left, body.top, body.left + 48.0f, body.bottom),
                     isDark ? Color(1.0f, 1.0f, 1.0f, 0.02f) : Color(0.0f, 0.0f, 0.0f, 0.02f));
        ctx.drawLine(Point(body.left + 48.0f, body.top), Point(body.left + 48.0f, body.bottom), p.border, 1.0f);

        ctx.pushClip(body);
        for (int i = startLine; i < endLine; ++i) {
            float ly = body.top + 8.0f + i * lineH - m_scrollOffset;

            wchar_t numBuf[16];
            swprintf_s(numBuf, L"%d", i + 1);
            ctx.drawText(numBuf, Point(body.left + 8.0f, ly), FontToken::Xs, FontWeight::Regular, p.textMuted, FontFamilyType::Mono);

            const auto& lineText = m_textLines[i];
            renderCodeLine(ctx, lineText, body.left + 56.0f, ly, p, isDark);
        }
        ctx.popClip();
    } else if (m_fileType == QuickLookFileType::Markdown) {
        ctx.pushClip(body);
        renderMarkdownView(ctx, m_textLines, body, m_scrollOffset, p, isDark);
        ctx.popClip();
    } else if (m_fileType == QuickLookFileType::Text) {
        float lineH = 20.0f;
        int startLine = std::max(0, static_cast<int>(m_scrollOffset / lineH));
        int endLine = std::min(static_cast<int>(m_textLines.size()),
                               static_cast<int>((m_scrollOffset + body.height()) / lineH) + 1);

        ctx.fillRect(Rect(body.left, body.top, body.left + 48.0f, body.bottom),
                     isDark ? Color(1.0f, 1.0f, 1.0f, 0.02f) : Color(0.0f, 0.0f, 0.0f, 0.02f));
        ctx.drawLine(Point(body.left + 48.0f, body.top), Point(body.left + 48.0f, body.bottom), p.border, 1.0f);

        ctx.pushClip(body);
        for (int i = startLine; i < endLine; ++i) {
            float ly = body.top + 8.0f + i * lineH - m_scrollOffset;

            wchar_t numBuf[16];
            swprintf_s(numBuf, L"%d", i + 1);
            ctx.drawText(numBuf, Point(body.left + 8.0f, ly), FontToken::Xs, FontWeight::Regular, p.textMuted, FontFamilyType::Mono);

            const auto& lineText = m_textLines[i];
            ctx.drawText(lineText, Point(body.left + 56.0f, ly), FontToken::Sm, FontWeight::Regular, p.text, FontFamilyType::Mono);
        }
        ctx.popClip();
    } else if (m_fileType == QuickLookFileType::Image) {
        if (!m_d2dBitmap && m_wicConverter && ctx.target()) {
            ctx.target()->CreateBitmapFromWicBitmap(m_wicConverter.Get(), nullptr, m_d2dBitmap.GetAddressOf());
        }

        if (m_d2dBitmap) {
            float availW = body.width() - 32.0f;
            float availH = body.height() - 32.0f;
            float imgW = static_cast<float>(m_imageWidth);
            float imgH = static_cast<float>(m_imageHeight);
            if (imgW > 0.0f && imgH > 0.0f) {
                float baseScale = std::min(availW / imgW, availH / imgH);
                float finalScale = baseScale * m_imageScale;
                float drawW = imgW * finalScale;
                float drawH = imgH * finalScale;
                float dx = body.left + (body.width() - drawW) * 0.5f;
                float dy = body.top + (body.height() - drawH) * 0.5f;
                Rect destRect(dx, dy, dx + drawW, dy + drawH);

                ctx.pushClip(body);
                ctx.drawBitmap(m_d2dBitmap.Get(), destRect, 1.0f);
                ctx.popClip();

                wchar_t badge[64];
                swprintf_s(badge, L"%u × %u px · %.0f%%", m_imageWidth, m_imageHeight, m_imageScale * 100.0f);
                Rect badgeRect(body.right - 150.0f, body.bottom - 28.0f, body.right - 12.0f, body.bottom - 8.0f);
                ctx.fillRoundedRect(badgeRect, 4.0f, isDark ? Color(0.0f, 0.0f, 0.0f, 0.65f) : Color(1.0f, 1.0f, 1.0f, 0.75f));
                ctx.drawRoundedRect(badgeRect, 4.0f, p.border, 1.0f);
                ctx.drawText(badge, Point(badgeRect.left + 8.0f, badgeRect.top + 3.0f), FontToken::Xs, FontWeight::Medium, p.textSecondary);
            }
        } else {
            float cx = body.left + body.width() * 0.5f;
            float cy = body.top + body.height() * 0.5f;
            VectorIconRenderer::drawIcon(ctx, IconType::Crop, Rect(cx - 36.0f, cy - 40.0f, cx + 36.0f, cy + 32.0f), p.primary, 1.8f);

            wchar_t resBuf[64];
            swprintf_s(resBuf, L"图片预览: %u × %u 像素", m_imageWidth, m_imageHeight);
            ctx.drawText(resBuf, Point(cx - 70.0f, cy + 46.0f), FontToken::Base, FontWeight::SemiBold, p.text);
        }
    } else if (m_fileType == QuickLookFileType::Folder) {
        float lineH = 26.0f;
        int startLine = std::max(0, static_cast<int>(m_scrollOffset / lineH));
        int endLine = std::min(static_cast<int>(m_folderEntries.size()),
                               static_cast<int>((m_scrollOffset + body.height() - 36.0f) / lineH) + 1);

        ctx.drawText(L"文件夹内容速查预览:", Point(body.left + 16.0f, body.top + 10.0f), FontToken::Sm, FontWeight::Bold, p.text);

        ctx.pushClip(Rect(body.left, body.top + 34.0f, body.right, body.bottom));
        for (int i = startLine; i < endLine; ++i) {
            const auto& item = m_folderEntries[i];
            float fy = body.top + 34.0f + i * lineH - m_scrollOffset;
            IconType ico = item.isDir ? IconType::Folder : IconType::FileText;
            VectorIconRenderer::drawIcon(ctx, ico, Rect(body.left + 16.0f, fy + 2.0f, body.left + 30.0f, fy + 16.0f), p.primary, 1.5f);
            ctx.drawText(item.name, Point(body.left + 36.0f, fy), FontToken::Sm, FontWeight::Regular, p.text);

            if (!item.isDir) {
                wchar_t sBuf[32];
                swprintf_s(sBuf, L"%.1f KB", item.sizeBytes / 1024.0f);
                ctx.drawText(sBuf, Point(body.right - 90.0f, fy), FontToken::Xs, FontWeight::Regular, p.textMuted);
            }
        }
        ctx.popClip();
    } else {
        float lineH = 20.0f;
        int startLine = std::max(0, static_cast<int>(m_scrollOffset / lineH));
        int endLine = std::min(static_cast<int>(m_hexLines.size()),
                               static_cast<int>((m_scrollOffset + body.height()) / lineH) + 1);

        ctx.pushClip(body);
        for (int i = startLine; i < endLine; ++i) {
            float hy = body.top + 8.0f + i * lineH - m_scrollOffset;
            ctx.drawText(m_hexLines[i], Point(body.left + 16.0f, hy), FontToken::Xs, FontWeight::Regular, p.textSecondary, FontFamilyType::Mono);
        }
        ctx.popClip();
    }

    // ── 3. 底部状态栏与快捷键说明 (Footer Bar) ──────────────────────────────
    Rect footer(m_bounds.left + 16.0f, m_bounds.bottom - 34.0f, m_bounds.right - 16.0f, m_bounds.bottom);
    ctx.drawText(L"Space 切换/关闭 · Esc 退出 · Enter 打开文件",
                 Point(footer.left + 4.0f, footer.top + 6.0f),
                 FontToken::Xs, FontWeight::Regular, p.textSecondary);

    ctx.drawText(L"Tools3000 QuickLook 原生极速预览引擎",
                 Point(footer.right - 230.0f, footer.top + 6.0f),
                 FontToken::Xs, FontWeight::Medium, p.textMuted);
}

// ─────────────────────────────────────────────────────────────────────────────
// NativeQuickLookApp 单例宿主实现
// ─────────────────────────────────────────────────────────────────────────────

NativeQuickLookApp& NativeQuickLookApp::instance() {
    static NativeQuickLookApp s_app;
    return s_app;
}

NativeQuickLookApp::NativeQuickLookApp() = default;
NativeQuickLookApp::~NativeQuickLookApp() {
    destroy();
}

void NativeQuickLookApp::ensureInitialized(HINSTANCE hInstance) {
    if (m_host) return;

    m_host = std::make_unique<NativeFramelessWindow>();
    NativeWindowConfig config;
    config.title = L"Tools3000 QuickLook";
    config.width = 920;
    config.height = 640;
    config.minWidth = 640;
    config.minHeight = 480;
    config.centerOnScreen = true;
    config.resizable = true;
    config.seamlessTitlebar = false;
    config.isPopup = true;
    config.isToolWindow = true;
    config.alwaysOnTop = true;

    if (m_host->create(hInstance, config)) {
        m_host->setFramelessMode(FramelessMode::SpotlightCenter);
        m_view = std::make_shared<NativeQuickLookView>();
        m_view->onAttachedToHost(m_host.get());
        m_host->setRootElement(m_view);

        m_host->setOnFocusLost([this]() {
            hide();
        });
    }
}

void NativeQuickLookApp::show(const std::wstring& filePath, HINSTANCE hInstance) {
    ensureInitialized(hInstance);
    if (!m_host) return;

    if (m_view) {
        m_view->previewFile(filePath);
    }

    m_host->positionCentered(920, 640);
    m_host->show(SW_SHOW);
    SetForegroundWindow(m_host->hwnd());
}

void NativeQuickLookApp::previewFile(const std::wstring& filePath) {
    if (m_view) {
        m_view->previewFile(filePath);
    }
}

void NativeQuickLookApp::hide() {
    if (m_host && m_host->isVisible()) {
        m_host->hide();
    }
}

void NativeQuickLookApp::destroy() {
    if (m_host) {
        m_host->destroy();
        m_host.reset();
    }
}

bool NativeQuickLookApp::isVisible() const {
    return m_host && m_host->isVisible();
}

std::wstring NativeQuickLookApp::currentFilePath() const {
    return m_view ? m_view->getFilePath() : L"";
}

} // namespace tools3000::ui::native
