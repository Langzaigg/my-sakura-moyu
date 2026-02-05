import re
import os
import argparse
from pathlib import Path

OFFSET_X = 640               
OFFSET_Y = -1000             

def get_styles(lines):
    styles_dict = {}
    is_style_section = False
    for line in lines:
        if '[V4+ Styles]' in line:
            is_style_section = True
            continue
        if is_style_section and line.startswith('Style:'):
            parts = line.replace('Style: ', '').split(',')
            name = parts[0].strip()

            primary = parts[3].strip().replace('&H00', '&H')
            if not primary.endswith('&'): primary += '&'
            outline = parts[5].strip().replace('&H00', '&H')
            if not outline.endswith('&'): outline += '&'
            shadow = parts[6].strip().replace('&H00', '&H')
            if not shadow.endswith('&'): shadow += '&'

            out_w = float(parts[16].strip())
            shad_w = float(parts[17].strip())
            
            styles_dict[name] = {
                'primary': primary, 'outline': outline, 'shadow': shadow,
                'out_w': out_w, 'shad_w': shad_w
            }
        elif is_style_section and line.startswith('['):
            is_style_section = False
    return styles_dict

def clean_noise(t):
    noise = [
        r'\\bord\d+(\.\d+)?', r'\\blur\d+(\.\d+)?', r'\\be\d+(\.\d+)?',
        r'\\c&H[0-9A-Fa-f]+&', r'\\3c&H[0-9A-Fa-f]+&', r'\\4c&H[0-9A-Fa-f]+&',
        r'\\1a&H[0-9A-Fa-f]+&', r'\\3a&H[0-9A-Fa-f]+&', r'\\4a&H[0-9A-Fa-f]+&',
        r'\\shad\d+(\.\d+)?', r'\\alpha&H[0-9A-Fa-f]+&'
    ]
    for n in noise:
        t = re.sub(n, '', t)
    return t.replace('\\\\', '\\').replace('{}', '')

def fix_line(line, styles_map):
    if 'Dialogue:' not in line or r'\fshp' not in line:
        return [line]

    parts = line.split(',', 9)
    if len(parts) < 10: return [line]
    
    style_name = parts[3].strip()
    original_text = parts[9]
    style_cfg = styles_map.get(style_name, {
        'primary': '&HFFFFFF&', 'outline': '&HAA88F1&', 'shadow': '&HB89EF2&', 
        'out_w': 1.0, 'shad_w': 1.0
    })

    pos_match = re.search(r'\\pos\(([-]?\d+),([-]?\d+)\)', line)
    if not pos_match: return [line]
    orig_x, orig_y = int(pos_match.group(1)), int(pos_match.group(2))

    base_shad_y = orig_y - OFFSET_Y
    base_shad_x = orig_x - OFFSET_X
    shad_w = style_cfg['shad_w']

    common = f"\\1a&H00&\\3a&H00&\\4a&H00&"

    def transform_fshp_shad(match):
        val = float(match.group(1))
        new_xshad = base_shad_x - val + shad_w
        return f"\\xshad{new_xshad:g}"

    t_glow = re.sub(r'\\fshp([-]?\d+(\.\d+)?)', transform_fshp_shad, original_text)
    t_glow = re.sub(r'\\pos\([-]?\d+,[-]?\d+\)', lambda m: f"\\pos({OFFSET_X},{OFFSET_Y})", t_glow)
    
    raw_tags_glow = re.search(r'\{.*?\}', t_glow).group(0)
    base_tags_glow = clean_noise(raw_tags_glow)
    content = t_glow.replace(raw_tags_glow, '')
    line_glow = ",".join(parts[:9]) + "," + base_tags_glow.replace('{', f'{{{common}\\yshad{base_shad_y + shad_w:g}\\bord{style_cfg["out_w"]}\\blur1.5\\4c{style_cfg["shadow"]}') + content

    def transform_fshp_main(match):
        val = float(match.group(1))
        new_xshad = base_shad_x - val
        return f"\\xshad{new_xshad:g}"

    t_main = re.sub(r'\\fshp([-]?\d+(\.\d+)?)', transform_fshp_main, original_text)
    t_main = re.sub(r'\\pos\([-]?\d+,[-]?\d+\)', lambda m: f"\\pos({OFFSET_X},{OFFSET_Y})", t_main)
    raw_tags_main = re.search(r'\{.*?\}', t_main).group(0)
    base_tags_main = clean_noise(raw_tags_main)

    tags_outline = base_tags_main.replace('{', f'{{{common}\\yshad{base_shad_y:g}\\bord{style_cfg["out_w"]}\\blur0.5\\4c{style_cfg["outline"]}')
    line_outline = ",".join(parts[:9]).replace('Dialogue: 0', 'Dialogue: 1') + "," + tags_outline + content

    tags_fill = base_tags_main.replace('{', f'{{{common}\\yshad{base_shad_y:g}\\bord0\\blur0\\4c{style_cfg["primary"]}')
    line_fill = ",".join(parts[:9]).replace('Dialogue: 0', 'Dialogue: 2') + "," + tags_fill + content

    return [line_glow, line_outline, line_fill]

def process(input_file, output_file):
    try:
        with open(input_file, 'r', encoding='utf-8-sig') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"读取失败: {e}")
        return

    styles_map = get_styles(lines)
    result = []
    for line in lines:
        if 'Dialogue:' in line and r'\fshp' in line:
            result.extend(fix_line(line, styles_map))
        else:
            result.append(line)

    output_path = Path(output_file)
    if output_path.parent: output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, 'w', encoding='utf-8-sig') as f:
        f.writelines(result)
    print(f"处理完成: {output_path}")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    args = parser.parse_args()

    inp, out = Path(args.input), Path(args.output)
    if inp.is_file():
        process(inp, out)
    else:
        out.mkdir(parents=True, exist_ok=True)
        for f in inp.glob("*.ass"):
            process(f, out / f.name)

if __name__ == '__main__':
    main()