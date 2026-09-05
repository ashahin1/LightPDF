import os
from PIL import Image, ImageDraw, ImageFont

def generate_lightpdf_icon(output_path):
    # Render on high-res 512x512 canvas for supersampled crispness
    size = 512
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Coordinates for document sheet
    # Left: 80, Top: 40, Right: 432, Bottom: 472
    doc_left, doc_top = 80, 40
    doc_right, doc_bottom = 432, 472
    corner_fold = 90  # folded corner size

    # Shadow behind document
    shadow_offset = 12
    draw.rounded_rectangle(
        [doc_left + shadow_offset, doc_top + shadow_offset, doc_right + shadow_offset, doc_bottom + shadow_offset],
        radius=36,
        fill=(0, 0, 0, 90)
    )

    # Document main polygon (cutting top-right corner for fold)
    # Shape points:
    # 1. (doc_left, doc_top + 36) -> rounded top-left
    # 2. (doc_right - corner_fold, doc_top) -> start fold
    # 3. (doc_right, doc_top + corner_fold) -> end fold
    # 4. (doc_right, doc_bottom) -> bottom-right
    # 5. (doc_left, doc_bottom) -> bottom-left
    
    # We can draw the main base rounded rect in crimson red:
    # Rich PDF red: #DC2626
    red_primary = (220, 38, 38, 255)
    red_dark = (185, 28, 28, 255)
    red_fold = (239, 68, 68, 255)

    draw.rounded_rectangle(
        [doc_left, doc_top, doc_right, doc_bottom],
        radius=36,
        fill=red_primary
    )

    # Subtle darker bottom half for depth
    draw.rounded_rectangle(
        [doc_left, doc_top + 280, doc_right, doc_bottom],
        radius=36,
        fill=red_dark
    )

    # Folded corner triangle (cutout + flap)
    fold_x0 = doc_right - corner_fold
    fold_y0 = doc_top
    fold_x1 = doc_right
    fold_y1 = doc_top + corner_fold

    # Cutout background triangle
    draw.polygon(
        [(fold_x0, fold_y0), (fold_x1, fold_y0), (fold_x1, fold_y1)],
        fill=(0, 0, 0, 0)
    )

    # Fold flap
    draw.polygon(
        [(fold_x0, fold_y0), (fold_x0, fold_y1), (fold_x1, fold_y1)],
        fill=red_fold
    )
    # Fold crease shadow
    draw.line([(fold_x0, fold_y0), (fold_x1, fold_y1)], fill=(0, 0, 0, 50), width=4)

    # Golden/White Lightning Bolt Symbol in upper center (Symbolizing "Light / Lightning-Fast")
    # Coordinates centered around x=240, y=175
    bolt_color = (255, 240, 100, 255)
    bolt_shadow = (140, 20, 20, 180)
    bolt_coords = [
        (260, 95),   # Top
        (205, 195),  # Left inner
        (248, 195),  # Middle right
        (232, 275),  # Bottom point
        (288, 175),  # Right inner
        (244, 175),  # Middle left
    ]

    # Draw bolt shadow then bolt
    bolt_coords_shadow = [(x + 4, y + 5) for x, y in bolt_coords]
    draw.polygon(bolt_coords_shadow, fill=bolt_shadow)
    draw.polygon(bolt_coords, fill=bolt_color)

    # White pill badge across lower portion containing bold "PDF"
    badge_left = doc_left + 42
    badge_right = doc_right - 42
    badge_top = 315
    badge_bottom = 415

    draw.rounded_rectangle(
        [badge_left, badge_top, badge_right, badge_bottom],
        radius=24,
        fill=(255, 255, 255, 255)
    )

    # Try to load Arial or Segoe UI for "PDF" text
    font_loaded = False
    for font_name in ["segoeuib.ttf", "arialbd.ttf", "calibrib.ttf"]:
        try:
            font = ImageFont.truetype(f"C:\\Windows\\Fonts\\{font_name}", 72)
            font_loaded = True
            break
        except Exception:
            continue

    if not font_loaded:
        font = ImageFont.load_default()

    # Draw "PDF" in dark crimson on white badge
    text = "PDF"
    bbox = draw.textbbox((0, 0), text, font=font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]
    text_x = badge_left + (badge_right - badge_left - text_w) / 2 - bbox[0]
    text_y = badge_top + (badge_bottom - badge_top - text_h) / 2 - bbox[1]
    draw.text((text_x, text_y), text, font=font, fill=(185, 28, 28, 255))

    # Generate standard multi-resolution ICO sizes
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    icon_images = []
    for s in sizes:
        icon_images.append(img.resize(s, Image.Resampling.LANCZOS))

    # Save multi-size icon
    icon_images[-1].save(
        output_path,
        format="ICO",
        sizes=sizes,
        append_images=icon_images[:-1]
    )
    print(f"Generated multi-size icon: {output_path}")

if __name__ == "__main__":
    out_ico = os.path.join(os.path.dirname(__file__), "resources", "app.ico")
    generate_lightpdf_icon(out_ico)
