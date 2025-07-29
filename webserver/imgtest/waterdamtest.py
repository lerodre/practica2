import cv2
import numpy as np
import os
from PIL import Image
import matplotlib.pyplot as plt

def crop_center(image, target_size=512):
    """Crop the center of an image to target_size x target_size"""
    h, w = image.shape[:2]
    
    # Calculate crop coordinates for center
    center_y, center_x = h // 2, w // 2
    half_size = target_size // 2
    
    # Calculate crop boundaries
    start_y = center_y - half_size
    end_y = center_y + half_size
    start_x = center_x - half_size
    end_x = center_x + half_size
    
    # Ensure we don't exceed image boundaries
    start_y = max(0, start_y)
    start_x = max(0, start_x)
    end_y = min(h, end_y)
    end_x = min(w, end_x)
    
    # If the image is smaller than target size, pad with zeros
    cropped = image[start_y:end_y, start_x:end_x]
    
    # If cropped image is smaller than target size, resize it
    if cropped.shape[0] != target_size or cropped.shape[1] != target_size:
        cropped = cv2.resize(cropped, (target_size, target_size))
    
    return cropped

def apply_canny_edge_detection(image, low_threshold=50, high_threshold=150):
    """Apply Canny edge detection to an image"""
    # Convert to grayscale if needed
    if len(image.shape) == 3:
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    else:
        gray = image
    
    # Apply Gaussian blur to reduce noise
    blurred = cv2.GaussianBlur(gray, (5, 5), 0)
    
    # Apply Canny edge detection
    edges = cv2.Canny(blurred, low_threshold, high_threshold)
    
    return edges

def downscale_image(image, target_size=256):
    """Downscale an image to target_size x target_size while maintaining content"""
    return cv2.resize(image, (target_size, target_size), interpolation=cv2.INTER_AREA)

def save_canny_as_png(binary_image, output_path):
    """Save a binary Canny image as PNG with optimal compression"""
    # Convert numpy array to PIL Image
    pil_image = Image.fromarray(binary_image)
    
    # Save as PNG with maximum compression
    png_path = output_path.replace('.jpg', '.png')
    pil_image.save(png_path, 'PNG', optimize=True)
    
    return png_path

def get_file_size(file_path):
    """Get file size in bytes and return formatted string"""
    if not os.path.exists(file_path):
        return "File not found"
    
    size_bytes = os.path.getsize(file_path)
    
    if size_bytes < 1024:
        return f"{size_bytes} bytes"
    elif size_bytes < 1024 * 1024:
        return f"{size_bytes / 1024:.2f} KB"
    else:
        return f"{size_bytes / (1024 * 1024):.2f} MB"

def create_comparison_plot(original_img, canny_512, canny_256, canny_128, original_path, canny_512_path, canny_256_path, canny_128_path):
    """Create a comparison plot showing original and all canny images with file info"""
    fig, axes = plt.subplots(2, 4, figsize=(20, 10))
    fig.suptitle('Water Dam Image Processing Comparison', fontsize=16, fontweight='bold')
    
    # Top row - Images
    # Original image
    axes[0, 0].imshow(cv2.cvtColor(original_img, cv2.COLOR_BGR2RGB))
    axes[0, 0].set_title('Original Image (512x512)')
    axes[0, 0].axis('off')
    
    # Canny 512x512
    axes[0, 1].imshow(canny_512, cmap='gray')
    axes[0, 1].set_title('Canny Edges (512x512)')
    axes[0, 1].axis('off')
    
    # Canny 256x256
    axes[0, 2].imshow(canny_256, cmap='gray')
    axes[0, 2].set_title('Canny Edges (256x256)')
    axes[0, 2].axis('off')
    
    # Canny 128x128
    axes[0, 3].imshow(canny_128, cmap='gray')
    axes[0, 3].set_title('Canny Edges (128x128)')
    axes[0, 3].axis('off')
    
    # Bottom row - File size comparison
    file_info = [
        f"Original 512x512:\n{get_file_size(original_path)}",
        f"Canny 512x512 PNG:\n{get_file_size(canny_512_path)}",
        f"Canny 256x256 PNG:\n{get_file_size(canny_256_path)}",
        f"Canny 128x128 PNG:\n{get_file_size(canny_128_path)}"
    ]
    
    # Create text plots for file information
    colors = ["lightgreen", "lightblue", "lightcoral", "lightyellow"]
    for i, (info, color) in enumerate(zip(file_info, colors)):
        axes[1, i].text(0.5, 0.5, info, ha='center', va='center', 
                      fontsize=11, transform=axes[1, i].transAxes,
                      bbox=dict(boxstyle="round,pad=0.3", facecolor=color))
        axes[1, i].axis('off')
    
    plt.tight_layout()
    plt.savefig('water_dam_comparison.png', dpi=300, bbox_inches='tight')
    plt.show()

def main():
    """Main function to process the water dam image"""
    
    # Input image path
    input_image_path = "waterdam.jpg"
    
    print("🌊 Water Dam Image Processing Started...")
    print("=" * 50)
    
    # Check if input image exists
    if not os.path.exists(input_image_path):
        print(f"❌ Error: Input image '{input_image_path}' not found!")
        print("Please place your water dam image in the same directory and update the path.")
        return
    
    # Load the image
    print(f"📖 Loading image: {input_image_path}")
    original_image = cv2.imread(input_image_path)
    
    if original_image is None:
        print("❌ Error: Could not load the image!")
        return
    
    print(f"📏 Original image size: {original_image.shape[1]}x{original_image.shape[0]} pixels")
    
    # Step 1: Crop center to 512x512
    print("✂️  Cropping center to 512x512...")
    cropped_512 = crop_center(original_image, 512)
    
    # Save cropped original 512x512
    cropped_512_path = "water_dam_512x512.jpg"
    cv2.imwrite(cropped_512_path, cropped_512)
    print(f"💾 Saved 512x512 image: {cropped_512_path}")
    
    # Step 2: Create 256x256 version
    print("📏 Creating 256x256 version...")
    cropped_256 = downscale_image(cropped_512, 256)
    
    # Save 256x256 version
    cropped_256_path = "water_dam_256x256.jpg"
    cv2.imwrite(cropped_256_path, cropped_256)
    print(f"💾 Saved 256x256 image: {cropped_256_path}")
    
    # Step 2.5: Create 128x128 version
    print("📏 Creating 128x128 version...")
    cropped_128 = downscale_image(cropped_512, 128)
    
    # Save 128x128 version
    cropped_128_path = "water_dam_128x128.jpg"
    cv2.imwrite(cropped_128_path, cropped_128)
    print(f"💾 Saved 128x128 image: {cropped_128_path}")
    
    # Step 3: Apply Canny to 512x512
    print("� Applying Canny edge detection to 512x512...")
    canny_512 = apply_canny_edge_detection(cropped_512)
    
    # Save Canny 512x512 as PNG
    canny_512_png_path = save_canny_as_png(canny_512, "water_dam_canny_512x512.jpg")
    print(f"💾 Saved Canny 512x512 PNG: {canny_512_png_path}")
    
    # Step 4: Apply Canny to 256x256
    print("🔍 Applying Canny edge detection to 256x256...")
    canny_256 = apply_canny_edge_detection(cropped_256)
    
    # Save Canny 256x256 as PNG
    canny_256_png_path = save_canny_as_png(canny_256, "water_dam_canny_256x256.jpg")
    print(f"💾 Saved Canny 256x256 PNG: {canny_256_png_path}")
    
    # Step 5: Apply Canny to 128x128
    print("🔍 Applying Canny edge detection to 128x128...")
    canny_128 = apply_canny_edge_detection(cropped_128)
    
    # Save Canny 128x128 as PNG
    canny_128_png_path = save_canny_as_png(canny_128, "water_dam_canny_128x128.jpg")
    print(f"💾 Saved Canny 128x128 PNG: {canny_128_png_path}")
    
    # Step 6: Display comparison
    print("\n📊 File Size Comparison:")
    print("-" * 50)
    print(f"Original 512x512:       {get_file_size(cropped_512_path)}")
    print(f"Original 256x256:       {get_file_size(cropped_256_path)}")
    print(f"Original 128x128:       {get_file_size(cropped_128_path)}")
    print(f"Canny 512x512 PNG:      {get_file_size(canny_512_png_path)}")
    print(f"Canny 256x256 PNG:      {get_file_size(canny_256_png_path)}")
    print(f"Canny 128x128 PNG:      {get_file_size(canny_128_png_path)}")
    
    # Calculate compression ratios
    original_512_size = os.path.getsize(cropped_512_path)
    original_256_size = os.path.getsize(cropped_256_path)
    original_128_size = os.path.getsize(cropped_128_path)
    canny_512_size = os.path.getsize(canny_512_png_path)
    canny_256_size = os.path.getsize(canny_256_png_path)
    canny_128_size = os.path.getsize(canny_128_png_path)
    
    ratio_512 = (original_512_size - canny_512_size) / original_512_size * 100
    ratio_256 = (original_256_size - canny_256_size) / original_256_size * 100
    ratio_128 = (original_128_size - canny_128_size) / original_128_size * 100
    size_reduction_256 = (original_512_size - original_256_size) / original_512_size * 100
    size_reduction_128 = (original_512_size - original_128_size) / original_512_size * 100
    
    print(f"\nCompression ratios:")
    print(f"  512x512 → Canny PNG:     {ratio_512:.1f}%")
    print(f"  256x256 → Canny PNG:     {ratio_256:.1f}%")
    print(f"  128x128 → Canny PNG:     {ratio_128:.1f}%")
    print(f"  512x512 → 256x256:       {size_reduction_256:.1f}%")
    print(f"  512x512 → 128x128:       {size_reduction_128:.1f}%")
    
    # Step 7: Create and display comparison plot
    print("\n📊 Creating comparison visualization...")
    create_comparison_plot(cropped_512, canny_512, canny_256, canny_128,
                         cropped_512_path, canny_512_png_path, canny_256_png_path, canny_128_png_path)
    
    print("\n✅ Processing completed successfully!")
    print("📁 Output files:")
    print(f"   - {cropped_512_path}")
    print(f"   - {cropped_256_path}")
    print(f"   - {cropped_128_path}")
    print(f"   - {canny_512_png_path}")
    print(f"   - {canny_256_png_path}")
    print(f"   - {canny_128_png_path}")
    print("   - water_dam_comparison.png")

if __name__ == "__main__":
    main()
