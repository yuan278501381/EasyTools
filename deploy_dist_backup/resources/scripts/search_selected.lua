-- ─────────────────────────────────────────────────────────────────────────────
-- search_selected.lua — 搜索选中文本 / 打开选中的 URL
--
-- 绑定方式: 在「手势设置」中将某个手势的动作类型设为 “Lua 脚本”，粘贴本文件内容。
-- 行为:
--   1. 复制当前选中的文本 (Ctrl+C);
--   2. 若内容是 http(s) 链接, 直接用默认浏览器打开;
--   3. 否则使用搜索引擎搜索该文本。
-- ─────────────────────────────────────────────────────────────────────────────

-- 1. 触发复制并等待剪贴板就绪
easy.keyboard.sendKeys("Ctrl+C")

local text = easy.clipboard.getText()
if not text or text == "" then
    easy.log.warn("search_selected: 剪贴板为空")
    return
end

-- 去除首尾空白
text = text:gsub("^%s+", ""):gsub("%s+$", "")

if text:match("^https?://") then
    easy.shell.open(text)
else
    local query = easy.url.encode(text)
    easy.shell.open("https://www.google.com/search?q=" .. query)
end
