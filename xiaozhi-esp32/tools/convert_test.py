import os
import re
import math

SOURCE_DIR = r"C:\Users\txr45\Downloads\TFT_eSPI-2.5.43\TFT_eSPI-2.5.43\examples\Generic\Animated_Eyes_2\data"
OUT_H = r"C:\Users\txr45\Documents\Eyes\ESP32-S3-DualEye-Touch-LCD-1.28\xiaozhi-esp32\main\boards\waveshare\esp32-s3-dualeye-lcd-1.28\eye_presets.h"
OUT_CC = r"C:\Users\txr45\Documents\Eyes\ESP32-S3-DualEye-Touch-LCD-1.28\xiaozhi-esp32\main\boards\waveshare\esp32-s3-dualeye-lcd-1.28\eye_presets.cc"

def parse_header(filepath):
    with open(filepath, 'r', errors='ignore') as f:
        text = f.read()
    
    defines = {}
    for m in re.finditer(r'#define\s+(\w+)\s+(\d+)', text):
        defines[m.group(1)] = int(m.group(2))
        
    arrays = {}
    for m in re.finditer(r'(?:const\s+)?(uint\d+_t)\s+(\w+)\s*\[([^\]]*)\](?:\s*PROGMEM)?\s*=\s*\{([^;]*)\};', text, re.DOTALL):
        arr_type = m.group(1)
        arr_name = m.group(2)
        arr_body = m.group(4)
        tokens = re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', arr_body)
        nums = [int(x, 16) if x.lower().startswith('0x') else int(x, 10) for x in tokens]
        arrays[arr_name] = nums
        
    return defines, arrays

print("Parsing eye headers...")
presets = [
    ("cat", "catEye.h", "Cat Eye"),
    ("dragon", "dragonEye.h", "Dragon Eye"),
    ("terminator", "terminatorEye.h", "Terminator Eye"),
    ("owl", "owlEye.h", "Owl Eye"),
    ("doe", "doeEye.h", "Doe Eye")
]

parsed_data = {}
for key, fname, display_name in presets:
    fpath = os.path.join(SOURCE_DIR, fname)
    if os.path.exists(fpath):
        defs, arrs = parse_header(fpath)
        parsed_data[key] = {
            "name": display_name,
            "defs": defs,
            "arrs": arrs
        }
        print(f"Loaded {key}: sclera={len(arrs.get('sclera', []))}, iris={len(arrs.get('iris', []))}")

print("Parsed all presets successfully.")
