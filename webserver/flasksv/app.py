from flask import Flask, request, render_template, jsonify, send_file
from datetime import datetime
import os
import json
import urllib.parse
import shutil

app = Flask(__name__)

# Ngrok Configuration
NGROK_DOMAIN = "https://cougar-factual-osprey.ngrok-free.app"

# Flask configuration for ngrok compatibility
app.config['SERVER_NAME'] = None  # Allow any host
app.config['PREFERRED_URL_SCHEME'] = 'https'

# Directories for organized data storage - relative to Flask app directory
BASE_DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sensor_data")
os.makedirs(BASE_DATA_DIR, exist_ok=True)

def organize_sensor_data(device_id, sensor_id, data):
    """Organize incoming sensor data into proper folder structure"""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    
    # Create device folder if it doesn't exist
    device_folder = os.path.join(BASE_DATA_DIR, device_id)
    os.makedirs(device_folder, exist_ok=True)
    
    # Determine sensor folder - use sensor_id directly without "sensor" prefix
    if sensor_id:
        sensor_folder = os.path.join(device_folder, sensor_id)
    else:
        sensor_folder = os.path.join(device_folder, "temporal_sensor")
    
    os.makedirs(sensor_folder, exist_ok=True)
    
    # Save data
    filename = f"data_{timestamp}.json"
    filepath = os.path.join(sensor_folder, filename)
    
    with open(filepath, 'w') as f:
        json.dump(data, f, indent=2)
    
    print(f"✔️ Datos guardados en: {filepath}")
    return filepath

def parse_flexsense_data(terminal_id, hex_value):
    """
    Parse FlexSense device data from TerminalId and hex Value
    """
    try:
        # Use TerminalId as the base device identifier
        device_id = f"flex_{terminal_id[-8:]}" if len(terminal_id) >= 8 else f"flex_{terminal_id}"
        
        # Try to identify sensor type from hex data patterns
        if hex_value and len(hex_value) >= 2:
            first_byte = hex_value[:2]
            # Simple sensor type detection based on first byte
            sensor_mapping = {
                "01": "temperature", "02": "humidity", "03": "pressure",
                "04": "battery", "05": "gps", "06": "accelerometer"
            }
            sensor_id = sensor_mapping.get(first_byte, "sensor_generic")
        else:
            sensor_id = "sensor_unknown"
            
        return device_id, sensor_id
        
    except Exception as e:
        print(f"Error analizando datos FlexSense: {e}")
        return f"flex_{terminal_id}", "sensor_error"

def decode_sensor_value(hex_value, sensor_type):
    """
    Attempt to decode sensor values from hex data
    """
    try:
        if not hex_value or len(hex_value) < 4:
            return {"raw_hex": hex_value, "error": "insufficient_data"}
            
        # Convert hex to bytes
        byte_data = bytes.fromhex(hex_value)
        decoded = {"raw_hex": hex_value, "raw_bytes": len(byte_data)}
        
        if sensor_type == "temperature" and len(byte_data) >= 3:
            # Example: temperature in 0.1°C resolution
            temp_raw = int.from_bytes(byte_data[1:3], 'big')
            decoded["temperature_celsius"] = temp_raw / 10.0
            
        elif sensor_type == "humidity" and len(byte_data) >= 2:
            # Example: humidity in % resolution
            humidity_raw = byte_data[1]
            decoded["humidity_percent"] = humidity_raw
            
        elif sensor_type == "pressure" and len(byte_data) >= 4:
            # Example: pressure in Pa
            pressure_raw = int.from_bytes(byte_data[1:4], 'big')
            decoded["pressure_pa"] = pressure_raw
            
        elif sensor_type == "battery" and len(byte_data) >= 3:
            # Example: battery voltage in mV
            voltage_raw = int.from_bytes(byte_data[1:3], 'big')
            decoded["battery_mv"] = voltage_raw
            
        return decoded
        
    except Exception as e:
        print(f"Error decodificando valor del sensor: {e}")
        return {"raw_hex": hex_value, "decode_error": str(e)}

def extract_device_sensor_info(data):
    """Extract device and sensor information from incoming data"""
    device_id = None
    sensor_id = None
    
    # Try to extract device ID from various possible fields
    possible_device_fields = ['device_id', 'module_id', 'flex_id', 'device', 'id', 'TerminalId']
    for field in possible_device_fields:
        if field in data and data[field]:
            device_id = str(data[field])
            break
    
    # Try to extract sensor ID
    possible_sensor_fields = ['sensor_id', 'sensor', 'sensor_type', 'port']
    for field in possible_sensor_fields:
        if field in data and data[field]:
            sensor_id = str(data[field])
            break
    
    # If no device ID found, create a generic one
    if not device_id:
        device_id = "unknown_device"
    
    return device_id, sensor_id

def delete_device_data(device_id):
    """Delete all data for a device including folders"""
    try:
        import shutil
        device_path = os.path.join(BASE_DATA_DIR, device_id)
        if os.path.exists(device_path):
            shutil.rmtree(device_path)
            print(f"Datos de dispositivo eliminados: {device_path}")
        return True
    except Exception as e:
        print(f"Error eliminando dispositivo {device_id}: {e}")
        return False

def delete_sensor_data(device_id, sensor_id):
    """Delete all data for a specific sensor"""
    try:
        import shutil
        sensor_path = os.path.join(BASE_DATA_DIR, device_id, sensor_id)
        if os.path.exists(sensor_path):
            shutil.rmtree(sensor_path)
            print(f"Datos de sensor eliminados: {sensor_path}")
            return True
        return False
    except Exception as e:
        print(f"Error eliminando sensor {device_id}/{sensor_id}: {e}")
        return False

def rename_device(old_device_id, new_device_id):
    """Rename a device folder"""
    try:
        old_path = os.path.join(BASE_DATA_DIR, old_device_id)
        new_path = os.path.join(BASE_DATA_DIR, new_device_id)
        if os.path.exists(old_path):
            os.rename(old_path, new_path)
            print(f"Dispositivo renombrado: {old_device_id} -> {new_device_id}")
        return True
    except Exception as e:
        print(f"Error renombrando dispositivo {old_device_id} a {new_device_id}: {e}")
        return False

def delete_json_file(filepath):
    """Delete a specific JSON file"""
    try:
        if os.path.exists(filepath):
            os.remove(filepath)
            print(f"Archivo eliminado: {filepath}")
            return True
        return False
    except Exception as e:
        print(f"Error eliminando archivo {filepath}: {e}")
        return False

# Web Routes
@app.route("/")
def dashboard():
    """Main dashboard showing devices and recent data"""
    devices = {}
    if os.path.exists(BASE_DATA_DIR):
        for device_name in os.listdir(BASE_DATA_DIR):
            device_path = os.path.join(BASE_DATA_DIR, device_name)
            if os.path.isdir(device_path):
                sensors = []
                for sensor_name in os.listdir(device_path):
                    sensor_path = os.path.join(device_path, sensor_name)
                    if os.path.isdir(sensor_path):
                        # Count files in sensor folder
                        file_count = len([f for f in os.listdir(sensor_path) 
                                        if f.endswith('.json')])
                        sensors.append({
                            'name': sensor_name,
                            'file_count': file_count
                        })
                devices[device_name] = sensors
    
    return render_template('dashboard_simple.html', devices=devices)

@app.route("/device/<device_id>/sensor/<sensor_id>")
def view_sensor_data(device_id, sensor_id):
    """View data files for a specific sensor"""
    sensor_path = os.path.join(BASE_DATA_DIR, device_id, sensor_id)
    files = []
    
    if os.path.exists(sensor_path):
        for filename in sorted(os.listdir(sensor_path), reverse=True):
            if filename.endswith('.json'):
                filepath = os.path.join(sensor_path, filename)
                # Extract timestamp from filename
                timestamp_str = filename.replace('data_', '').replace('.json', '')
                try:
                    timestamp = datetime.strptime(timestamp_str, "%Y%m%d_%H%M%S_%f")
                    formatted_time = timestamp.strftime("%Y-%m-%d %H:%M:%S")
                except:
                    formatted_time = timestamp_str
                
                files.append({
                    'name': filename,
                    'path': filepath,
                    'timestamp': formatted_time
                })
    
    return render_template('sensor_data_simple.html', 
                         device_id=device_id, sensor_id=sensor_id, files=files)

@app.route("/api/file")
def get_file_content():
    """API endpoint to get file content"""
    try:
        filepath = request.args.get('path')
        if not filepath:
            return jsonify({"error": "No file path provided"}), 400
        
        # Security check
        decoded_filepath = urllib.parse.unquote(filepath)
        normalized_path = os.path.normpath(decoded_filepath)
        absolute_base = os.path.abspath(BASE_DATA_DIR)
        absolute_file = os.path.abspath(normalized_path)
        
        if not absolute_file.startswith(absolute_base):
            return jsonify({"error": "Access denied"}), 403
        
        if not os.path.exists(normalized_path):
            return jsonify({"error": "File not found"}), 404
        
        with open(normalized_path, 'r') as f:
            data = json.load(f)
        
        return jsonify(data)
    except Exception as e:
        return jsonify({"error": str(e)}), 500

# Myriota HTTP POST endpoint
@app.route("/myriota", methods=["POST"])
def receive_myriota_data():
    """Myriota satellite network HTTP POST endpoint"""
    try:
        data = request.get_json()
        print(f"📡 Datos Myriota Recibidos: {json.dumps(data, indent=2)}")
        
        # Extract Myriota-specific fields - TerminalId is nested in Data field
        terminal_id = None
        hex_value = None
        
        # Check if Data field exists and contains the nested JSON
        if "Data" in data:
            try:
                # Parse the nested JSON string in the Data field
                data_content = json.loads(data["Data"])
                print(f"📦 Contenido de Datos Analizados: {json.dumps(data_content, indent=2)}")
                
                # Extract from Packets array
                if "Packets" in data_content and len(data_content["Packets"]) > 0:
                    packet = data_content["Packets"][0]  # Take first packet
                    terminal_id = packet.get("TerminalId")
                    hex_value = packet.get("Value")
                    print(f"🔍 Extraído - TerminalId: {terminal_id}, Value: {hex_value}")
            except json.JSONDecodeError as e:
                print(f"❌ Error analizando JSON anidado de Datos: {e}")
        
        # Fallback to direct fields if nested parsing failed
        if not terminal_id and "TerminalId" in data:
            terminal_id = data["TerminalId"]
        if not hex_value and "Value" in data:
            hex_value = data["Value"]
        
        if terminal_id and hex_value:
            device_id, sensor_id = parse_flexsense_data(terminal_id, hex_value)
            
            # Decode sensor data
            decoded_data = decode_sensor_value(hex_value, sensor_id)
            
            # Enhance data with Myriota metadata
            enhanced_data = {
                "source": "myriota",
                "timestamp": datetime.now().isoformat(),
                "terminal_id": terminal_id,
                "device_id": device_id,
                "sensor_id": sensor_id,
                "raw_data": data,
                "decoded_data": decoded_data
            }
        else:
            # Generic handling for other data formats
            device_id, sensor_id = extract_device_sensor_info(data)
            if not device_id or device_id == "unknown_device":
                device_id = "myriota_generic_device"
            
            enhanced_data = {
                "source": "myriota_generic",
                "timestamp": datetime.now().isoformat(),
                "device_id": device_id,
                "sensor_id": sensor_id or "general_data",
                "raw_data": data
            }
        
        # Auto-create device and save data
        filepath = organize_sensor_data(device_id, sensor_id or "general_data", enhanced_data)
        
        return jsonify({
            "status": "success",
            "message": "Data received and saved",
            "device_id": device_id,
            "sensor_id": sensor_id or "general_data",
            "filepath": filepath
        }), 200
        
    except Exception as e:
        print(f"❌ Error procesando datos Myriota: {e}")
        return jsonify({"status": "error", "message": str(e)}), 500

# Generic HTTP POST endpoint
@app.route("/data", methods=["POST"])
def receive_http_data():
    """Generic HTTP endpoint for receiving sensor data"""
    try:
        data = request.get_json()
        print(f"📨 Datos HTTP Recibidos: {json.dumps(data, indent=2)}")
        
        # Extract device and sensor info
        device_id, sensor_id = extract_device_sensor_info(data)
        
        # Enhance data with metadata
        enhanced_data = {
            "source": "http",
            "timestamp": datetime.now().isoformat(),
            "device_id": device_id,
            "sensor_id": sensor_id,
            "raw_data": data
        }
        
        # Auto-create device and save data
        filepath = organize_sensor_data(device_id, sensor_id, enhanced_data)
        
        return jsonify({
            "status": "success",
            "message": "Datos recibidos y guardados",
            "device_id": device_id,
            "sensor_id": sensor_id,
            "filepath": filepath
        }), 200
        
    except Exception as e:
        print(f"❌ Error procesando datos HTTP: {e}")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route("/api/device/<device_id>", methods=["DELETE"])
def delete_device(device_id):
    """Delete a device and all its data"""
    if delete_device_data(device_id):
        return jsonify({"status": "success", "message": f"Dispositivo {device_id} eliminado"})
    else:
        return jsonify({"status": "error", "message": "Error al eliminar dispositivo"}), 500

@app.route("/api/device/<device_id>/rename", methods=["POST"])
def rename_device_endpoint(device_id):
    """Rename a device"""
    new_name = request.json.get("new_name")
    if not new_name:
        return jsonify({"status": "error", "message": "Se requiere nuevo nombre"}), 400
    
    if rename_device(device_id, new_name):
        return jsonify({"status": "success", "message": f"Dispositivo renombrado a {new_name}"})
    else:
        return jsonify({"status": "error", "message": "Error al renombrar dispositivo"}), 500

@app.route("/api/device/<device_id>/sensor/<sensor_id>", methods=["DELETE"])
def delete_sensor(device_id, sensor_id):
    """Delete a sensor and all its data"""
    if delete_sensor_data(device_id, sensor_id):
        return jsonify({"status": "success", "message": f"Sensor {sensor_id} eliminado"})
    else:
        return jsonify({"status": "error", "message": "Error al eliminar sensor"}), 500

@app.route("/api/file/delete", methods=["POST", "DELETE"])
def delete_file():
    """Delete a specific JSON file"""
    # Handle both query parameter (DELETE) and JSON body (POST)
    if request.method == "DELETE":
        filepath = request.args.get("path")
    else:
        filepath = request.json.get("filepath") if request.json else request.args.get("path")
    
    if not filepath:
        return jsonify({"success": False, "error": "Se requiere ruta de archivo"}), 400
    
    # Security check - ensure file is within our data directory
    normalized_path = os.path.normpath(filepath)
    absolute_base = os.path.abspath(BASE_DATA_DIR)
    absolute_file = os.path.abspath(normalized_path)
    
    if not absolute_file.startswith(absolute_base):
        return jsonify({"success": False, "error": "Acceso denegado"}), 403
    
    if delete_json_file(normalized_path):
        return jsonify({"success": True, "message": "Archivo eliminado"})
    else:
        return jsonify({"success": False, "error": "Error al eliminar archivo"}), 500

@app.route("/api/file/download")
def download_file():
    """Download a JSON file"""
    filepath = request.args.get('path')
    if not filepath:
        return jsonify({"error": "No se proporcionó ruta de archivo"}), 400
    
    # Security check
    decoded_filepath = urllib.parse.unquote(filepath)
    normalized_path = os.path.normpath(decoded_filepath.replace('/', os.sep))
    absolute_base = os.path.abspath(BASE_DATA_DIR)
    absolute_file = os.path.abspath(normalized_path)
    
    if not absolute_file.startswith(absolute_base):
        return jsonify({"error": "Acceso denegado"}), 403
    
    if not os.path.exists(normalized_path):
        return jsonify({"error": "Archivo no encontrado"}), 404
    
    try:
        return send_file(normalized_path, as_attachment=True, 
                        download_name=os.path.basename(normalized_path))
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == "__main__":
    print(" Iniciando Gestor de Datos FlexSense")
    print(f" Directorio de Datos: {BASE_DATA_DIR}")
    print(f" Ruta Absoluta de Datos: {os.path.abspath(BASE_DATA_DIR)}")
    print(f" Dominio Ngrok: {NGROK_DOMAIN}")
    print(" Endpoints HTTP: /myriota, /data")
    
    # Use port 80 and disable debug for stability
    app.run(debug=False, port=5000, host='0.0.0.0', threaded=True)
