# EasyTools — 开发任务清单

## Phase 1 — 基础设施 + 鼠标手势 MVP

### 1.1 项目骨架搭建
- [x] 顶层 CMakeLists.txt + vcpkg.json
- [x] .clang-format / .clang-tidy 配置
- [x] src/main.cpp 入口点
- [ ] WebView2 集成（Evergreen Runtime）
- [ ] 前端项目 ui/ 初始化（Vite + React + TypeScript）

### 1.2 核心基础设施
- [x] 统一日志系统（Logger + TraceID）
- [x] 配置管理器（JSON 读写 + 热加载）
- [x] 全局快捷键管理器
- [x] 系统托盘图标
- [x] WebView2 ↔ C++ IPC 桥接
- [x] 崩溃收集（MiniDump）

### 1.3 鼠标手势核心
- [x] 低级鼠标钩子（WH_MOUSE_LL）
- [x] 方向编码手势识别算法
- [x] 手势动作分发系统
- [x] 默认手势集（14个全局 + 2个浏览器专用）
- [x] 作用域规则引擎（进程/类名/句柄）
- [ ] 手势轨迹可视化
- [ ] 暂停/恢复快捷键（已注册，UI 反馈待做）

### 1.4 WebView2 设置界面
- [ ] 设置界面骨架（侧边栏导航 + 内容区）
- [ ] 手势设置页
- [ ] 通用设置页
- [ ] 暗色/亮色主题切换

## Phase 2 — 截图 MVP（待 Phase 1 完成后展开）
## Phase 3 — 高级功能（待 Phase 2 完成后展开）
## Phase 4 — 打磨 & 发布（待 Phase 3 完成后展开）
