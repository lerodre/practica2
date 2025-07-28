# FlexSense Data Manager

Web application for managing sensor data from FlexSense devices via MQTT and Myriota satellite network.

## Requirements

- Python 3.7+
- MQTT Broker (Mosquitto)
- ngrok (for Myriota integration)

## Installation

1. Install dependencies:

```bash
pip install flask paho-mqtt
```

2. Install Mosquitto MQTT broker:

```bash
# Windows (download from mosquitto.org)
# Or using chocolatey:
choco install mosquitto
```

3. Install ngrok (for satellite data reception):

```bash
# Download from ngrok.com and follow installation instructions
```

## Configuration

### Local Setup

1. Start MQTT broker:

```bash
mosquitto
# Or on Windows service:
net start mosquitto
```

2. Start Flask application:

```bash
cd webserver
python app.py
```

3. Access dashboard: http://localhost:80

### Myriota Integration Setup

1. Start ngrok tunnel:

```bash
ngrok http 80 --domain=cougar-factual-osprey.ngrok-free.app
```

2. Configure Myriota webhook URL: https://cougar-factual-osprey.ngrok-free.app/myriota
3. Test integration: https://cougar-factual-osprey.ngrok-free.app/health

## Usage

### Adding Devices

1. Click "Add Device" on dashboard
2. Enter device ID and name
3. List sensor types (comma-separated)
4. Device will be registered for MQTT and data organization

### Data Reception

- **MQTT**: Automatic via mosquitto broker on topic myriota/device_id/sensor_id
- **HTTP POST**: Send JSON to /data endpoint
- **Myriota Satellite**: Automatic via /myriota webhook

### Data Management

- **View Files**: Click on sensor to browse data files
- **Preview**: Click "View" to see JSON content
- **Download**: Click "Download" to save files locally
- **Delete Files**: Click "Delete" to remove individual files
- **Delete Sensors**: Click trash icon on sensor in dashboard
- **Delete Devices**: Click trash icon on device in dashboard
- **Rename Devices**: Click edit icon on device in dashboard

### File Organization

Data automatically organized as:

```
sensor_data/
├── device_id/
│   ├── sensor_id/
│   │   ├── data_timestamp.json
│   │   └── ...
│   └── ...
└── ...
```

## API Endpoints

### Web Interface

- `/` - Main dashboard
- `/device/{device_id}/sensor/{sensor_id}` - Sensor data view
- `/add_device` - Add new device form

### Data Reception

- `/myriota` - Myriota satellite webhook (POST)
- `/data` - General data endpoint (POST)

### Device Management

- `DELETE /api/device/{device_id}` - Delete device and all data
- `POST /api/device/{device_id}/rename` - Rename device
- `DELETE /api/device/{device_id}/sensor/{sensor_id}` - Delete sensor

### File Operations

- `GET /api/file?path={filepath}` - Get file content
- `GET /api/file/download?path={filepath}` - Download file
- `POST /api/file/delete` - Delete file

### System Status

- `/health` - System health check
- `/api/mqtt/status` - MQTT connection status

## Testing

### MQTT Testing

```bash
# Publish test message
mosquitto_pub -h localhost -t myriota/test_device/temperature -m '{"temperature":25.5,"timestamp":1234567890}'
```

### HTTP Testing

```bash
# Send test data
curl -X POST http://localhost:80/data -H "Content-Type: application/json" -d '{"device_id":"test","sensor_id":"temp","value":25.5}'
```

### Myriota Testing

```bash
cd webserver
python test_myriota.py
```

## Data Formats

### MQTT Message

```json
{
  "device_id": "flex_001",
  "sensor_id": "temperature",
  "value": 25.5,
  "timestamp": 1234567890
}
```

### Myriota Satellite Message

```json
{
  "EndpointRef": "N_HlfTNgRsqe:device_id",
  "Timestamp": 1234567890,
  "Data": "{\"Packets\":[{\"TerminalId\":\"device_id\",\"Value\":\"hex_data\"}]}",
  "Id": "message_id"
}
```

## Troubleshooting

### MQTT Issues

- Check if mosquitto is running: `mosquitto -v`
- Verify port 1883 is available
- Check firewall settings

### ngrok Issues

- Verify domain is correct: cougar-factual-osprey.ngrok-free.app
- Check if tunnel is active: visit ngrok dashboard
- Test webhook: curl https://cougar-factual-osprey.ngrok-free.app/health

### Data Issues

- Check file permissions in sensor_data directory
- Verify JSON format of incoming data
- Check Flask logs for error messages

### Web Interface Issues

- Clear browser cache
- Check Flask server is running on port 80
- Verify no other services using port 80

## File Structure

```
webserver/
├── app.py                 # Main Flask application
├── templates/             # HTML templates
│   ├── dashboard.html     # Main dashboard
│   ├── sensor_data.html   # Sensor data viewer
│   └── add_device.html    # Device registration
├── sensor_data/           # Data storage (auto-created)
├── devices_config.json    # Device registry (auto-created)
├── test_myriota.py        # Myriota integration tests
└── README.md              # This file
```

## Security Notes

- Flask runs in development mode - use production WSGI server for deployment
- File access restricted to sensor_data directory
- Input validation on all API endpoints
- MQTT broker should be secured for production use
