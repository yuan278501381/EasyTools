# EasyTools UI

EasyTools 的 React 19 + TypeScript 设置、搜索和托盘界面。生产构建由
`vite-plugin-singlefile` 合并为单个 `dist/index.html`，由原生 WebView2
容器通过 `https://easytools.local/` 虚拟域加载；前后端通过带请求 ID、
超时和错误回传的消息桥通信。

## 本地开发

```powershell
npm ci
npm run dev
```

原生 Debug 构建会连接 `http://localhost:5173`。脱离原生宿主打开时，
`useBridge` 提供可预测的开发 Mock，便于检查页面状态。

## 质量检查

```powershell
npm run lint
npm run i18n-check
npm run build
```

- `lint`：ESLint 与 React Hooks 规则。
- `i18n-check`：校验中英文键集合一致。
- `build`：TypeScript 工程检查并生成单文件生产资源。

版本号必须与根目录 `CMakeLists.txt` 的 `project(... VERSION ...)` 一致；
根目录 `deploy.ps1` 会在发布前强制校验。
