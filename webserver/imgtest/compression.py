import cv2
import numpy as np
import pywt
from PIL import Image
import os
import matplotlib.pyplot as plt

def get_file_size_kb(filepath):
    """Get file size in KB"""
    size_bytes = os.path.getsize(filepath)
    size_kb = size_bytes / 1024
    return size_kb

def convert_to_4bit_grayscale(image):
    """Convert 8-bit grayscale to 4-bit grayscale"""
    # Scale from 0-255 (8-bit) to 0-15 (4-bit) and back to 0-255 for display
    image_4bit = (image // 16) * 16  # Quantize to 4-bit equivalent (16 levels)
    return image_4bit.astype(np.uint8)

def convert_to_6bit_grayscale(image):
    """Convert 8-bit grayscale to 6-bit grayscale"""
    # Scale from 0-255 (8-bit) to 0-63 (6-bit) and back to 0-255 for display
    image_6bit = (image // 4) * 4  # Quantize to 6-bit equivalent
    return image_6bit.astype(np.uint8)

def resize_to_240p(image):
    """Resize image to 240p maintaining aspect ratio"""
    height, width = image.shape
    target_height = 240
    target_width = int((target_height * width) / height)
    
    # If calculated width is less than 426, use 426 and adjust height
    if target_width < 426:
        target_width = 426
        target_height = int((target_width * height) / width)
    
    resized_image = cv2.resize(image, (target_width, target_height), interpolation=cv2.INTER_LANCZOS4)
    return resized_image, target_width, target_height

def resize_to_144p(image):
    """Resize image to 144p maintaining aspect ratio"""
    height, width = image.shape
    target_height = 144
    target_width = int((target_height * width) / height)
    
    # If calculated width is less than 256, use 256 and adjust height
    if target_width < 256:
        target_width = 256
        target_height = int((target_width * height) / width)
    
    resized_image = cv2.resize(image, (target_width, target_height), interpolation=cv2.INTER_LANCZOS4)
    return resized_image, target_width, target_height

def resize_to_50p(image):
    """Resize image to 50p maintaining aspect ratio"""
    height, width = image.shape
    target_height = 50
    target_width = int((target_height * width) / height)
    
    # If calculated width is less than 89, use 89 and adjust height
    if target_width < 89:
        target_width = 89
        target_height = int((target_width * height) / width)
    
    resized_image = cv2.resize(image, (target_width, target_height), interpolation=cv2.INTER_LANCZOS4)
    return resized_image, target_width, target_height

def save_image_with_quality(image, filepath, quality):
    """Save image with specific JPEG quality"""
    if image.dtype != np.uint8:
        image = np.clip(image, 0, 255).astype(np.uint8)
    
    # Convert grayscale to RGB for JPEG saving
    if len(image.shape) == 2:
        image_rgb = cv2.cvtColor(image, cv2.COLOR_GRAY2RGB)
    else:
        image_rgb = image
    
    pil_image = Image.fromarray(image_rgb)
    pil_image.save(filepath, 'JPEG', quality=quality, optimize=True)

def haar_wavelet_compress(image, compression_level=3):
    """Apply Haar wavelet compression with specified level"""
    # Convert to float for wavelet transform
    image_float = image.astype(np.float32)
    
    # Apply 2D Haar wavelet transform
    coeffs = pywt.dwt2(image_float, 'haar')
    cA, (cH, cV, cD) = coeffs
    
    # Apply compression based on level
    if compression_level == 0:
        # Level 0 = no compression, just return original image
        return image.astype(np.uint8)
    elif compression_level == 1:
        # Level 1 = light compression
        threshold = 0.1 * np.max(np.abs(cA))
    elif compression_level == 2:
        # Level 2 = medium compression
        threshold = 0.3 * np.max(np.abs(cA))
    else:
        # Level 3+ = heavy compression
        threshold = 0.5 * np.max(np.abs(cA))
    
    # Zero out small coefficients
    cH = pywt.threshold(cH, threshold, mode='soft')
    cV = pywt.threshold(cV, threshold, mode='soft')
    cD = pywt.threshold(cD, threshold, mode='soft')
    
    # Reconstruct image
    coeffs_compressed = (cA, (cH, cV, cD))
    reconstructed = pywt.idwt2(coeffs_compressed, 'haar')
    
    # Ensure values are in valid range
    reconstructed = np.clip(reconstructed, 0, 255)
    
    return reconstructed.astype(np.uint8)

def process_image(input_path):
    """Main image processing function"""
    print("=" * 60)
    print("SIMPLIFIED COMPRESSION TESTS")
    print("=" * 60)
    
    # Check if input file exists
    if not os.path.exists(input_path):
        print(f"Error: Input file '{input_path}' not found!")
        return
    
    # Load original image
    original_size_kb = get_file_size_kb(input_path)
    image = cv2.imread(input_path, cv2.IMREAD_GRAYSCALE)
    if image is None:
        print(f"Error: Could not load image from '{input_path}'")
        return
    
    print(f"📁 Original image: {os.path.basename(input_path)}")
    print(f"📐 Original size: {image.shape[1]}x{image.shape[0]} pixels")
    print(f"📊 Original file size: {original_size_kb:.2f} KB")
    
    base_name = os.path.splitext(input_path)[0]
    results = []
    
    # TEST 1: 240p + 4-bit grayscale + Haar 3
    print(f"\n🧪 TEST 1: 240p + 4-bit grayscale + Haar 3")
    print("-" * 40)
    
    # Convert to 4-bit grayscale
    image_4bit = convert_to_4bit_grayscale(image)
    
    # Resize to 240p
    resized_240p, width_240p, height_240p = resize_to_240p(image_4bit)
    
    # Apply Haar level 3 compression
    compressed_test1 = haar_wavelet_compress(resized_240p)
    
    # Save result
    test1_path = f"{base_name}_test1_240p_4bit_haar3.jpg"
    save_image_with_quality(compressed_test1, test1_path, 30)
    test1_size_kb = get_file_size_kb(test1_path)
    test1_ratio = (original_size_kb / test1_size_kb) if test1_size_kb > 0 else 0
    
    results.append({
        'name': 'Test 1: 240p + 4-bit + Haar3',
        'path': test1_path,
        'size_kb': test1_size_kb,
        'ratio': test1_ratio,
        'image': compressed_test1,
        'description': f"240p ({width_240p}x{height_240p}), 4-bit grayscale"
    })
    
    print(f"✅ Completed Test 1")
    print(f"   📐 Dimensions: {width_240p}x{height_240p} pixels")
    print(f"   🎨 Color depth: 4-bit grayscale (16 levels)")
    print(f"   📁 Size: {test1_size_kb:.2f} KB")
    print(f"   🗜️  Compression ratio: {test1_ratio:.1f}x")
    print(f"   💾 Saved as: {os.path.basename(test1_path)}")
    
    # TEST 2: 144p + 6-bit grayscale + Haar 3
    print(f"\n🧪 TEST 2: 144p + 6-bit grayscale + Haar 3")
    print("-" * 40)
    
    # Convert to 6-bit grayscale
    image_6bit = convert_to_6bit_grayscale(image)
    
    # Resize to 144p
    resized_144p, width_144p, height_144p = resize_to_144p(image_6bit)
    
    # Apply Haar level 3 compression
    compressed_test2 = haar_wavelet_compress(resized_144p)
    
    # Save result
    test2_path = f"{base_name}_test2_144p_6bit_haar3.jpg"
    save_image_with_quality(compressed_test2, test2_path, 25)
    test2_size_kb = get_file_size_kb(test2_path)
    test2_ratio = (original_size_kb / test2_size_kb) if test2_size_kb > 0 else 0
    
    results.append({
        'name': 'Test 2: 144p + 6-bit + Haar3',
        'path': test2_path,
        'size_kb': test2_size_kb,
        'ratio': test2_ratio,
        'image': compressed_test2,
        'description': f"144p ({width_144p}x{height_144p}), 6-bit grayscale"
    })
    
    print(f"✅ Completed Test 2")
    print(f"   📐 Dimensions: {width_144p}x{height_144p} pixels")
    print(f"   🎨 Color depth: 6-bit grayscale (64 levels)")
    print(f"   📁 Size: {test2_size_kb:.2f} KB")
    print(f"   🗜️  Compression ratio: {test2_ratio:.1f}x")
    print(f"   💾 Saved as: {os.path.basename(test2_path)}")
    
    # TEST 3: 144p + 8-bit grayscale + Haar 3
    print(f"\n🧪 TEST 3: 144p + 8-bit grayscale + Haar 3")
    print("-" * 40)
    
    # Use original 8-bit grayscale
    # Resize to 144p
    resized_144p_8bit, width_144p_8bit, height_144p_8bit = resize_to_144p(image)
    
    # Apply Haar level 3 compression
    compressed_test3 = haar_wavelet_compress(resized_144p_8bit)
    
    # Save result
    test3_path = f"{base_name}_test3_144p_8bit_haar3.jpg"
    save_image_with_quality(compressed_test3, test3_path, 20)
    test3_size_kb = get_file_size_kb(test3_path)
    test3_ratio = (original_size_kb / test3_size_kb) if test3_size_kb > 0 else 0
    
    results.append({
        'name': 'Test 3: 144p + 8-bit + Haar3',
        'path': test3_path,
        'size_kb': test3_size_kb,
        'ratio': test3_ratio,
        'image': compressed_test3,
        'description': f"144p ({width_144p_8bit}x{height_144p_8bit}), 8-bit grayscale"
    })
    
    print(f"✅ Completed Test 3")
    print(f"   📐 Dimensions: {width_144p_8bit}x{height_144p_8bit} pixels")
    print(f"   🎨 Color depth: 8-bit grayscale (256 levels)")
    print(f"   📁 Size: {test3_size_kb:.2f} KB")
    print(f"   🗜️  Compression ratio: {test3_ratio:.1f}x")
    print(f"   💾 Saved as: {os.path.basename(test3_path)}")
    
    # TEST 4: 144p + 4-bit grayscale + Haar 3
    print(f"\n🧪 TEST 4: 144p + 4-bit grayscale + Haar 3")
    print("-" * 40)
    
    # Convert to 4-bit grayscale
    image_4bit_test4 = convert_to_4bit_grayscale(image)
    
    # Resize to 144p
    resized_144p_4bit, width_144p_4bit, height_144p_4bit = resize_to_144p(image_4bit_test4)
    
    # Apply Haar level 3 compression
    compressed_test4 = haar_wavelet_compress(resized_144p_4bit)
    
    # Save result
    test4_path = f"{base_name}_test4_144p_4bit_haar3.jpg"
    save_image_with_quality(compressed_test4, test4_path, 15)
    test4_size_kb = get_file_size_kb(test4_path)
    test4_ratio = (original_size_kb / test4_size_kb) if test4_size_kb > 0 else 0
    
    results.append({
        'name': 'Test 4: 144p + 4-bit + Haar3',
        'path': test4_path,
        'size_kb': test4_size_kb,
        'ratio': test4_ratio,
        'image': compressed_test4,
        'description': f"144p ({width_144p_4bit}x{height_144p_4bit}), 4-bit grayscale"
    })
    
    print(f"✅ Completed Test 4")
    print(f"   📐 Dimensions: {width_144p_4bit}x{height_144p_4bit} pixels")
    print(f"   🎨 Color depth: 4-bit grayscale (16 levels)")
    print(f"   📁 Size: {test4_size_kb:.2f} KB")
    print(f"   🗜️  Compression ratio: {test4_ratio:.1f}x")
    print(f"   💾 Saved as: {os.path.basename(test4_path)}")
    
    # TEST 5: 50p + 8-bit grayscale + Haar 2
    print(f"\n🧪 TEST 5: 50p + 8-bit grayscale + Haar 2")
    print("-" * 40)
    
    # Use original 8-bit grayscale
    # Resize to 50p
    resized_50p, width_50p, height_50p = resize_to_50p(image)
    
    # Apply Haar level 2 compression (medium compression)
    compressed_test5 = haar_wavelet_compress(resized_50p, compression_level=2)
    
    # Save result
    test5_path = f"{base_name}_test5_50p_8bit_haar2.jpg"
    save_image_with_quality(compressed_test5, test5_path, 10)
    test5_size_kb = get_file_size_kb(test5_path)
    test5_ratio = (original_size_kb / test5_size_kb) if test5_size_kb > 0 else 0
    
    results.append({
        'name': 'Test 5: 50p + 8-bit + Haar2',
        'path': test5_path,
        'size_kb': test5_size_kb,
        'ratio': test5_ratio,
        'image': compressed_test5,
        'description': f"50p ({width_50p}x{height_50p}), 8-bit grayscale"
    })
    
    print(f"✅ Completed Test 5")
    print(f"   📐 Dimensions: {width_50p}x{height_50p} pixels")
    print(f"   🎨 Color depth: 8-bit grayscale (256 levels)")
    print(f"   🔧 Wavelet: Haar level 2 (medium compression)")
    print(f"   📁 Size: {test5_size_kb:.2f} KB")
    print(f"   🗜️  Compression ratio: {test5_ratio:.1f}x")
    print(f"   💾 Saved as: {os.path.basename(test5_path)}")
    
    # TEST 6: 50p + 8-bit grayscale + Haar 1
    print(f"\n🧪 TEST 6: 50p + 8-bit grayscale + Haar 1")
    print("-" * 40)
    
    # Use original 8-bit grayscale
    # Resize to 50p (reuse the same resized image from TEST 5)
    
    # Apply Haar level 1 compression (light compression)
    compressed_test6 = haar_wavelet_compress(resized_50p, compression_level=1)
    
    # Save result
    test6_path = f"{base_name}_test6_50p_8bit_haar1.jpg"
    save_image_with_quality(compressed_test6, test6_path, 15)
    test6_size_kb = get_file_size_kb(test6_path)
    test6_ratio = (original_size_kb / test6_size_kb) if test6_size_kb > 0 else 0
    
    results.append({
        'name': 'Test 6: 50p + 8-bit + Haar1',
        'path': test6_path,
        'size_kb': test6_size_kb,
        'ratio': test6_ratio,
        'image': compressed_test6,
        'description': f"50p ({width_50p}x{height_50p}), 8-bit grayscale"
    })
    
    print(f"✅ Completed Test 6")
    print(f"   📐 Dimensions: {width_50p}x{height_50p} pixels")
    print(f"   🎨 Color depth: 8-bit grayscale (256 levels)")
    print(f"   🔧 Wavelet: Haar level 1 (light compression)")
    print(f"   📁 Size: {test6_size_kb:.2f} KB")
    print(f"   🗜️  Compression ratio: {test6_ratio:.1f}x")
    print(f"   💾 Saved as: {os.path.basename(test6_path)}")
    
    # TEST 7: 50p + 8-bit grayscale + Haar 0 (no compression)
    print(f"\n🧪 TEST 7: 50p + 8-bit grayscale + Haar 0")
    print("-" * 40)
    
    # Use original 8-bit grayscale
    # Resize to 50p (reuse the same resized image from TEST 5)
    
    # Apply Haar level 0 compression (no compression, just resize)
    compressed_test7 = haar_wavelet_compress(resized_50p, compression_level=0)
    
    # Save result
    test7_path = f"{base_name}_test7_50p_8bit_haar0.jpg"
    save_image_with_quality(compressed_test7, test7_path, 20)
    test7_size_kb = get_file_size_kb(test7_path)
    test7_ratio = (original_size_kb / test7_size_kb) if test7_size_kb > 0 else 0
    
    results.append({
        'name': 'Test 7: 50p + 8-bit + Haar0',
        'path': test7_path,
        'size_kb': test7_size_kb,
        'ratio': test7_ratio,
        'image': compressed_test7,
        'description': f"50p ({width_50p}x{height_50p}), 8-bit grayscale"
    })
    
    print(f"✅ Completed Test 7")
    print(f"   📐 Dimensions: {width_50p}x{height_50p} pixels")
    print(f"   🎨 Color depth: 8-bit grayscale (256 levels)")
    print(f"   🔧 Wavelet: Haar level 0 (no compression)")
    print(f"   📁 Size: {test7_size_kb:.2f} KB")
    print(f"   🗜️  Compression ratio: {test7_ratio:.1f}x")
    print(f"   💾 Saved as: {os.path.basename(test7_path)}")
    
    # SUMMARY
    print(f"\n📊 COMPRESSION TEST SUMMARY:")
    print("=" * 60)
    print(f"Original size: {original_size_kb:.2f} KB")
    print()
    
    for result in results:
        print(f"{result['name']:25}: {result['size_kb']:6.2f} KB ({result['ratio']:4.1f}x compression)")
    
    # Find the best compression
    best_result = min(results, key=lambda x: x['size_kb'])
    print(f"\n🏆 Best compression: {best_result['name']} ({best_result['size_kb']:.2f} KB)")
    
    # Create comparison visualization
    create_test_comparison(input_path, results)

def create_test_comparison(original_path, results):
    """Create a visual comparison of all test results"""
    fig, axes = plt.subplots(3, 3, figsize=(18, 15))
    fig.suptitle('Compression Test Comparison', fontsize=16, fontweight='bold')
    
    # Original
    original = cv2.imread(original_path, cv2.IMREAD_GRAYSCALE)
    axes[0, 0].imshow(original, cmap='gray')
    axes[0, 0].set_title(f'Original\n{original.shape[1]}x{original.shape[0]}\n{get_file_size_kb(original_path):.1f} KB')
    axes[0, 0].axis('off')
    
    # Test results - arrange in 3x3 grid
    positions = [(0, 1), (0, 2), (1, 0), (1, 1), (1, 2), (2, 0), (2, 1)]
    
    for i, (result, pos) in enumerate(zip(results, positions)):
        axes[pos].imshow(result['image'], cmap='gray')
        axes[pos].set_title(f"{result['name']}\n{result['description']}\n{result['size_kb']:.1f} KB ({result['ratio']:.1f}x)")
        axes[pos].axis('off')
    
    # Hide unused subplot
    axes[2, 2].axis('off')
    
    plt.tight_layout()
    
    # Save comparison
    base_name = os.path.splitext(original_path)[0]
    comparison_path = f"{base_name}_test_comparison.png"
    plt.savefig(comparison_path, dpi=300, bbox_inches='tight')
    print(f"📈 Test comparison saved as: {os.path.basename(comparison_path)}")
    plt.close()

if __name__ == "__main__":
    # Use the Lena image
    input_image = "webserver/imgtest/lena.jpg"
    
    # Check if the image exists
    if not os.path.exists(input_image):
        print(f"❌ Error: '{input_image}' not found in current directory!")
        print("Please make sure 'lena.jpg' is in the same folder as this script.")
        exit(1)
    
    # Process the image
    process_image(input_image)
