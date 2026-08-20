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

def create_gradient_bg(size, color1, color2):
    w, h = size
    img = Image.new("RGBA", size)
    arr = np.zeros((h, w, 4), dtype=np.uint8)
    
    # Simple linear gradient top-left to bottom-right
    for y in range(h):
        for x in range(w):
            t = (x + y) / (w + h)
            r = int(color1[0] * (1 - t) + color2[0] * t)
            g = int(color1[1] * (1 - t) + color2[1] * t)
            b = int(color1[2] * (1 - t) + color2[2] * t)
            arr[y, x] = (r, g, b, 255)
            
    return Image.fromarray(arr)

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
            
    print(f"ICO Generated: {output_path} ({len(sizes)} sizes)")

def build_perfect_icons():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    archive_dir = os.path.join(repo_root, "resources", "branding_archive")
    resources_dir = os.path.join(repo_root, "resources")
    
    # 1. Load the flawless extracted large E
    large_e_path = os.path.join(archive_dir, "extracted_large_e.png")
    if not os.path.exists(large_e_path):
        print("Error: extracted_large_e.png not found.")
        return
        
    large_e = Image.open(large_e_path).convert("RGBA")
    
    # 2. App Icon (Squircle with Gradient)
    # The mockup uses a deep space blue-gray background. 
    # Let's use #2F3746 (47, 55, 70) to #1C2331 (28, 35, 49)
    bg = create_gradient_bg((512, 512), (56, 68, 88), (31, 38, 51))
    
    # Create squircle mask
    mask = create_squircle_mask((512, 512), 0.22)
    bg.putalpha(mask)
    
    # Paste the letter E centered
    e_w, e_h = large_e.size
    # We want it to take up about 75% of the height
    target_h = int(512 * 0.75)
    target_w = int(e_w * (target_h / e_h))
    e_resized = large_e.resize((target_w, target_h), Image.Resampling.LANCZOS)
    
    offset_x = (512 - target_w) // 2
    offset_y = (512 - target_h) // 2
    
    # Drop shadow
    shadow = e_resized.copy()
    shadow_data = np.array(shadow)
    shadow_data[:, :, 0:3] = 0 # Black
    shadow = Image.fromarray(shadow_data).filter(ImageFilter.GaussianBlur(15))
    
    bg.paste(shadow, (offset_x, offset_y + 10), shadow)
    bg.paste(e_resized, (offset_x, offset_y), e_resized)
    
    save_ico(bg, os.path.join(resources_dir, "app.ico"))
    
    # 3. Tray Icon (Pure White shape from the same perfect alpha mask)
    # The tray E has some slits, let's just make it completely white but keep the alpha gradients 
    # so the overlapping layers look slightly transparent at the folds, just like the actual mockup.
    tray_e = large_e.copy()
    tray_data = np.array(tray_e)
    # Set RGB to 255, keep alpha
    tray_data[:, :, 0:3] = 255
    tray_pure = Image.fromarray(tray_data)
    
    # Make it a square
    size = max(tray_pure.width, tray_pure.height)
    tray_sq = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    offset = ((size - tray_pure.width) // 2, (size - tray_pure.height) // 2)
    tray_sq.paste(tray_pure, offset, tray_pure)
    
    # Crop tight so it fills 98% of tray
    bbox = tray_sq.getbbox()
    tray_sq = tray_sq.crop(bbox)
    
    # Pad it to exactly square
    final_size = max(tray_sq.width, tray_sq.height)
    # Add a tiny 2px padding for 256x256 (less than 1%)
    pad = int(final_size * 0.02)
    final_tray = Image.new("RGBA", (final_size + pad*2, final_size + pad*2), (255, 255, 255, 0))
    offset_2 = ((final_size + pad*2 - tray_sq.width) // 2, (final_size + pad*2 - tray_sq.height) // 2)
    final_tray.paste(tray_sq, offset_2, tray_sq)
    
    save_ico(final_tray, os.path.join(resources_dir, "tray.ico"))
    
    print("Perfect matching icons generated from user mockup!")

if __name__ == "__main__":
    build_perfect_icons()
