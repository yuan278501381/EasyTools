import os
import struct
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

def create_squircle_mask(size, radius_ratio=0.22):
    w, h = size
    scale = 4
    sw, sh = w * scale, h * scale
    mask = Image.new("L", (sw, sh), 0)
    draw = ImageDraw.Draw(mask)
    r = int(min(sw, sh) * radius_ratio)
    draw.rounded_rectangle([(0, 0), (sw, sh)], radius=r, fill=255)
    return mask.resize((w, h), Image.Resampling.LANCZOS)

def create_gradient_bg(size, color1, color2):
    w, h = size
    arr = np.zeros((h, w, 4), dtype=np.uint8)
    for y in range(h):
        for x in range(w):
            t = (x + y) / (w + h)
            r = int(color1[0] * (1 - t) + color2[0] * t)
            g = int(color1[1] * (1 - t) + color2[1] * t)
            b = int(color1[2] * (1 - t) + color2[2] * t)
            arr[y, x] = (r, g, b, 255)
    return Image.fromarray(arr)

def add_inner_glow(image, mask, glow_color=(255,255,255,40), width=4):
    # Create an edge mask for the inner border
    edge_mask = mask.copy()
    edge_draw = ImageDraw.Draw(edge_mask)
    # To do a simple inner stroke, we can shrink the mask slightly and subtract
    # But since it's a rounded rect, PIL doesn't easily shrink. 
    # Let's use a Gaussian blur and threshold to get an edge, or just skip it 
    # to keep it super clean.
    return image

def save_ico(image: Image.Image, output_path: str, sizes=(16, 20, 24, 32, 40, 48, 64, 128, 256)):
    images_data = []
    header_size = 6 + len(sizes) * 16
    current_offset = header_size

    for sz in sizes:
        resized = image.resize((sz, sz), Image.Resampling.LANCZOS).convert("RGBA")
        arr = np.array(resized)
        bgra = np.zeros((sz, sz, 4), dtype=np.uint8)
        bgra[:, :, 0] = arr[:, :, 2]
        bgra[:, :, 1] = arr[:, :, 1]
        bgra[:, :, 2] = arr[:, :, 0]
        bgra[:, :, 3] = arr[:, :, 3]
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

def build_world_class_icons():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    source_logo_path = os.path.join(repo_root, "ui", "public", "Logo_Origin.png")
    resources_dir = os.path.join(repo_root, "resources")
    
    if not os.path.exists(source_logo_path):
        print(f"Error: {source_logo_path} not found.")
        return

    print("Loading source logo...")
    raw_img = Image.open(source_logo_path).convert("RGBA")
    bbox = raw_img.getbbox()
    cropped = raw_img.crop(bbox)
    
    # Pad to perfect square
    size = max(cropped.width, cropped.height)
    # Add a tiny 2% padding so it doesn't touch the absolute edge of the canvas
    pad = int(size * 0.02)
    full_size = size + pad * 2
    white_logo = Image.new("RGBA", (full_size, full_size), (0,0,0,0))
    offset = ((full_size - cropped.width) // 2, (full_size - cropped.height) // 2)
    white_logo.paste(cropped, offset, cropped)
    
    # 1. GENERATE TRAY ICONS (Dual-theme)
    # White logo for Dark Taskbars
    save_ico(white_logo, os.path.join(resources_dir, "tray.ico"))
    
    # Scheme A: Mocha Brown Border (#3A2312) + Pure White Core for Light Taskbars
    tray_light_canvas = Image.new("RGBA", (full_size, full_size), (0, 0, 0, 0))
    logo_alpha = cropped.split()[3]
    thick_alpha = logo_alpha.copy()
    filter_iters = max(3, int(full_size * 0.022))
    for _ in range(filter_iters):
        thick_alpha = thick_alpha.filter(ImageFilter.MaxFilter(5))
        
    brown_border = Image.new("RGBA", (cropped.width, cropped.height), (58, 35, 18, 255))
    brown_border.putalpha(thick_alpha)
    tray_light_canvas.paste(brown_border, offset, brown_border)
    
    white_core = Image.new("RGBA", (cropped.width, cropped.height), (255, 255, 255, 255))
    white_core.putalpha(logo_alpha)
    tray_light_canvas.paste(white_core, offset, white_core)
    
    save_ico(tray_light_canvas, os.path.join(resources_dir, "tray_dark.ico"))
    
    # 2. GENERATE APP ICON (World-Class Squircle)
    canvas_size = 1024
    # Deep premium slate/blue gradient (Midnight)
    bg = create_gradient_bg((canvas_size, canvas_size), (30, 41, 59), (15, 23, 42))
    
    # Squircle mask
    mask = create_squircle_mask((canvas_size, canvas_size), 0.225)
    bg.putalpha(mask)
    
    # Resize white logo to fit beautifully inside the squircle (~60% of canvas)
    target_w = int(canvas_size * 0.6)
    logo_resized = white_logo.resize((target_w, target_w), Image.Resampling.LANCZOS)
    
    # Add a stunning, soft drop shadow to the white logo
    shadow = logo_resized.copy()
    shadow_data = np.array(shadow)
    shadow_data[:, :, 0:3] = 0 # Turn black
    shadow = Image.fromarray(shadow_data).filter(ImageFilter.GaussianBlur(25))
    
    # Composite
    comp = bg.copy()
    offset_x = (canvas_size - target_w) // 2
    offset_y = (canvas_size - target_w) // 2
    
    # Paste shadow with a slight downward offset
    comp.paste(shadow, (offset_x, offset_y + 20), shadow)
    # Paste white logo on top
    comp.paste(logo_resized, (offset_x, offset_y), logo_resized)
    
    save_ico(comp, os.path.join(resources_dir, "app.ico"))
    
    # Save a high-res PNG for React UI Base64
    app_png_path = os.path.join(resources_dir, "app_icon_hires.png")
    comp.save(app_png_path)
    
    # Update React Component with the high-res squircle
    import base64
    with open(app_png_path, "rb") as f:
        b64 = base64.b64encode(f.read()).decode()
        
    react_code = f'''import React from 'react';
// eslint-disable-next-line @typescript-eslint/no-unused-vars
export const EasyToolsBolt: React.FC<{{size?: number, className?: string, fill?: string}} & React.ImgHTMLAttributes<HTMLImageElement>> = ({{size = 24, className = '', fill, ...props}}) => (
  <img src="data:image/png;base64,{b64}" width={{size}} height={{size}} className={{className}} style={{{{display:'inline-block', flexShrink:0}}}} {{...props}} />
);
'''
    with open(os.path.join(repo_root, "ui", "src", "components", "EasyToolsBolt.tsx"), "w", encoding="utf-8") as f:
        f.write(react_code)
        
    print("World-class production icons built successfully using user's official logo!")

if __name__ == "__main__":
    build_world_class_icons()
