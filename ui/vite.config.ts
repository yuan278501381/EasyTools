import { defineConfig } from 'vite'
import type { Plugin } from 'vite'
import react from '@vitejs/plugin-react'
import fs from 'fs'
import path from 'path'

// 自定义插件：将 Vite 动态分配的端口写入本地文件，供 C++ 后端在开发模式下动态读取
function writeDevServerUrl(): Plugin {
  return {
    name: 'write-dev-server-url',
    configureServer(server) {
      server.httpServer?.once('listening', () => {
        const address = server.httpServer?.address()
        if (address && typeof address === 'object') {
          const url = `http://localhost:${address.port}`
          const outPath = path.resolve(__dirname, '.dev-server-url')
          fs.writeFileSync(outPath, url, 'utf-8')
          
          console.log(`\n✨ EasyTools 动态端口就绪。按住 Ctrl 点击下方链接可在浏览器中打开调试：`)
        }
      })
    }
  }
}

// https://vite.dev/config/
export default defineConfig({
  base: './',
  plugins: [react(), writeDevServerUrl()],
  server: {
    host: '0.0.0.0', // 监听 0.0.0.0 以在终端展示物料局域网 IP
    strictPort: false // 允许端口被占用时动态递增选择新端口
  }
})
