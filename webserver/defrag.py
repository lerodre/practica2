#!/usr/bin/env python3
"""
SCHC NO-ACK Defragmentation Script
Reads JSON files from FlexSense sensor data and reconstructs fragmented messages
Based on RFC 8724 - Static Context Header Compression and Fragmentation
"""

import json
import os
import glob
from typing import List, Dict, Optional, Tuple
import struct

# SCHC Configuration (matching sender)
RULE_ID = 0x01
FCN_FINAL = 0x3F  # 111111 (6 bits)
MTU_SIZE = 20

class SCHCFragment:
    """Represents a SCHC fragment"""
    def __init__(self, hex_value: str, filename: str, timestamp: str):
        self.hex_value = hex_value
        self.filename = filename
        self.timestamp = timestamp
        self.raw_bytes = bytes.fromhex(hex_value)
        
        # Parse header
        self.rule_id = (self.raw_bytes[0] >> 6) & 0x03  # Upper 2 bits
        self.fcn = self.raw_bytes[0] & 0x3F  # Lower 6 bits
        self.is_final = (self.fcn == FCN_FINAL)
        
        if self.is_final:
            # Final fragment: RuleID(2)|FCN(6)|RCS(32)|payload
            self.rcs = struct.unpack('>I', self.raw_bytes[1:5])[0]  # Big-endian 32-bit
            self.payload_start = 5
            self.payload = self.raw_bytes[5:]
        else:
            # Normal fragment: RuleID(2)|FCN(6)|payload
            self.rcs = None
            self.payload_start = 1
            self.payload = self.raw_bytes[1:]
            
    def __repr__(self):
        return f"SCHCFragment(fcn={self.fcn}, final={self.is_final}, rcs={self.rcs}, payload_len={len(self.payload)})"

def calculate_crc32(data: bytes) -> int:
    """Calculate CRC-32 as used in SCHC (matching sender implementation)"""
    crc = 0xFFFFFFFF
    
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320  # CRC-32 polynomial
            else:
                crc >>= 1
    
    return (~crc) & 0xFFFFFFFF

def read_json_files(directory_path: str) -> List[Dict]:
    """Read all JSON files from the specified directory"""
    json_files = []
    json_pattern = os.path.join(directory_path, "*.json")
    
    print(f"🔍 Searching for JSON files in: {directory_path}")
    
    for filepath in glob.glob(json_pattern):
        try:
            with open(filepath, 'r', encoding='utf-8') as file:
                data = json.load(file)
                data['_filepath'] = filepath
                data['_filename'] = os.path.basename(filepath)
                json_files.append(data)
                print(f"   ✅ Loaded: {os.path.basename(filepath)}")
        except Exception as e:
            print(f"   ❌ Error loading {filepath}: {e}")
    
    print(f"📁 Found {len(json_files)} JSON files")
    return json_files

def extract_fragments(json_files: List[Dict]) -> List[SCHCFragment]:
    """Extract SCHC fragments from JSON files"""
    fragments = []
    
    print("\n🧩 Extracting SCHC fragments...")
    
    for json_data in json_files:
        try:
            # Parse the nested Data field
            raw_data = json_data.get('raw_data', {})
            data_str = raw_data.get('Data', '')
            
            if not data_str:
                print(f"   ⚠️  No Data field in {json_data['_filename']}")
                continue
            
            # Parse the nested JSON in Data field
            data_obj = json.loads(data_str)
            packets = data_obj.get('Packets', [])
            
            for packet in packets:
                hex_value = packet.get('Value', '')
                if hex_value:
                    fragment = SCHCFragment(
                        hex_value=hex_value,
                        filename=json_data['_filename'],
                        timestamp=json_data.get('timestamp', '')
                    )
                    fragments.append(fragment)
                    print(f"   📦 Fragment FCN={fragment.fcn} from {fragment.filename}")
                    
        except Exception as e:
            print(f"   ❌ Error parsing {json_data['_filename']}: {e}")
    
    print(f"🔢 Total fragments extracted: {len(fragments)}")
    return fragments

def validate_and_sort_fragments(fragments: List[SCHCFragment]) -> Tuple[List[SCHCFragment], bool]:
    """Validate fragment completeness and sort them by FCN"""
    print("\n🔍 Validating fragments...")
    
    if not fragments:
        print("   ❌ No fragments found!")
        return [], False
    
    # Find final fragment
    final_fragment = None
    normal_fragments = []
    
    for fragment in fragments:
        if fragment.is_final:
            if final_fragment is None:
                final_fragment = fragment
                print(f"   🏁 Final fragment found: FCN={fragment.fcn}, RCS=0x{fragment.rcs:08X}")
            else:
                print(f"   ⚠️  Multiple final fragments found!")
        else:
            normal_fragments.append(fragment)
    
    if final_fragment is None:
        print("   ❌ No final fragment found - cannot determine expected fragment count")
        return [], False
    
    # Sort normal fragments by FCN
    normal_fragments.sort(key=lambda f: f.fcn)
    
    # Validate sequence
    expected_fcn = 0
    missing_fragments = []
    
    for fragment in normal_fragments:
        if fragment.fcn != expected_fcn:
            # Check for missing fragments
            while expected_fcn < fragment.fcn:
                missing_fragments.append(expected_fcn)
                expected_fcn += 1
        expected_fcn += 1
    
    if missing_fragments:
        print(f"   ❌ Missing fragments with FCN: {missing_fragments}")
        return [], False
    
    # Create sorted list: normal fragments + final fragment
    sorted_fragments = normal_fragments + [final_fragment]
    
    print(f"   ✅ Fragment sequence is complete")
    print(f"   📋 Fragment order: {[f.fcn for f in sorted_fragments]}")
    
    return sorted_fragments, True

def reassemble_message(fragments: List[SCHCFragment]) -> Optional[str]:
    """Reassemble the original message from fragments"""
    print("\n🔧 Reassembling message...")
    
    if not fragments:
        print("   ❌ No fragments to reassemble")
        return None
    
    # Concatenate payloads
    full_payload = bytearray()
    
    for i, fragment in enumerate(fragments):
        payload_to_add = fragment.payload
        
        # Remove null padding from payload
        while payload_to_add and payload_to_add[-1] == 0:
            payload_to_add = payload_to_add[:-1]
        
        full_payload.extend(payload_to_add)
        print(f"   📦 Added {len(payload_to_add)} bytes from fragment FCN={fragment.fcn}")
    
    print(f"   🔗 Total payload length: {len(full_payload)} bytes")
    
    # Find final fragment for CRC verification
    final_fragment = None
    for fragment in fragments:
        if fragment.is_final:
            final_fragment = fragment
            break
    
    if final_fragment:
        # Calculate CRC of the reassembled payload
        calculated_crc = calculate_crc32(bytes(full_payload))
        expected_crc = final_fragment.rcs
        
        print(f"   🔐 CRC verification:")
        print(f"      Expected: 0x{expected_crc:08X}")
        print(f"      Calculated: 0x{calculated_crc:08X}")
        
        if calculated_crc == expected_crc:
            print("   ✅ CRC verification PASSED")
        else:
            print("   ❌ CRC verification FAILED - message may be corrupted")
            return None
    
    # Convert to string (assuming it's text)
    try:
        message = full_payload.decode('utf-8').rstrip('\x00')
        print(f"   📝 Decoded message: \"{message}\"")
        return message
    except UnicodeDecodeError:
        print("   ⚠️  Message is not valid UTF-8, showing as hex:")
        hex_message = full_payload.hex()
        print(f"   📝 Hex message: {hex_message}")
        return hex_message

def main():
    """Main defragmentation function"""
    print("🚀 SCHC NO-ACK Defragmentation Script")
    print("=" * 50)
    
    # Path to sensor data
    sensor_data_path = "flasksv/sensor_data/flex_5e92a51c/sensor_generic"
    
    if not os.path.exists(sensor_data_path):
        print(f"❌ Error: Directory not found: {sensor_data_path}")
        print("Please make sure the path is correct and the directory exists.")
        return
    
    print(f"📂 Reading from: {sensor_data_path}")
    
    # Step 1: Read JSON files
    json_files = read_json_files(sensor_data_path)
    if not json_files:
        print("❌ No JSON files found!")
        return
    
    # Step 2: Extract fragments
    fragments = extract_fragments(json_files)
    if not fragments:
        print("❌ No fragments extracted!")
        return
    
    # Step 3: Validate and sort fragments
    sorted_fragments, is_valid = validate_and_sort_fragments(fragments)
    if not is_valid:
        print("❌ Fragment validation failed!")
        return
    
    # Step 4: Reassemble message
    message = reassemble_message(sorted_fragments)
    
    if message:
        print("\n🎉 SUCCESS!")
        print("=" * 50)
        print(f"📄 Reconstructed message:")
        print(f'   "{message}"')
        print(f"📊 Statistics:")
        print(f"   - Total fragments: {len(sorted_fragments)}")
        print(f"   - Message length: {len(message)} characters")
        print(f"   - Files processed: {len(json_files)}")
    else:
        print("\n❌ FAILED!")
        print("Could not reconstruct the message.")

if __name__ == "__main__":
    main()
