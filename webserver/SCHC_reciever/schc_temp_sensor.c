// SCHC Temperature Sensor with Fragmentation Support
// Based on RFC 8724 - Static Context Header Compression and Fragmentation

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "flex.h"

#define APPLICATION_NAME "SCHC Temperature Sensor"

// SCHC Configuration
#define SCHC_MTU_BYTES 20             // Maximum Transmission Unit (bytes)
#define SCHC_MTU_HEX_CHARS 40         // MTU in hex characters (20 bytes * 2)
#define SCHC_RULE_ID_BITS 2           // Rule ID size in bits
#define SCHC_FCN_BITS 3               // Fragment Counter (FCN) size in bits
#define SCHC_RCS_BITS 8               // Reassembly Check Sequence size in bits

// SCHC Values
#define SCHC_RULE_ID_TEMP 0x01        // Rule ID for temperature messages
#define SCHC_FCN_INTERMEDIATE 0x00    // FCN for intermediate fragments (000-110)
#define SCHC_FCN_FINAL 0x07           // FCN for final fragment (111)
#define SCHC_FCN_ALL_1 0x07           // All-1 fragment marker

// Message limits per day
#define MAX_MESSAGES_PER_DAY 24
#define SAMPLES_PER_DAY 4

// Message tracking
static uint32_t messages_sent_today = 0;
static uint32_t last_reset_day = 0;

// SCHC Fragment structure
typedef struct {
    uint8_t rule_id;
    uint8_t fcn;
    uint8_t rcs;
    uint8_t payload[SCHC_MTU_BYTES - 2];  // Payload (minus rule_id and fcn)
    uint8_t payload_len;
    bool has_rcs;
} schc_fragment_t;

// Message buffer for fragmentation
typedef struct {
    uint8_t data[256];  // Maximum message size
    uint16_t length;
    uint8_t fragment_count;
    uint8_t sequence_number;
} message_buffer_t;

// Diagnostic fields
#define DIAG_MESSAGES_SENT_TODAY FLEX_DIAG_CONF_ID_USER_0
#define DIAG_FRAGMENTS_SENT FLEX_DIAG_CONF_ID_USER_1
#define DIAG_LAST_TEMPERATURE FLEX_DIAG_CONF_ID_USER_2

FLEX_DIAG_CONF_TABLE_BEGIN()
  FLEX_DIAG_CONF_TABLE_U32_ADD(DIAG_MESSAGES_SENT_TODAY, "Messages Sent Today", 0, FLEX_DIAG_CONF_TYPE_DIAG),
  FLEX_DIAG_CONF_TABLE_U32_ADD(DIAG_FRAGMENTS_SENT, "Fragments Sent", 0, FLEX_DIAG_CONF_TYPE_PERSIST_DIAG),
  FLEX_DIAG_CONF_TABLE_U32_ADD(DIAG_LAST_TEMPERATURE, "Last Temperature", 0, FLEX_DIAG_CONF_TYPE_DIAG),
FLEX_DIAG_CONF_TABLE_END();

// CRC-8 calculation for RCS (Reassembly Check Sequence)
static uint8_t calculate_crc8(const uint8_t *data, uint16_t length) {
    uint8_t crc = 0xFF;
    uint16_t i, j;
    
    for (i = 0; i < length; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;  // CRC-8 polynomial
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// Simulate temperature sensor reading
static int16_t read_temperature_sensor(void) {
    // Simulate temperature reading (in 0.1°C units)
    // In real implementation, this would read from actual sensor
    static int16_t base_temp = 250;  // 25.0°C
    
    // Add some variation (+/- 2°C)
    int16_t variation = (FLEX_TimeGet() % 40) - 20;
    return base_temp + variation;
}

// Check if we can send more messages today
static bool can_send_message_today(void) {
    time_t current_time = FLEX_TimeGet();
    uint32_t current_day = current_time / (24 * 3600);
    
    // Reset counter if it's a new day
    if (current_day != last_reset_day) {
        messages_sent_today = 0;
        last_reset_day = current_day;
        printf("New day - resetting message counter\n");
    }
    
    return messages_sent_today < MAX_MESSAGES_PER_DAY;
}

// Create temperature message (longer message to force fragmentation for testing)
static uint16_t create_temperature_message(uint8_t *buffer, int16_t temperature) {
    const char *msg_template = "the temperature registered by the sensor is: %d.%d degrees celsius and this message is intentionally long to test SCHC fragmentation over satellite";
    uint16_t length;
    
    // Format temperature with one decimal place
    length = snprintf((char*)buffer, 256, msg_template, 
                     temperature / 10, abs(temperature % 10));
    
    return length;
}

// Fragment a message using SCHC
static uint8_t fragment_message(const message_buffer_t *msg, schc_fragment_t *fragments) {
    uint8_t fragment_count = 0;
    uint16_t offset = 0;
    uint8_t fcn = 0;
    
    printf("Fragmenting message: %d bytes\n", msg->length);
    
    // Calculate how many fragments we need
    uint16_t intermediate_payload_size = SCHC_MTU_BYTES - 1;  // 19 bytes (20 - 1 for header)
    uint16_t final_payload_size = SCHC_MTU_BYTES - 2;        // 18 bytes (20 - 2 for header + RCS)
    uint16_t remaining = msg->length;
    
    while (remaining > 0) {
        schc_fragment_t *frag = &fragments[fragment_count];
        
        // Set rule ID
        frag->rule_id = SCHC_RULE_ID_TEMP;
        
        // Check if this is the last fragment
        bool is_last_fragment = (remaining <= final_payload_size) || 
                               (fragment_count == 7); // Force final at FCN=7
        
        if (is_last_fragment) {
            // Final fragment (All-1)
            frag->fcn = SCHC_FCN_FINAL;
            frag->has_rcs = true;
            frag->rcs = calculate_crc8(msg->data, msg->length);
            
            // Use remaining bytes, up to final_payload_size
            frag->payload_len = (remaining <= final_payload_size) ? remaining : final_payload_size;
        } else {
            // Intermediate fragment
            frag->fcn = fcn;
            frag->has_rcs = false;
            frag->rcs = 0;
            
            // Use full intermediate payload size
            frag->payload_len = (remaining >= intermediate_payload_size) ? 
                               intermediate_payload_size : remaining;
        }
        
        // Copy payload
        memcpy(frag->payload, &msg->data[offset], frag->payload_len);
        
        printf("Fragment %d: FCN=%d, payload_len=%d, is_last=%d\n", 
               fragment_count, frag->fcn, frag->payload_len, is_last_fragment);
        
        offset += frag->payload_len;
        remaining -= frag->payload_len;
        fragment_count++;
        
        // Break if we've processed all data
        if (remaining == 0) break;
        
        // Increment FCN for next fragment (but not for final)
        if (!is_last_fragment) {
            fcn = (fcn + 1) % SCHC_FCN_FINAL;  // 0-6, then final is 7
        }
    }
    
    return fragment_count;
}

// Send a single fragment
static int send_fragment(const schc_fragment_t *fragment) {
    uint8_t packet[SCHC_MTU_BYTES];
    uint8_t packet_len = 0;
    
    // Clear packet buffer first
    memset(packet, 0, SCHC_MTU_BYTES);
    
    if (fragment->has_rcs) {
        // Final fragment: RuleID(2)|FCN(3)|RCS(8)|5_reserved_bits + Payload
        packet[0] = (fragment->rule_id << 6) | (fragment->fcn << 3) | ((fragment->rcs >> 5) & 0x07);
        packet[1] = (fragment->rcs << 3) & 0xF8;  // RCS lower 5 bits + 3 reserved bits
        packet_len = 2;
        
        // Add payload starting from bit 3 of byte 1
        // For simplicity, align payload to byte boundary at byte 2
        memcpy(&packet[2], fragment->payload, fragment->payload_len);
        packet_len = 2 + fragment->payload_len;
    } else {
        // Intermediate fragment: RuleID(2)|FCN(3)|3_reserved_bits + Payload
        packet[0] = (fragment->rule_id << 6) | (fragment->fcn << 3);  // 3 reserved bits are 0
        packet_len = 1;
        
        // Add payload starting from byte 1
        memcpy(&packet[1], fragment->payload, fragment->payload_len);
        packet_len = 1 + fragment->payload_len;
    }
    
    // Ensure packet is exactly MTU size (pad with zeros if needed)
    if (packet_len < SCHC_MTU_BYTES) {
        // Already zeroed by memset, just set length
        packet_len = SCHC_MTU_BYTES;
    }
    
    // Convert to hex string for debugging only
    char hex_debug[SCHC_MTU_HEX_CHARS + 1];
    for (int i = 0; i < SCHC_MTU_BYTES; i++) {
        sprintf(&hex_debug[i * 2], "%02X", packet[i]);
    }
    hex_debug[SCHC_MTU_HEX_CHARS] = '\0';
    
    printf("Sending fragment: RuleID=%d, FCN=%d, RCS=%s, size=%d bytes\n",
           fragment->rule_id, fragment->fcn, 
           fragment->has_rcs ? "yes" : "no", packet_len);
    printf("Debug hex: %s\n", hex_debug);
    
    // ✅ Send binary data directly (NOT hex string)
    return FLEX_MessageSchedule(packet, SCHC_MTU_BYTES);
}

// Main temperature sampling and transmission function
static time_t temperature_sample_and_send(void) {
    // Check if we can send more messages today
    if (!can_send_message_today()) {
        printf("Message limit reached for today (%lu/%d)\n", 
               (unsigned long)messages_sent_today, MAX_MESSAGES_PER_DAY);
        return FLEX_HoursFromNow(24);  // Try again tomorrow
    }
    
    // Read temperature sensor
    int16_t temperature = read_temperature_sensor();
    printf("Temperature reading: %d.%d°C\n", temperature / 10, abs(temperature % 10));
    
    // Update diagnostic
    uint32_t temp_diag = (uint32_t)temperature;
    FLEX_DiagConfValueWrite(DIAG_LAST_TEMPERATURE, &temp_diag);
    
    // Create message
    message_buffer_t msg;
    msg.length = create_temperature_message(msg.data, temperature);
    msg.sequence_number = messages_sent_today;
    
    printf("Created message: \"%s\" (%d bytes)\n", msg.data, msg.length);
    
    // Force fragmentation for testing - always fragment if message > 19 bytes
    if (msg.length > SCHC_MTU_BYTES - 1) {
        // Fragmentation needed
        printf("Fragmentation required\n");
        
        schc_fragment_t fragments[8];  // Maximum 8 fragments
        uint8_t fragment_count = fragment_message(&msg, fragments);
        
        printf("Message fragmented into %d fragments\n", fragment_count);
        
        // Send all fragments
        uint8_t sent_fragments = 0;
        for (uint8_t i = 0; i < fragment_count; i++) {
            if (messages_sent_today >= MAX_MESSAGES_PER_DAY) {
                printf("Message limit reached while sending fragments\n");
                break;
            }
            
            int result = send_fragment(&fragments[i]);
            if (result == 0) {
                sent_fragments++;
                messages_sent_today++;
                printf("✓ Fragment %d sent successfully\n", i);
            } else {
                printf("✗ Failed to send fragment %d (error: %d)\n", i, result);
                break;
            }
        }
        
        // Update diagnostics
        uint32_t msg_count = messages_sent_today;
        FLEX_DiagConfValueWrite(DIAG_MESSAGES_SENT_TODAY, &msg_count);
        
        uint32_t frag_count = 0;
        FLEX_DiagConfValueRead(DIAG_FRAGMENTS_SENT, &frag_count);
        frag_count += sent_fragments;
        FLEX_DiagConfValueWrite(DIAG_FRAGMENTS_SENT, &frag_count);
        
        printf("Sent %d/%d fragments successfully\n", sent_fragments, fragment_count);
    }
    
    printf("Messages sent today: %lu/%d\n", (unsigned long)messages_sent_today, MAX_MESSAGES_PER_DAY);
    printf("Next sample in %d hours\n", 24 / SAMPLES_PER_DAY);
    printf("===============================\n\n");
    
    // Schedule next sample
    return FLEX_HoursFromNow(24 / SAMPLES_PER_DAY);
}

void FLEX_AppInit() {
    printf("%s\n", APPLICATION_NAME);
    printf("SCHC MTU: %d bytes (%d hex chars)\n", SCHC_MTU_BYTES, SCHC_MTU_HEX_CHARS);
    printf("Max messages per day: %d\n", MAX_MESSAGES_PER_DAY);
    printf("Samples per day: %d\n", SAMPLES_PER_DAY);
    
    // Initialize message counter
    time_t current_time = FLEX_TimeGet();
    last_reset_day = current_time / (24 * 3600);
    
    // Read current diagnostics
    uint32_t msg_count = 0;
    if (FLEX_DiagConfValueRead(DIAG_MESSAGES_SENT_TODAY, &msg_count) == 0) {
        printf("Previous messages sent today: %lu\n", msg_count);
        messages_sent_today = msg_count;
    }
    
    printf("Starting temperature monitoring...\n\n");
    
    // Schedule first sample
    FLEX_JobSchedule(temperature_sample_and_send, FLEX_ASAP());
}
