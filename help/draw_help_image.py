import csv
import os
from PIL import Image, ImageDraw, ImageFont

def get_text_width(draw, text, font, char_spacing=0):
    if not text: return 0
    total_w = 0
    for char in text:
        bbox = draw.textbbox((0, 0), char, font=font)
        total_w += (bbox[2] - bbox[0]) + char_spacing
    return total_w - char_spacing

def draw_text_line(draw, xy, text, font, fill, char_spacing=0):
    x, y = xy
    for char in text:
        draw.text((x, y), char, font=font, fill=fill)
        bbox = draw.textbbox((0, 0), char, font=font)
        x += (bbox[2] - bbox[0]) + char_spacing

def draw_text(file_name, raw_text, config):
    f_path = config.get("font_path")
    f_size = config.get("font_size", 17)
    c_spacing = config.get("char_spacing", 2)
    l_spacing = config.get("line_spacing", 1.25)
    padding = config.get("padding", 0)
    output_dir = config.get("output_dir", ".")
    text_color = (255, 255, 255, 255)
    
    try:
        font = ImageFont.truetype(f_path, f_size)
    except:
        font = ImageFont.load_default()

    lines = raw_text.replace('\\n', '\n').split('\n')
    
    temp_draw = ImageDraw.Draw(Image.new('RGBA', (1, 1), (0, 0, 0, 0)))
    font_h = temp_draw.textbbox((0, 0), "Xg", font=font)[3]
    line_h = int(font_h * l_spacing)

    max_w = 0
    for line in lines:
        w = get_text_width(temp_draw, line, font, c_spacing)
        if w > max_w: max_w = w
    
    img_w = max_w + padding * 2
    img_h = len(lines) * line_h + padding * 2

    img = Image.new('RGBA', (int(img_w), int(img_h)), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    curr_y = padding
    for line in lines:
        draw_text_line(draw, (padding, curr_y), line, font, text_color, c_spacing)
        curr_y += line_h

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    output_path = os.path.join(output_dir, f"{file_name}.png")
    img.save(output_path)
    print(f"Saved: {output_path} ({int(img_w)}x{int(img_h)})")

def draw_help_image(config):
    csv_path = config.get("data_path")
    
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found.")
        return

    with open(csv_path, mode='r', encoding='utf-8-sig') as f:
        reader = csv.DictReader(f)
        for row in reader:
            if(row["name"] and row["text"]):
                draw_text(row['name'], row['text'], config)

font_config = {
    "data_path": "help_data.csv",
    "output_dir": "patch",
    "font_path": "msyh.ttc",
    "font_size": 17,
    "char_spacing": 2,
    "line_spacing": 1,
    "padding": 0
}

draw_help_image(font_config)