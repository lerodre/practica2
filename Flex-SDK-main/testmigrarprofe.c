// SCHC FlexSense// No-ACK Mode Configuration
#define NOACK_FCN_SIZE 6              // FCN field size in No-ACK (6 bits)
#define MAX_FRAGMENTS 63              // Maximum fragments in No-ACK (FCN 0-62 + ALL-1 = 63)
#define FCN_ALL1 63                   // FCN value for ALL-1 fragment (111111 in binary)
#define RCS_SIZE 4                    // RCS (CRC-32) size in bytes

// FlexSense specific constraints
#define MAX_MESSAGES_PER_DAY 20       // FlexSense limit: 20 messages per 24 hours
#define SENSOR_READ_INTERVAL 3        // Hours between readings- No-ACK Mode Implementation
// Implements RFC 8724 - Static Context Header Compression and Fragmentation

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "flex.h"

#define APPLICATION_NAME "SCHC FlexSense Adapter - No-ACK"

// SCHC Configuration
#define MTU_SIZE 20                   // FlexSense MTU size
#define SCHC_FRAG_UPDIR_RULE_ID 20    //SCHC_GW_Macros.hpp
#define SCHC_FRAG_DOWNDIR_RULE_ID 21  //SCHC_GW_Macros.hpp

// No-ACK Mode Configuration
#define NOACK_FCN_SIZE 6              // FCN field size in No-ACK (6 bits)
#define MAX_FRAGMENTS 63              // Maximum fragments in No-ACK (2^6 - 1 = 63, FCN=63 reserved for ALL-1)
#define FCN_ALL1 63                   // FCN value for ALL-1 fragment (111111 in binary)
#define RCS_SIZE 4                    // RCS (CRC-32) size in bytes

// FlexSense specific constraints
#define MAX_MESSAGES_PER_DAY 20       // 20 messages per 24 hours
#define SENSOR_READ_INTERVAL 3        // Hours between readings

// Message tracking
static uint8_t messages_sent_today = 0;
static uint32_t last_reset_day = 0;
static uint16_t sequence_number = 0;

// SCHC Fragment Types (adapted from teacher's message types)
typedef enum {
    SCHC_REGULAR_FRAGMENT = 0,
    SCHC_ALL1_FRAGMENT = 1
} schc_fragment_type_t;

// SCHC Message Structure
typedef struct {
    uint8_t rule_id;
    uint8_t fcn;            // 6-bit FCN (62,61,60...0 for regular fragments, 63 for ALL-1)
    uint32_t rcs;           // CRC-32 for integrity check (only in ALL-1 fragment)
    uint8_t *payload;
    uint16_t payload_len;
    schc_fragment_type_t type;
} schc_message_t;

// CRC-32 calculation
static uint32_t calculate_crc32(const uint8_t *data, uint16_t length) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;  // CRC-32 polynomial
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

// Simulate sensor data reading (replace with actual sensor)
static uint8_t* read_sensor_data(uint16_t *data_length) {
    // Simulate reading from various sensors
    static char sensor_data[200];
    
    // Get temperature from FlexSense
    float temp_celsius;
    int temp_result = FLEX_TemperatureGet(&temp_celsius);
    
    // Simulate additional sensor readings
    uint16_t humidity = 65;      // Simulated humidity
    uint16_t pressure = 1013;    // Simulated pressure in hPa
    uint16_t light = 450;        // Simulated light level
    
    snprintf(sensor_data, sizeof(sensor_data), 
             "SENSOR_DATA|SEQ:%d|TEMP:%.1f|HUM:%d|PRESS:%d|LIGHT:%d|STATUS:OK|TIMESTAMP:%ld",
             sequence_number++, 
             (temp_result == 0) ? temp_celsius : 25.0,  // Default temp if error
             humidity, pressure, light, FLEX_TimeGet());
    
    *data_length = strlen(sensor_data);
    
    // Allocate and copy data
    uint8_t *data = malloc(*data_length);
    if (data) {
        memcpy(data, sensor_data, *data_length);
    }
    
    return data;
}

// Create SCHC fragment (No-ACK mode: RuleID + FCN)
static int create_schc_fragment(uint8_t fragment_num, uint8_t total_fragments,
                                const uint8_t *payload, uint16_t payload_len,
                                const uint8_t *original_data, uint16_t original_len,
                                uint8_t *packet) {
    
    memset(packet, 0, MTU_SIZE);
    
    bool is_all1 = (fragment_num == total_fragments - 1);
    uint8_t header_offset = 0;
    uint8_t fcn_value;
    
    if (is_all1) {
        // ALL-1 Fragment: RuleID(2)|FCN(6)|RCS(32)|Payload
        // Pack RuleID(2) and FCN(6) into first byte
        fcn_value = FCN_ALL1;  // FCN = 63 (111111) for ALL-1
        packet[0] = (SCHC_FRAG_UPDIR_RULE_ID << 6) | (fcn_value & 0x3F);
        
        // Calculate and insert RCS (CRC-32)
        uint32_t rcs = calculate_crc32(original_data, original_len);
        packet[1] = (rcs >> 24) & 0xFF;
        packet[2] = (rcs >> 16) & 0xFF;
        packet[3] = (rcs >> 8) & 0xFF;
        packet[4] = rcs & 0xFF;
        
        header_offset = 5;  // RuleID+FCN(1) + RCS(4) = 5 bytes
        
        printf("ALL-1 Fragment %d: RuleID=%d, FCN=%d, RCS=0x%08X\n", 
               fragment_num, SCHC_FRAG_UPDIR_RULE_ID, fcn_value, (unsigned int)rcs);
    } else {
        // Regular Fragment: RuleID(2)|FCN(6)|Payload
        // SCHC uses descending FCN: first fragment = 62, second = 61, etc.
        fcn_value = (FCN_ALL1 - 1) - fragment_num;  // FCN descending from 62,61,60...
        packet[0] = (SCHC_FRAG_UPDIR_RULE_ID << 6) | (fcn_value & 0x3F);
        
        header_offset = 1;  // Only RuleID+FCN = 1 byte
        
        printf("Regular Fragment %d: RuleID=%d, FCN=%d (descending)\n", 
               fragment_num, SCHC_FRAG_UPDIR_RULE_ID, fcn_value);
    }
    
    // Calculate available payload space
    uint8_t available_payload_space = MTU_SIZE - header_offset;
    
    // Copy payload
    uint16_t copy_len = (payload_len <= available_payload_space) ? 
                        payload_len : available_payload_space;
    
    if (copy_len > 0) {
        memcpy(&packet[header_offset], payload, copy_len);
    }
    
    // Print packet for debugging
    printf("Packet (%d bytes): ", MTU_SIZE);
    for (int i = 0; i < MTU_SIZE; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");
    
    // Show payload as text (if printable)
    printf("Payload text: \"");
    for (int i = header_offset; i < header_offset + copy_len; i++) {
        if (packet[i] >= 32 && packet[i] <= 126) {
            printf("%c", packet[i]);
        } else {
            printf(".");
        }
    }
    printf("\"\n");
    
    return copy_len;
}

// Check daily message limit
static bool can_send_message_today(void) {
    time_t current_time = FLEX_TimeGet();
    uint32_t current_day = current_time / (24 * 3600);
    
    if (current_day != last_reset_day) {
        messages_sent_today = 0;
        last_reset_day = current_day;
        printf("New day - resetting message counter\n");
    }
    
    return messages_sent_today < MAX_MESSAGES_PER_DAY;
}

// Main SCHC fragmentation and transmission (No-ACK mode)
static void send_schc_noack_message(const uint8_t *data, uint16_t data_len) {
    printf("\n=== SCHC No-ACK Transmission ===\n");
    printf("Sending data: %d bytes\n", data_len);
    printf("Data preview: %.50s%s\n", data, (data_len > 50) ? "..." : "");
    
    // Calculate payload spaces (No windows, only FCN)
    uint8_t regular_payload_space = MTU_SIZE - 1;  // RuleID+FCN = 1 byte
    uint8_t all1_payload_space = MTU_SIZE - 5;     // RuleID+FCN+RCS = 5 bytes
    
    if (data_len <= all1_payload_space) {
        printf("Data fits in single ALL-1 fragment\n");
        
        // Send as single ALL-1 fragment
        uint8_t packet[MTU_SIZE];
        create_schc_fragment(0, 1, data, data_len, data, data_len, packet);
        
        int result = FLEX_MessageSchedule(packet, MTU_SIZE);
        if (result == 0) {
            printf("Single fragment sent successfully\n");
            messages_sent_today++;
        } else {
            printf("Failed to send fragment: %d\n", result);
        }
        return;
    }
    
    printf("Fragmentation needed\n");
    
    // Calculate number of fragments needed
    uint16_t remaining_data = data_len;
    uint8_t fragment_count = 0;
    
    // Count regular fragments
    while (remaining_data > all1_payload_space) {
        fragment_count++;
        remaining_data -= regular_payload_space;
        if (remaining_data <= all1_payload_space) {
            break;
        }
    }
    
    // Add ALL-1 fragment
    fragment_count++;
    
    if (fragment_count > MAX_FRAGMENTS) {
        printf("Error: Too many fragments needed (%d > %d)\n", 
               fragment_count, MAX_FRAGMENTS);
        return;
    }
    
    printf("Total fragments needed: %d\n", fragment_count);
    
    // Check if we have enough daily message quota
    if (messages_sent_today + fragment_count > MAX_MESSAGES_PER_DAY) {
        printf("Error: Not enough daily quota. Need %d fragments, have %d messages left\n",
               fragment_count, MAX_MESSAGES_PER_DAY - messages_sent_today);
        return;
    }
    
    // Send fragments
    uint16_t offset = 0;
    for (uint8_t i = 0; i < fragment_count; i++) {
        if (!can_send_message_today()) {
            printf("Daily message limit reached during transmission\n");
            break;
        }
        
        bool is_all1 = (i == fragment_count - 1);
        uint16_t payload_size;
        uint8_t available_space = is_all1 ? all1_payload_space : regular_payload_space;
        
        payload_size = (offset + available_space <= data_len) ? 
                       available_space : (data_len - offset);
        
        uint8_t packet[MTU_SIZE];
        int sent_len = create_schc_fragment(i, fragment_count, 
                                           &data[offset], payload_size,
                                           data, data_len, packet);
        
        if (sent_len > 0) {
            int result = FLEX_MessageSchedule(packet, MTU_SIZE);
            if (result == 0) {
                printf("Fragment %d sent successfully (%d bytes payload)\n", i, sent_len);
                messages_sent_today++;
                offset += sent_len;
            } else {
                printf("Failed to send fragment %d: %d\n", i, result);
                break;
            }
        } else {
            printf("Error creating fragment %d\n", i);
            break;
        }
    }
    
    printf("Transmission complete. Sent: %d bytes, Total: %d bytes\n", offset, data_len);
}

// Main sensor reading and transmission function
static time_t process_sensor_data(void) {
    printf("\n========================================\n");
    printf("SCHC FlexSense Data Collection\n");
    printf("Messages sent today: %d/%d\n", messages_sent_today, MAX_MESSAGES_PER_DAY);
    
    if (!can_send_message_today()) {
        printf("Daily limit reached, waiting for next day\n");
        return FLEX_HoursFromNow(6);
    }
    
    // Read sensor data
    uint16_t data_length;
    uint8_t *sensor_data = read_sensor_data(&data_length);
    
    if (sensor_data == NULL) {
        printf("Error reading sensor data\n");
        return FLEX_HoursFromNow(1);  // Retry in 1 hour
    }
    
    printf("Sensor data collected: %d bytes\n", data_length);
    
    // Send via SCHC No-ACK
    send_schc_noack_message(sensor_data, data_length);
    
    // Clean up
    free(sensor_data);
    
    printf("Next reading in %d hours\n", SENSOR_READ_INTERVAL);
    printf("========================================\n\n");
    
    return FLEX_HoursFromNow(SENSOR_READ_INTERVAL);
}

// FlexSense initialization
void FLEX_AppInit() {
    printf("%s\n", APPLICATION_NAME);
    printf("SCHC No-ACK Configuration:\n");
    printf("- MTU Size: %d bytes\n", MTU_SIZE);
    printf("- Rule ID: %d\n", SCHC_FRAG_UPDIR_RULE_ID);
    printf("- FCN Size: %d bits (62,61,60...0 regular, 63 ALL-1)\n", NOACK_FCN_SIZE);
    printf("- Max fragments: %d\n", MAX_FRAGMENTS);
    printf("- Max messages/day: %d (FlexSense limit)\n", MAX_MESSAGES_PER_DAY);
    printf("- Sensor read interval: %d hours\n", SENSOR_READ_INTERVAL);
    printf("- Regular fragment payload: %d bytes\n", MTU_SIZE - 1);
    printf("- ALL-1 fragment payload: %d bytes\n", MTU_SIZE - 5);
    printf("Starting SCHC No-ACK sensor data collection...\n");
    
    // Initialize day tracking
    time_t current_time = FLEX_TimeGet();
    last_reset_day = current_time / (24 * 3600);
    sequence_number = 0;
    
    // Schedule first data collection
    FLEX_JobSchedule(process_sensor_data, FLEX_ASAP());
}
