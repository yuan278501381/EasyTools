#include "capture/CaptureDropdownMenu.h"

#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

namespace tools3000::capture {

void openDropdownMenu(CaptureState& state, DropdownType type, const D2D1_RECT_F& anchorRect) {
    if (anchorRect.right <= anchorRect.left) {
        // 纯快捷键触发，无按钮物理矩形：直接轮转下一项
        if (type == DropdownType::BeautyShellBg) {
            int cur = static_cast<int>(state.beautyShell.bgType);
            int next = (cur + 1) % static_cast<int>(BeautyBackgroundType::COUNT);
            commitDropdownSelection(state, DropdownType::BeautyShellBg, next);
            return;
        } else if (type == DropdownType::BeautyShellPadding) {
            int curPad = state.beautyShell.padding;
            int nextPad = (curPad <= 24) ? 48 : (curPad <= 48 ? 72 : 24);
            commitDropdownSelection(state, DropdownType::BeautyShellPadding, nextPad);
            return;
        } else if (type == DropdownType::BeautyShellRadius) {
            static const std::array<float, 5> radiuses = { 0.0f, 10.0f, 16.0f, 24.0f, 32.0f };
            float cur = state.beautyShell.cornerRadius;
            int nextIdx = 0;
            for (size_t i = 0; i < radiuses.size(); ++i) {
                if (radiuses[i] > cur + 0.5f) { nextIdx = static_cast<int>(i); break; }
            }
            commitDropdownSelection(state, DropdownType::BeautyShellRadius, static_cast<int>(radiuses[nextIdx]));
            return;
        }
    }

    if (state.dropdownMenu.type == type) {
        closeDropdownMenu(state, true);
        return;
    }

    state.dropdownMenu.type = type;
    state.dropdownMenu.anchorButtonRect = anchorRect;
    state.dropdownMenu.hoveredIndex = -1;
    state.dropdownMenu.items.clear();
    state.dropdownMenu.originalBgType = state.beautyShell.bgType;
    const bool isZh = tools3000::core::WinUtils::isSystemLanguageChinese();

    switch (type) {
        case DropdownType::BeautyShellBg: {
            state.dropdownMenu.items = {
                { 0, isZh ? L"摄影棚冷灰" : L"Studio Slate", isZh ? L"质感冷调" : L"Cool Tone",
                  state.beautyShell.bgType == BeautyBackgroundType::StudioSlate, false, {},
                  true, D2D1::ColorF(226.0f/255.0f, 232.0f/255.0f, 240.0f/255.0f), D2D1::ColorF(203.0f/255.0f, 213.0f/255.0f, 225.0f/255.0f) },
                { 1, isZh ? L"温润纸白" : L"Paper Chalk", isZh ? L"素雅柔和" : L"Soft Neutral",
                  state.beautyShell.bgType == BeautyBackgroundType::PaperChalk, false, {},
                  true, D2D1::ColorF(248.0f/255.0f, 250.0f/255.0f, 252.0f/255.0f), D2D1::ColorF(241.0f/255.0f, 245.0f/255.0f, 249.0f/255.0f) },
                { 2, isZh ? L"晨雾微光" : L"Silk Mist", isZh ? L"晶莹通透" : L"Subtle Light",
                  state.beautyShell.bgType == BeautyBackgroundType::SilkMist, false, {},
                  true, D2D1::ColorF(238.0f/255.0f, 242.0f/255.0f, 246.0f/255.0f), D2D1::ColorF(226.0f/255.0f, 232.0f/255.0f, 240.0f/255.0f) },
                { 3, isZh ? L"曜石深邃" : L"Midnight Graphite", isZh ? L"极客沉浸" : L"Dark Matte",
                  state.beautyShell.bgType == BeautyBackgroundType::MidnightGraphite, false, {},
                  true, D2D1::ColorF(30.0f/255.0f, 41.0f/255.0f, 59.0f/255.0f), D2D1::ColorF(15.0f/255.0f, 23.0f/255.0f, 42.0f/255.0f) },
                { 4, isZh ? L"悬浮纯影" : L"Pure Minimal", isZh ? L"纯粹无界" : L"Pure Clean",
                  state.beautyShell.bgType == BeautyBackgroundType::PureMinimal, false, {},
                  true, D2D1::ColorF(1.0f, 1.0f, 1.0f), D2D1::ColorF(0.96f, 0.96f, 0.97f) }
            };
            break;
        }

        case DropdownType::BeautyShellPadding: {
            state.dropdownMenu.items = {
                { 24, isZh ? L"紧凑留白" : L"Compact", L"24 px", state.beautyShell.padding == 24 },
                { 48, isZh ? L"舒适自然" : L"Standard", L"48 px", state.beautyShell.padding == 48 },
                { 72, isZh ? L"大气呼吸" : L"Spacious", L"72 px", state.beautyShell.padding == 72 }
            };
            break;
        }

        case DropdownType::BeautyShellRadius: {
            int curR = static_cast<int>(std::round(state.beautyShell.cornerRadius));
            state.dropdownMenu.items = {
                { 0, isZh ? L"硬朗直角" : L"Sharp", L"0 px", curR == 0 },
                { 10, isZh ? L"轻微圆润" : L"Subtle", L"10 px", curR == 10 },
                { 16, isZh ? L"标准优雅" : L"Standard", L"16 px", curR == 16 },
                { 24, isZh ? L"大弧流线" : L"Rounded", L"24 px", curR == 24 },
                { 32, isZh ? L"胶囊视界" : L"Curved", L"32 px", curR == 32 }
            };
            break;
        }

        case DropdownType::RecordFormat: {
            state.dropdownMenu.items = {
                { 0, isZh ? L"MP4 视频" : L"MP4 Video", L"H.264 / AAC", state.recordSetup.format == RecordFormat::MP4_H264 },
                { 1, isZh ? L"GIF 动图" : L"GIF Animation", isZh ? L"微信 20MB 保护" : L"WeChat 20MB Safe", state.recordSetup.format == RecordFormat::GIF }
            };
            break;
        }

        case DropdownType::RecordFps: {
            state.dropdownMenu.items = {
                { 15, L"15 fps", isZh ? L"省流极简" : L"Low Size", state.recordSetup.fps == 15 },
                { 30, L"30 fps", isZh ? L"推荐流畅" : L"Balanced", state.recordSetup.fps == 30 },
                { 60, L"60 fps", isZh ? L"极客高刷" : L"Ultra Smooth", state.recordSetup.fps == 60 }
            };
            break;
        }

        case DropdownType::RecordQuality: {
            state.dropdownMenu.items = {
                { 0, isZh ? L"标准画质" : L"Standard", L"5 Mbps", state.recordSetup.qualityLevel == 0 },
                { 1, isZh ? L"高清 1080P" : L"High Definition", L"10 Mbps", state.recordSetup.qualityLevel == 1 },
                { 2, isZh ? L"原画超清" : L"Original Master", L"20 Mbps", state.recordSetup.qualityLevel == 2 }
            };
            break;
        }

        case DropdownType::RecordClickEffect: {
            bool ripple = state.recordSetup.showClickEffects;
            bool zoom = state.recordSetup.showClickZoom;
            int curMode = (!ripple && !zoom) ? 0 : (ripple && !zoom ? 1 : (!ripple && zoom ? 2 : 3));
            state.dropdownMenu.items = {
                { 0, isZh ? L"无点击反馈" : L"None", isZh ? L"保持纯净" : L"Pure Clean", curMode == 0 },
                { 1, isZh ? L"水波纹动效" : L"Water Ripple", isZh ? L"扩散涟漪" : L"Ripple Wave", curMode == 1 },
                { 2, isZh ? L"Mac 点击缩放" : L"Mac Click Zoom", isZh ? L"柔和聚焦" : L"Smooth Focus", curMode == 2 },
                { 3, isZh ? L"水波纹 + 缩放" : L"Ripple + Zoom", isZh ? L"双重反馈" : L"Dual Feedback", curMode == 3 }
            };
            break;
        }

        default:
            break;
    }

    state.toolbarLayoutValid = false;
}

void commitDropdownSelection(CaptureState& state, DropdownType type, int id) {
    const bool isZh = tools3000::core::WinUtils::isSystemLanguageChinese();
    state.loupeToastUntil = GetTickCount() + 1400;

    switch (type) {
        case DropdownType::BeautyShellBg: {
            if (id >= 0 && id < static_cast<int>(BeautyBackgroundType::COUNT)) {
                state.beautyShell.bgType = static_cast<BeautyBackgroundType>(id);
                state.beautyShell.enabled = true;
                state.dropdownMenu.originalBgType = state.beautyShell.bgType;
                const char* themeName = "studio_slate";
                switch (state.beautyShell.bgType) {
                    case BeautyBackgroundType::StudioSlate: themeName = "studio_slate"; break;
                    case BeautyBackgroundType::PaperChalk: themeName = "paper_chalk"; break;
                    case BeautyBackgroundType::SilkMist: themeName = "silk_mist"; break;
                    case BeautyBackgroundType::MidnightGraphite: themeName = "midnight_graphite"; break;
                    case BeautyBackgroundType::PureMinimal: themeName = "pure_minimal"; break;
                    default: break;
                }
                tools3000::core::ConfigManager::instance().set<std::string>("/capture/beautyShellTheme", themeName);
                tools3000::core::ConfigManager::instance().set<bool>("/capture/beautyShellEnabled", true);
                static const wchar_t* bgNames[] = { L"摄影棚冷灰", L"温润纸白", L"晨雾微光", L"曜石深邃", L"悬浮纯影" };
                static const wchar_t* bgNamesEn[] = { L"Studio Slate", L"Paper Chalk", L"Silk Mist", L"Midnight Graphite", L"Pure Minimal" };
                state.loupeToastMessage = (isZh ? L"外壳质感: " : L"Shell Theme: ") + std::wstring(isZh ? bgNames[id] : bgNamesEn[id]);
            }
            break;
        }

        case DropdownType::BeautyShellPadding: {
            state.beautyShell.padding = id;
            state.beautyShell.enabled = true;
            tools3000::core::ConfigManager::instance().set<int>("/capture/beautyShellPadding", id);
            tools3000::core::ConfigManager::instance().set<bool>("/capture/beautyShellEnabled", true);
            state.loupeToastMessage = isZh ? std::format(L"外壳留白: {} px", id) : std::format(L"Shell Padding: {} px", id);
            break;
        }

        case DropdownType::BeautyShellRadius: {
            state.beautyShell.cornerRadius = static_cast<float>(id);
            state.beautyShell.enabled = true;
            tools3000::core::ConfigManager::instance().set<int>("/capture/beautyShellRadius", id);
            tools3000::core::ConfigManager::instance().set<bool>("/capture/beautyShellEnabled", true);
            state.loupeToastMessage = isZh ? std::format(L"外壳圆角: {} px", id) : std::format(L"Shell Radius: {} px", id);
            break;
        }

        case DropdownType::RecordFormat: {
            state.recordSetup.format = (id == 1) ? RecordFormat::GIF : RecordFormat::MP4_H264;
            tools3000::core::ConfigManager::instance().set<std::string>(
                "/recording/format", state.recordSetup.format == RecordFormat::GIF ? "gif" : "mp4_h264");
            state.loupeToastMessage = isZh
                ? (state.recordSetup.format == RecordFormat::GIF ? L"录屏格式: GIF 动图" : L"录屏格式: MP4 视频")
                : (state.recordSetup.format == RecordFormat::GIF ? L"Format: GIF" : L"Format: MP4");
            break;
        }

        case DropdownType::RecordFps: {
            state.recordSetup.fps = id;
            tools3000::core::ConfigManager::instance().set<int>("/recording/fps", id);
            state.loupeToastMessage = isZh ? std::format(L"录制帧率: {} fps", id) : std::format(L"Frame Rate: {} fps", id);
            break;
        }

        case DropdownType::RecordQuality: {
            state.recordSetup.qualityLevel = id;
            int bitrate = (id == 0) ? 5 : (id == 1 ? 10 : 20);
            tools3000::core::ConfigManager::instance().set<int>("/recording/bitrate", bitrate);
            static const wchar_t* qNames[] = { L"标准画质", L"高清 1080P", L"原画超清" };
            static const wchar_t* qNamesEn[] = { L"Standard", L"1080P HD", L"Original Master" };
            state.loupeToastMessage = (isZh ? L"录制画质: " : L"Video Quality: ") + std::wstring(isZh ? qNames[id] : qNamesEn[id]);
            break;
        }

        case DropdownType::RecordClickEffect: {
            if (id == 0) {
                state.recordSetup.showClickEffects = false;
                state.recordSetup.showClickZoom = false;
                state.loupeToastMessage = isZh ? L"点击特效: 已关闭" : L"Click Effects: None";
            } else if (id == 1) {
                state.recordSetup.showClickEffects = true;
                state.recordSetup.showClickZoom = false;
                state.loupeToastMessage = isZh ? L"点击特效: 水波纹" : L"Click Effects: Water Ripple";
            } else if (id == 2) {
                state.recordSetup.showClickEffects = false;
                state.recordSetup.showClickZoom = true;
                state.loupeToastMessage = isZh ? L"点击特效: Mac 点击缩放" : L"Click Effects: Mac Click Zoom";
            } else {
                state.recordSetup.showClickEffects = true;
                state.recordSetup.showClickZoom = true;
                state.loupeToastMessage = isZh ? L"点击特效: 水波纹 + 缩放" : L"Click Effects: Ripple + Zoom";
            }
            tools3000::core::ConfigManager::instance().set<bool>("/recording/showClickEffects", state.recordSetup.showClickEffects);
            tools3000::core::ConfigManager::instance().set<bool>("/recording/showClickZoom", state.recordSetup.showClickZoom);
            break;
        }

        default:
            break;
    }

    state.dropdownMenu.type = DropdownType::None;
    state.dropdownMenu.items.clear();
    state.toolbarLayoutValid = false;
}

void updateDropdownHover(CaptureState& state, int hoverIndex) {
    if (hoverIndex == state.dropdownMenu.hoveredIndex) return;
    state.dropdownMenu.hoveredIndex = hoverIndex;

    if (state.dropdownMenu.type == DropdownType::BeautyShellBg) {
        if (hoverIndex >= 0 && hoverIndex < static_cast<int>(state.dropdownMenu.items.size())) {
            state.beautyShell.bgType = static_cast<BeautyBackgroundType>(state.dropdownMenu.items[hoverIndex].id);
        } else {
            state.beautyShell.bgType = state.dropdownMenu.originalBgType;
        }
    }
}

void closeDropdownMenu(CaptureState& state, bool restorePreview) {
    if (restorePreview && state.dropdownMenu.type == DropdownType::BeautyShellBg) {
        state.beautyShell.bgType = state.dropdownMenu.originalBgType;
    }
    state.dropdownMenu.type = DropdownType::None;
    state.dropdownMenu.items.clear();
    state.dropdownMenu.hoveredIndex = -1;
    state.toolbarLayoutValid = false;
}

}  // namespace tools3000::capture
