import os
import sys
import struct
from io import BytesIO
from PIL import Image
import numpy as np

def save_ico(image: Image.Image, output_path: str, sizes=(16, 20, 24, 32, 40, 48, 64, 128, 256)):
    """
    生成符合 Windows 规范的未压缩 32 位 DIB 格式 ICO 文件，完全支持 High-DPI
    """
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
        
        bih = struct.pack(
            "<LLLHHLLLLLL",
            40, sz, sz * 2, 1, 32, 0, xor_bytes, 0, 0, 0, 0
        )
        
        dib_data = bih + bgra_flipped + and_mask
        images_data.append((sz, image_bytes, current_offset, dib_data))
        current_offset += image_bytes

    with open(output_path, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(sizes)))
        for sz, img_bytes, offset, _ in images_data:
            w_byte = 0 if sz == 256 else sz
            h_byte = 0 if sz == 256 else sz
            f.write(struct.pack("<BBBBHHLL", w_byte, h_byte, 0, 0, 1, 32, img_bytes, offset))
        for _, _, _, data in images_data:
            f.write(data)
            
    print(f"Master ICO Generated: {output_path} ({len(sizes)} sizes)")

def build_scheme(scheme_num: int):
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    resources_dir = os.path.join(repo_root, "resources")
    ui_components_dir = os.path.join(repo_root, "ui", "src", "components")
    ui_public_dir = os.path.join(repo_root, "ui", "public")

    if scheme_num == 1:
        print(">> 装配 方案 1: 超速滑翔光翼 E (100% 纯净无黑斑母版)...")
        app_png = os.path.join(repo_root, "master_scheme1_app_512.png")
        tray_png = os.path.join(repo_root, "master_scheme1_tray_isolated.png")
    else:
        print(">> 装配 方案 4: 莫比乌斯流体尾迹 e (100% 纯净无黑斑母版)...")
        app_png = os.path.join(repo_root, "master_scheme4_app_512.png")
        tray_png = os.path.join(repo_root, "master_scheme4_tray_isolated.png")

    squircle = Image.open(app_png).convert("RGBA")
    tray_img = Image.open(tray_png).convert("RGBA")

    # 1. 输出 ICO
    app_ico_path = os.path.join(resources_dir, "app.ico")
    tray_ico_path = os.path.join(resources_dir, "tray.ico")
    save_ico(squircle, app_ico_path)
    save_ico(tray_img, tray_ico_path)

    # 2. 前端 UI 组件 base64 内嵌
    buffered = BytesIO()
    squircle.resize((96, 96), Image.Resampling.LANCZOS).save(buffered, format="PNG")
    import base64
    b64_str = base64.b64encode(buffered.getvalue()).decode("utf-8")
    
    # 3. 更新 ui/public/favicon.svg
    favicon_svg_content = f'''<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 96 96" width="96" height="96">
  <image width="96" height="96" xlink:href="data:image/png;base64,{b64_str}"/>
</svg>'''
    with open(os.path.join(ui_public_dir, "favicon.svg"), "w", encoding="utf-8") as f:
        f.write(favicon_svg_content)

    # 4. 更新 React 组件 EasyToolsBolt.tsx
    bolt_component_code = f'''import React from 'react';

interface EasyToolsBoltProps {{
  size?: number;
  className?: string;
  fill?: string;
}}

/**
 * 官方标准 100% 高保真母版品牌 Logo (Scheme {scheme_num})
 */
export const EasyToolsBolt: React.FC<EasyToolsBoltProps> = ({{
  size = 24,
  className = '',
}}) => {{
  return (
    <img
      src="data:image/png;base64,{b64_str}"
      alt="EasyTools"
      width={{size}}
      height={{size}}
      className={{className}}
      style={{{{ display: 'inline-block', verticalAlign: 'middle', flexShrink: 0, borderRadius: size * 0.22 }}}}
      aria-hidden="true"
    />
  );
}};
'''
    with open(os.path.join(ui_components_dir, "EasyToolsBolt.tsx"), "w", encoding="utf-8") as f:
        f.write(bolt_component_code)

    print(f"Scheme {scheme_num} 100% 高保真母版全链路资产已成功同步！")

if __name__ == "__main__":
    scheme = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    build_scheme(scheme)
