import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// https://vite.dev/config/
export default defineConfig({
  // 用相对路径打包资源: WebView2 以 file:/// 加载 index.html 时, 绝对路径 /assets/...
  // 会被解析到盘符根目录而加载失败(页面空白)。必须用 './' 让资源走相对路径。
  base: './',
  plugins: [react()],
})
