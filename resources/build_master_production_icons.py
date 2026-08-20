import os
import asyncio
from PIL import Image
from playwright.async_api import async_playwright
import struct
import numpy as np

SVG_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
<style>
  body { margin: 0; padding: 0; background: transparent; overflow: hidden; }
  svg { display: block; }
</style>
</head>
<body>
<svg id="app-icon" viewBox="0 0 512 512" width="512" height="512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="bgGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#2a3245" />
      <stop offset="100%" stop-color="#111622" />
    </linearGradient>
    
    <linearGradient id="topGrad" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#FFFFFF" />
      <stop offset="100%" stop-color="#E2E8F0" />
    </linearGradient>
    <linearGradient id="spineGrad" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" stop-color="#F1F5F9" />
      <stop offset="100%" stop-color="#CBD5E1" />
    </linearGradient>
    <linearGradient id="midGrad" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#94A3B8" />
      <stop offset="100%" stop-color="#F8FAFC" />
    </linearGradient>
    <linearGradient id="botGrad" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#CBD5E1" />
      <stop offset="100%" stop-color="#F1F5F9" />
    </linearGradient>

    <filter id="shadow" x="-30%" y="-30%" width="160%" height="160%">
      <feDropShadow dx="0" dy="12" stdDeviation="10" flood-color="#000000" flood-opacity="0.35" />
    </filter>
    
    <filter id="shadowLight" x="-30%" y="-30%" width="160%" height="160%">
      <feDropShadow dx="0" dy="8" stdDeviation="6" flood-color="#000000" flood-opacity="0.2" />
    </filter>
  </defs>

  <!-- Squircle Base -->
  <rect x="24" y="24" width="464" height="464" rx="106" fill="url(#bgGrad)" stroke="rgba(255,255,255,0.15)" stroke-width="2" />

  <!-- The E Ribbon -->
  <g transform="translate(180, 66) skewX(-18)">
    <!-- Layer 1: Middle Wing -->
    <rect x="20" y="150" width="220" height="80" rx="40" fill="url(#midGrad)" />
    
    <!-- Layer 2: Bottom Wing -->
    <path d="M 0 280 L 0 300 A 80 80 0 0 0 80 380 L 260 380 A 40 40 0 0 0 260 300 L 80 300 L 80 280 Z" fill="url(#botGrad)" />
    
    <!-- Layer 3: Spine -->
    <rect x="0" y="60" width="80" height="240" fill="url(#spineGrad)" filter="url(#shadowLight)" />
    
    <!-- Layer 4: Top Wing -->
    <path d="M 280 0 L 80 0 A 80 80 0 0 0 0 80 L 80 80 L 280 80 A 40 40 0 0 0 280 0 Z" fill="url(#topGrad)" filter="url(#shadow)" />
  </g>
</svg>

<svg id="tray-icon" viewBox="0 0 512 512" width="512" height="512" xmlns="http://www.w3.org/2000/svg" style="margin-top: 500px;">
  <!-- Pure White version with subtle shadows for Tray -->
  <g transform="translate(180, 66) skewX(-18)">
    <rect x="20" y="150" width="220" height="80" rx="40" fill="#E2E8F0" />
    <path d="M 0 280 L 0 300 A 80 80 0 0 0 80 380 L 260 380 A 40 40 0 0 0 260 300 L 80 300 L 80 280 Z" fill="#F1F5F9" />
    <rect x="0" y="60" width="80" height="240" fill="#F8FAFC" filter="url(#shadowLight)" />
    <path d="M 280 0 L 80 0 A 80 80 0 0 0 0 80 L 80 80 L 280 80 A 40 40 0 0 0 280 0 Z" fill="#FFFFFF" filter="url(#shadow)" />
  </g>
</svg>
</body>
</html>
"""

def save_ico(image: Image.Image, output_path: str, sizes=(16, 20, 24, 32, 40, 48, 64, 128, 256)):
    images_data = []
    header_size = 6 + len(sizes) * 16
    current_offset = header_size

    for sz in sizes:
        resized = image.resize((sz, sz), Image.Resampling.LANCZOS).convert("RGBA")
        arr = np.array(resized)
        bgra = np.zeros((sz, sz, 4), dtype=np.uint8)
        bgra[:, :, 0] = arr[:, :, 2] # B
        bgra[:, :, 1] = arr[:, :, 1] # G
        bgra[:, :, 2] = arr[:, :, 0] # R
        bgra[:, :, 3] = arr[:, :, 3] # A
        bgra_flipped = np.flipud(bgra).tobytes()
        xor_bytes = sz * sz * 4
        and_row_bytes = ((sz + 31) // 32) * 4
        and_mask = bytes(and_row_bytes * sz)
        image_bytes = 40 + xor_bytes + len(and_mask)
        bih = struct.pack("<LLLHHLLLLLL", 40, sz, sz * 2, 1, 32, 0, xor_bytes, 0, 0, 0, 0)
        images_data.append((sz, image_bytes, current_offset, bih + bgra_flipped + and_mask))
        current_offset += image_bytes

    with open(output_path, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(sizes)))
        for sz, img_bytes, offset, _ in images_data:
            w_byte = 0 if sz == 256 else sz
            h_byte = 0 if sz == 256 else sz
            f.write(struct.pack("<BBBBHHLL", w_byte, h_byte, 0, 0, 1, 32, img_bytes, offset))
        for _, _, _, data in images_data:
            f.write(data)
    print(f"ICO Generated: {output_path} ({len(sizes)} sizes)")

async def render_svgs():
    html_path = os.path.abspath("temp_vector.html")
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(SVG_TEMPLATE)

    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page()
        await page.set_viewport_size({"width": 1024, "height": 1200})
        await page.goto(f"file://{html_path}")
        
        # Screenshot App Icon
        app_el = await page.locator("#app-icon").bounding_box()
        await page.screenshot(path="temp_app.png", clip=app_el, omit_background=True)
        
        # Screenshot Tray Icon
        tray_el = await page.locator("#tray-icon").bounding_box()
        await page.screenshot(path="temp_tray.png", clip=tray_el, omit_background=True)
        
        await browser.close()

    os.remove(html_path)

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    resources_dir = os.path.join(repo_root, "resources")
    
    # Save App Icon
    app_img = Image.open("temp_app.png")
    save_ico(app_img, os.path.join(resources_dir, "app.ico"))
    
    # Save Tray Icon (Crop tightly first)
    tray_img = Image.open("temp_tray.png")
    bbox = tray_img.getbbox()
    tray_crop = tray_img.crop(bbox)
    # Pad to square
    size = max(tray_crop.width, tray_crop.height)
    tray_sq = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    offset = ((size - tray_crop.width) // 2, (size - tray_crop.height) // 2)
    tray_sq.paste(tray_crop, offset, tray_crop)
    save_ico(tray_sq, os.path.join(resources_dir, "tray.ico"))
    
    # Update React Component with SVG
    b64 = __import__("base64").b64encode(open("temp_app.png", "rb").read()).decode()
    with open(os.path.join(repo_root, "ui", "src", "components", "EasyToolsBolt.tsx"), "w", encoding="utf-8") as f:
        f.write(f'''import React from 'react';
// eslint-disable-next-line @typescript-eslint/no-unused-vars
export const EasyToolsBolt: React.FC<{{size?: number, className?: string, fill?: string}} & React.ImgHTMLAttributes<HTMLImageElement>> = ({{size = 24, className = '', fill, ...props}}) => (
  <img src="data:image/png;base64,{b64}" width={{size}} height={{size}} className={{className}} style={{{{display:'inline-block', flexShrink:0, borderRadius:size*0.22}}}} {{...props}} />
);''')
    
    os.remove("temp_app.png")
    os.remove("temp_tray.png")
    print("Perfect Geometric Icons fully built and deployed!")

if __name__ == "__main__":
    asyncio.run(render_svgs())
