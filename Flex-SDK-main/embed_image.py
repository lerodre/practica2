#!/usr/bin/env python3
"""
Image Embedder for SCHC Image Sender
Reads an image file and embeds it into the C code array
"""

import os
import sys

def read_image_file(image_path):
    """Leer archivo de imagen y retornar datos binarios"""
    try:
        with open(image_path, 'rb') as f:
            return f.read()
    except FileNotFoundError:
        print(f"Error: Archivo de imagen '{image_path}' no encontrado!")
        return None
    except Exception as e:
        print(f"Error leyendo archivo de imagen: {e}")
        return None

def generate_c_array(image_data, array_name="compressed_image"):
    """Generar cadena de array en C a partir de datos binarios"""
    lines = []
    lines.append(f"static const uint8_t {array_name}[{len(image_data)}] = {{")
    
    # Generate hex values in rows of 16 bytes
    for i in range(0, len(image_data), 16):
        chunk = image_data[i:i+16]
        hex_values = ', '.join(f'0x{b:02X}' for b in chunk)
        
        # Agregar comentario mostrando offset de bytes
        comment = f"  // Offset {i:04X}"
        lines.append(f"    {hex_values},{comment}")
    
    lines.append("};")
    return '\n'.join(lines)

def update_c_file(c_file_path, image_data):
    """Actualizar el archivo C con nuevos datos de imagen"""
    try:
        # Leer archivo C actual
        with open(c_file_path, 'r') as f:
            content = f.read()
        
        # Encontrar el inicio y fin del array de imagen
        # Buscar diferentes variantes del marcador
        start_markers = [
            "static const uint8_t compressed_image[IMAGE_SIZE] = {",
            "static const uint8_t compressed_image[" + str(len(image_data)) + "] = {",
        ]
        
        # Buscar también por patrón más flexible
        import re
        pattern = r"static const uint8_t compressed_image\[\d+\] = \{"
        match = re.search(pattern, content)
        
        start_pos = -1
        if match:
            start_pos = match.start()
            start_marker = match.group()
        else:
            # Intentar con los marcadores fijos
            for marker in start_markers:
                start_pos = content.find(marker)
                if start_pos != -1:
                    start_marker = marker
                    break
        
        if start_pos == -1:
            print("Error: No se pudo encontrar el array de imagen en el archivo C!")
            print("Buscando patrones:")
            for marker in start_markers:
                print(f"   - {marker}")
            print(f"   - Patrón regex: {pattern}")
            return False
        
        end_marker = "};"
        
        # Encontrar el final del array (buscar }; después del inicio)
        search_start = start_pos + len(start_marker)
        end_pos = content.find(end_marker, search_start)
        if end_pos == -1:
            print("Error: No se pudo encontrar el final del array de imagen en el archivo C!")
            return False
        
        # Incluir el }; en el reemplazo
        end_pos += len(end_marker)
        
        # Generar nuevo código de array
        new_array = generate_c_array(image_data)
        
        # Actualizar definición de IMAGE_SIZE (buscar y reemplazar)
        import re
        image_size_pattern = r"#define IMAGE_SIZE \d+"
        new_image_size_define = f"#define IMAGE_SIZE {len(image_data)}"
        
        if re.search(image_size_pattern, content):
            new_content = re.sub(image_size_pattern, new_image_size_define, content)
        else:
            # Si no encuentra #define IMAGE_SIZE, agregarlo antes del array
            new_content = content
            print("Advertencia: No se encontró #define IMAGE_SIZE, se mantendrá el tamaño actual del array")
        
        # Reemplazar el array antiguo con el nuevo
        new_content = new_content[:start_pos] + new_array + new_content[end_pos:]
        
        # Escribir contenido actualizado de vuelta al archivo
        with open(c_file_path, 'w') as f:
            f.write(new_content)
        
        print(f"Archivo {c_file_path} actualizado exitosamente")
        print(f"   Tamaño de imagen: {len(image_data)} bytes")
        print(f"   Fragmentos estimados: {(len(image_data) + 18) // 19}")
        
        return True
        
    except Exception as e:
        print(f"Error actualizando archivo C: {e}")
        return False

def main():
    """Función principal"""
    # Rutas de archivos por defecto
    image_file = "cameraman_test5_50p_8bit_haar2.jpg"
    c_file = "send_img_test.c"
    
    # Verificar si se proporcionó archivo de imagen como argumento
    if len(sys.argv) > 1:
        image_file = sys.argv[1]
    
    # Verificar si se proporcionó archivo C como argumento
    if len(sys.argv) > 2:
        c_file = sys.argv[2]
    
    print(f" Cargador de Imagenes")
    print(f"   Archivo de imagen: {image_file}")
    print(f"   Archivo C: {c_file}")
    print()
    
    # Verificar si los archivos existen
    if not os.path.exists(image_file):
        print(f"Archivo de imagen '{image_file}' no encontrado!")
        print("Archivos disponibles en el directorio actual:")
        for f in os.listdir('.'):
            if f.lower().endswith(('.jpg', '.jpeg', '.png', '.bin', '.dat')):
                print(f"   - {f}")
        return 1
    
    if not os.path.exists(c_file):
        print(f"Archivo C '{c_file}' no encontrado!")
        return 1
    
    # Leer datos de imagen
    print(f"Leyendo archivo de imagen...")
    image_data = read_image_file(image_file)
    if image_data is None:
        return 1
    
    print(f"Leidos {len(image_data)} bytes del archivo de imagen")
    
    # Verificar si la imagen tiene un tamaño razonable para transmisión satelital
    max_reasonable_size = 2000  # bytes
    if len(image_data) > max_reasonable_size:
        print(f"Advertencia: La imagen tiene {len(image_data)} bytes")
        print(f"   Esto requerirá {(len(image_data) + 18) // 19} fragmentos")
        print(f"   Considera comprimir más para transmisión satelital")
        
        response = input("Continuar de todas formas? (s/N): ")
        if response.lower() != 's':
            return 1
    
    # Mostrar información de imagen
    estimated_fragments = (len(image_data) + 18) // 19  # 19 bytes por fragmento normal
    days_needed = (estimated_fragments + 19) // 20  # 20 fragmentos por día
    
    print(f"Estimación de Transmisión:")
    print(f"   Fragmentos necesarios: {estimated_fragments}")
    print(f"   Días necesarios: {days_needed}")
    print(f"   Fragmentos por día: 20 (límite)")
    print(f"   Bytes por fragmento: ~19 (promedio)")
    print()
    
    # Actualizar archivo C
    print(f"Actualizando archivo C...")
    if update_c_file(c_file, image_data):
        print()
        print(f"Imagen embebida exitosamente!")
        print(f"   Ahora puedes compilar y subir {c_file}")
        print()
        print("Siguientes pasos:")
        print("1. meson compile -C build")
        print("2. Subir user_application.bin a FlexSense")
        print("3. Monitorear progreso de transmisión satelital")
        return 0
    else:
        return 1

if __name__ == "__main__":
    sys.exit(main())
