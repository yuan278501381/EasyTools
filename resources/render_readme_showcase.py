import os
import asyncio
import base64
from playwright.async_api import async_playwright

async def render_banner():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    logo_path = os.path.join(repo_root, "resources", "app_icon_hires.png")
    output_dir = os.path.join(repo_root, "docs", "images")
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, "about_hero_showcase.png")

    with open(logo_path, "rb") as f:
        logo_b64 = base64.b64encode(f.read()).decode()

    html_content = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
  @import url('https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@400;500;600;700;800&family=JetBrains+Mono:wght@500;700&display=swap');

  * {{
    box-sizing: border-box;
    margin: 0;
    padding: 0;
  }}

  body {{
    background: transparent;
    display: flex;
    align-items: center;
    justify-content: center;
    font-family: 'Plus Jakarta Sans', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    padding: 24px;
    width: 1400px;
    height: 720px;
    overflow: hidden;
  }}

  .grand-showcase {{
    position: relative;
    width: 100%;
    height: 100%;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    border-radius: 28px;
    background: linear-gradient(145deg, #090d16 0%, #0f172a 40%, #1e293b 100%);
    overflow: hidden;
    box-shadow: 0 24px 64px rgba(0, 0, 0, 0.45), inset 0 1px 0 rgba(255, 255, 255, 0.1);
    border: 1px solid rgba(255, 255, 255, 0.08);
  }}

  .grand-showcase::before {{
    content: "";
    position: absolute;
    top: 0; left: 0; right: 0; bottom: 0;
    background-image: 
      radial-gradient(circle at 18% 40%, rgba(56, 189, 248, 0.14) 0%, transparent 45%),
      radial-gradient(circle at 82% 35%, rgba(124, 58, 237, 0.14) 0%, transparent 45%),
      radial-gradient(circle at 50% 90%, rgba(14, 165, 233, 0.08) 0%, transparent 50%);
    pointer-events: none;
  }}

  .grid-pattern {{
    position: absolute;
    inset: 0;
    background-size: 36px 36px;
    background-image: 
      linear-gradient(to right, rgba(255, 255, 255, 0.02) 1px, transparent 1px),
      linear-gradient(to bottom, rgba(255, 255, 255, 0.02) 1px, transparent 1px);
    mask-image: radial-gradient(ellipse at center, black 40%, transparent 75%);
    -webkit-mask-image: radial-gradient(ellipse at center, black 40%, transparent 75%);
    pointer-events: none;
  }}

  .aura {{
    position: absolute;
    width: 320px;
    height: 320px;
    background: radial-gradient(circle, rgba(56, 189, 248, 0.28) 0%, rgba(99, 102, 241, 0.16) 50%, transparent 70%);
    filter: blur(50px);
    border-radius: 50%;
    top: 40%;
    left: 50%;
    transform: translate(-50%, -60%);
    pointer-events: none;
  }}

  .logo-container {{
    position: relative;
    z-index: 2;
    margin-bottom: 22px;
  }}

  .logo-img {{
    width: 156px;
    height: 156px;
    filter: drop-shadow(0 20px 40px rgba(0, 0, 0, 0.65)) drop-shadow(0 0 25px rgba(56, 189, 248, 0.25));
    border-radius: 34px;
    display: block;
  }}

  .text-content {{
    position: relative;
    z-index: 2;
    text-align: center;
    display: flex;
    flex-direction: column;
    align-items: center;
  }}

  .brand-title {{
    font-size: 3.4rem;
    font-weight: 800;
    letter-spacing: -0.035em;
    background: linear-gradient(135deg, #ffffff 0%, #f1f5f9 45%, #94a3b8 100%);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    line-height: 1.1;
    display: flex;
    align-items: center;
    gap: 16px;
  }}

  .version-tag {{
    font-size: 1.1rem;
    font-weight: 700;
    font-family: 'JetBrains Mono', monospace;
    background: rgba(56, 189, 248, 0.15);
    color: #38bdf8;
    border: 1px solid rgba(56, 189, 248, 0.35);
    padding: 4px 14px;
    border-radius: 999px;
    letter-spacing: 0;
    box-shadow: 0 0 15px rgba(56, 189, 248, 0.2);
    -webkit-text-fill-color: #38bdf8;
  }}

  .subtitle {{
    margin-top: 10px;
    font-size: 1.15rem;
    color: #94a3b8;
    font-weight: 600;
    letter-spacing: 0.12em;
    text-transform: uppercase;
  }}

  .feature-pills {{
    margin-top: 24px;
    display: flex;
    gap: 12px;
    flex-wrap: wrap;
    justify-content: center;
  }}

  .pill {{
    display: inline-flex;
    align-items: center;
    gap: 7px;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    backdrop-filter: blur(12px);
    padding: 7px 16px;
    border-radius: 10px;
    font-size: 0.88rem;
    font-weight: 600;
    color: #e2e8f0;
    letter-spacing: 0.02em;
  }}

  .pill-dot {{
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: #38bdf8;
    box-shadow: 0 0 8px #38bdf8;
  }}

  .author-badge {{
    margin-top: 20px;
    font-size: 0.85rem;
    color: #64748b;
    display: flex;
    align-items: center;
    gap: 6px;
    font-weight: 500;
  }}

  .author-name {{
    color: #cbd5e1;
    font-weight: 600;
  }}
</style>
</head>
<body>
  <div class="grand-showcase">
    <div class="grid-pattern"></div>
    <div class="aura"></div>
    
    <div class="logo-container">
      <img src="data:image/png;base64,{logo_b64}" class="logo-img" alt="EasyTools Grand Logo" />
    </div>

    <div class="text-content">
      <div class="brand-title">
        <span>EasyTools</span>
        <span class="version-tag">v1.0.5</span>
      </div>
      <p class="subtitle">The Ultimate Windows Productivity Suite</p>
      
      <div class="feature-pills">
        <div class="pill"><span class="pill-dot"></span>C++20 & Direct2D 亚毫秒内核</div>
        <div class="pill"><span class="pill-dot" style="background:#818cf8;box-shadow:0 0 8px #818cf8;"></span>React 19 & Fluent Design</div>
        <div class="pill"><span class="pill-dot" style="background:#34d399;box-shadow:0 0 8px #34d399;"></span>1000Hz 鼠标手势与 HUD 回显</div>
        <div class="pill"><span class="pill-dot" style="background:#f43f5e;box-shadow:0 0 8px #f43f5e;"></span>全盘 MFT 毫秒级闪电搜索</div>
        <div class="pill"><span class="pill-dot" style="background:#fbbf24;box-shadow:0 0 8px #fbbf24;"></span>100% 单元测试代码覆盖率</div>
      </div>

      <div class="author-badge">
        <span>Designed & Engineered with passion by</span>
        <span class="author-name">Yy1 (@yuan278501381)</span>
        <span>• MIT License</span>
      </div>
    </div>
  </div>
</body>
</html>
"""

    temp_html = os.path.join(repo_root, "docs", "images", "temp_banner.html")
    with open(temp_html, "w", encoding="utf-8") as f:
        f.write(html_content)

    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page(viewport={"width": 1400, "height": 720}, device_scale_factor=2)
        await page.goto(f"file://{temp_html}")
        await page.locator(".grand-showcase").screenshot(path=output_path, omit_background=True)
        await browser.close()

    if os.path.exists(temp_html):
        os.remove(temp_html)

    print(f"Grand Showcase Banner rendered to: {output_path}")

if __name__ == "__main__":
    asyncio.run(render_banner())
