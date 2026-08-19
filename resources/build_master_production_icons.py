import os
import sys
import struct
from io import BytesIO
from PIL import Image, ImageDraw, ImageFilter
import numpy as np

def create_squircle_mask(size, radius_ratio=0.22):
    w, h = size
    scale = 4
    sw, sh = w * scale, h * scale
    mask = Image.new("L", (sw, sh), 0)
    draw = ImageDraw.Draw(mask)
    r = int(min(sw, sh) * radius_ratio)
    draw.rounded_rectangle([(0, 0), (sw, sh)], radius=r, fill=255)
    return mask.resize((w, h), Image.Resampling.LANCZOS)

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

def extract_large_pure_tray_icon(jpg_path, crop_box, bg_threshold=50):
    img = Image.open(jpg_path).convert("RGBA")
    cropped = img.crop(crop_box)
    
    arr = np.array(cropped)
    r, g, b = arr[:,:,0].astype(float), arr[:,:,1].astype(float), arr[:,:,2].astype(float)
    brightness = 0.299 * r + 0.587 * g + 0.114 * b
    
    low_cut = bg_threshold
    high_cut = bg_threshold + 40
    alpha = np.clip((brightness - low_cut) / (high_cut - low_cut) * 255.0, 0, 255).astype(np.uint8)
    
    res = Image.new("RGBA", cropped.size, (255, 255, 255, 0))
    res.putalpha(Image.fromarray(alpha, "L"))
    
    bbox = res.getbbox()
    if bbox:
        res_cropped = res.crop(bbox)
        # 大号托盘图标：填满 98% 的画布 (留出极小 1% 边距，达到最大视觉冲击力与清晰度)
        target_size = 502
        res_cropped.thumbnail((target_size, target_size), Image.Resampling.LANCZOS)
        final_img = Image.new("RGBA", (512, 512), (255, 255, 255, 0))
        offset = ((512 - res_cropped.width) // 2, (512 - res_cropped.height) // 2)
        final_img.paste(res_cropped, offset, res_cropped)
        return final_img
    return res

def build_scheme(scheme_num: int):
    artifact_dir = r"C:\Users\yuan2\.gemini\antigravity\brain\a4d8e3c0-7c1c-4d27-95cd-65baa13ee7a1"
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    resources_dir = os.path.join(repo_root, "resources")
    ui_components_dir = os.path.join(repo_root, "ui", "src", "components")
    ui_public_dir = os.path.join(repo_root, "ui", "public")

    if scheme_num == 1:
        print(">> 装配 方案 1: 超速滑翔光翼 E (大号纯白托盘 + 高清母版)...")
        app_master_jpg = os.path.join(artifact_dir, "letter_e_aero_glide_1787155546091.jpg")
        tray_master_jpg = os.path.join(artifact_dir, "master_tray_aero_glide_e_1787158355720.jpg")
        
        img_app = Image.open(app_master_jpg).convert("RGBA")
        squircle = img_app.crop((120, 120, 904, 904)).resize((512, 512), Image.Resampling.LANCZOS)
        mask = create_squircle_mask((512, 512), 0.22)
        squircle.putalpha(mask)
        
        tray_img = extract_large_pure_tray_icon(tray_master_jpg, (270, 290, 730, 710), 50)

    else:
        print(">> 装配 方案 4: 莫比乌斯流体尾迹 e (大号纯白托盘 + 高清母版)...")
        app_master_jpg = os.path.join(artifact_dir, "letter_e_sonic_loop_1787155667576.jpg")
        tray_master_jpg = os.path.join(artifact_dir, "master_tray_sonic_loop_e_1787158373479.jpg")
        
        img_app = Image.open(app_master_jpg).convert("RGBA")
        squircle = img_app.crop((156, 160, 868, 872)).resize((512, 512), Image.Resampling.LANCZOS)
        mask = create_squircle_mask((512, 512), 0.22)
        squircle.putalpha(mask)
        
        tray_img = extract_large_pure_tray_icon(tray_master_jpg, (320, 310, 840, 670), 50)

    # 1. 输出 resources/app.ico 与 resources/tray.ico
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

    print(f"Scheme {scheme_num} 大号托盘与高保真母版资产全部就绪！")

if __name__ == "__main__":
    scheme = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    build_scheme(scheme)
