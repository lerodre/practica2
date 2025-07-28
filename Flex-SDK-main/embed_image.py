#!/usr/bin/env python3
"""
Image Embedder for SCHC Image Sender
Reads an image file and embeds it into the C code array
"""

import os
import sys

def read_image_file(image_path):
    """Read image file and return binary data"""
    try:
        with open(image_path, 'rb') as f:
            return f.read()
    except FileNotFoundError:
        print(f"Error: Image file '{image_path}' not found!")
        return None
    except Exception as e:
        print(f"Error reading image file: {e}")
        return None

def generate_c_array(image_data, array_name="compressed_image"):
    """Generate C array string from binary data"""
    lines = []
    lines.append(f"static const uint8_t {array_name}[{len(image_data)}] = {{")
    
    # Generate hex values in rows of 16 bytes
    for i in range(0, len(image_data), 16):
        chunk = image_data[i:i+16]
        hex_values = ', '.join(f'0x{b:02X}' for b in chunk)
        
        # Add comment showing byte offset
        comment = f"  // Offset {i:04X}"
        lines.append(f"    {hex_values},{comment}")
    
    lines.append("};")
    return '\n'.join(lines)

def update_c_file(c_file_path, image_data):
    """Update the C file with new image data"""
    try:
        # Read current C file
        with open(c_file_path, 'r') as f:
            content = f.read()
        
        # Find the start and end of the image array
        start_marker = "static const uint8_t compressed_image[IMAGE_SIZE] = {"
        end_marker = "};"
        
        start_pos = content.find(start_marker)
        if start_pos == -1:
            print("Error: Could not find image array in C file!")
            return False
        
        # Find the end of the array (look for }; after the start)
        search_start = start_pos + len(start_marker)
        end_pos = content.find(end_marker, search_start)
        if end_pos == -1:
            print("Error: Could not find end of image array in C file!")
            return False
        
        # Include the }; in the replacement
        end_pos += len(end_marker)
        
        # Generate new array code
        new_array = generate_c_array(image_data)
        
        # Update IMAGE_SIZE define
        new_content = content.replace(
            f"#define IMAGE_SIZE {content.split('#define IMAGE_SIZE ')[1].split()[0]}",
            f"#define IMAGE_SIZE {len(image_data)}"
        )
        
        # Replace the old array with new one
        new_content = new_content[:start_pos] + new_array + new_content[end_pos:]
        
        # Write updated content back to file
        with open(c_file_path, 'w') as f:
            f.write(new_content)
        
        print(f"✅ Successfully updated {c_file_path}")
        print(f"   Image size: {len(image_data)} bytes")
        print(f"   Estimated fragments: {(len(image_data) + 18) // 19}")
        
        return True
        
    except Exception as e:
        print(f"Error updating C file: {e}")
        return False

def main():
    """Main function"""
    # Default file paths
    image_file = "lena_test6_50p_8bit_haar1.jpg"
    c_file = "send_img_test.c"
    
    # Check if image file was provided as argument
    if len(sys.argv) > 1:
        image_file = sys.argv[1]
    
    # Check if C file was provided as argument
    if len(sys.argv) > 2:
        c_file = sys.argv[2]
    
    print(f"🖼️  SCHC Image Embedder")
    print(f"   Image file: {image_file}")
    print(f"   C file: {c_file}")
    print()
    
    # Check if files exist
    if not os.path.exists(image_file):
        print(f"❌ Image file '{image_file}' not found!")
        print("Available files in current directory:")
        for f in os.listdir('.'):
            if f.lower().endswith(('.jpg', '.jpeg', '.png', '.bin', '.dat')):
                print(f"   - {f}")
        return 1
    
    if not os.path.exists(c_file):
        print(f"❌ C file '{c_file}' not found!")
        return 1
    
    # Read image data
    print(f"📖 Reading image file...")
    image_data = read_image_file(image_file)
    if image_data is None:
        return 1
    
    print(f"✅ Read {len(image_data)} bytes from image file")
    
    # Check if image is reasonable size for satellite transmission
    max_reasonable_size = 2000  # bytes
    if len(image_data) > max_reasonable_size:
        print(f"⚠️  Warning: Image is {len(image_data)} bytes")
        print(f"   This will require {(len(image_data) + 18) // 19} fragments")
        print(f"   Consider compressing further for satellite transmission")
        
        response = input("Continue anyway? (y/N): ")
        if response.lower() != 'y':
            return 1
    
    # Show image info
    estimated_fragments = (len(image_data) + 18) // 19
    sessions_needed = (estimated_fragments + 9) // 10  # 10 fragments per session
    transmission_hours = sessions_needed * 2  # 2 hours between sessions
    
    print(f"📊 Transmission Estimate:")
    print(f"   Fragments needed: {estimated_fragments}")
    print(f"   Sessions needed: {sessions_needed}")
    print(f"   Total time: ~{transmission_hours} hours")
    print()
    
    # Update C file
    print(f"✏️  Updating C file...")
    if update_c_file(c_file, image_data):
        print()
        print(f"🎉 Image successfully embedded!")
        print(f"   You can now compile and upload {c_file}")
        print()
        print("Next steps:")
        print("1. meson compile -C build")
        print("2. Upload user_application.bin to FlexSense")
        print("3. Monitor satellite transmission progress")
        return 0
    else:
        return 1

if __name__ == "__main__":
    sys.exit(main())
