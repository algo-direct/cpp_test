#!/usr/bin/env python3
"""Convert SVG files to JPG using cairosvg and PIL"""

import os
import sys

try:
    import cairosvg
    from PIL import Image
    import io
except ImportError:
    print("Installing required packages...")
    os.system("pip3 install --user cairosvg pillow")
    import cairosvg
    from PIL import Image
    import io

def convert_svg_to_jpg(svg_file, jpg_file, quality=95):
    """Convert SVG to JPG with white background"""
    try:
        # Convert SVG to PNG in memory
        png_data = cairosvg.svg2png(url=svg_file)
        
        # Open PNG with PIL
        image = Image.open(io.BytesIO(png_data))
        
        # Convert RGBA to RGB (add white background)
        if image.mode == 'RGBA':
            rgb_image = Image.new('RGB', image.size, (255, 255, 255))
            rgb_image.paste(image, mask=image.split()[3])  # Use alpha channel as mask
            image = rgb_image
        elif image.mode != 'RGB':
            image = image.convert('RGB')
        
        # Save as JPG
        image.save(jpg_file, 'JPEG', quality=quality, optimize=True)
        print(f"✓ Converted: {svg_file} -> {jpg_file}")
        return True
    except Exception as e:
        print(f"✗ Error converting {svg_file}: {e}")
        return False

def main():
    # Get all SVG files in current directory
    svg_files = [f for f in os.listdir('.') if f.endswith('.svg')]
    
    if not svg_files:
        print("No SVG files found in current directory")
        return
    
    print(f"Found {len(svg_files)} SVG files to convert\n")
    
    success_count = 0
    for svg_file in svg_files:
        jpg_file = svg_file.replace('.svg', '.jpg')
        if convert_svg_to_jpg(svg_file, jpg_file):
            success_count += 1
    
    print(f"\n✓ Successfully converted {success_count}/{len(svg_files)} files")

if __name__ == '__main__':
    main()
