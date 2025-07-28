#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>

// SCHC Configuration (matching the sender)
#define SCHC_MTU_BYTES 20             // Maximum Transmission Unit
#define SCHC_RULE_ID_BITS 2           // Rule ID size in bits
#define SCHC_FCN_BITS 3               // Fragment Counter size in bits
#define SCHC_RCS_BITS 8               // Reassembly Check Sequence size in bits

// SCHC Values
#define SCHC_RULE_ID_TEMP 0x01        // Rule ID for temperature messages
#define SCHC_FCN_FINAL 0x07           // FCN for final fragment (111 = All-1)
#define MAX_FRAGMENTS 16              // Maximum fragments per message
#define MAX_MESSAGE_SIZE 512          // Maximum reassembled message size

// Fragment structure for receiver
typedef struct {
    uint8_t rule_id;
    uint8_t fcn;
    uint8_t rcs;
    uint8_t payload[SCHC_MTU_BYTES];
    uint8_t payload_len;
    bool is_final;
    bool is_valid;
} received_fragment_t;

// Message reassembly buffer
typedef struct {
    uint8_t data[MAX_MESSAGE_SIZE];
    uint16_t total_length;
    bool fragments_received[MAX_FRAGMENTS];
    received_fragment_t fragments[MAX_FRAGMENTS];
    uint8_t expected_fragments;
    bool is_complete;
    uint8_t calculated_rcs;
} reassembly_buffer_t;

// CRC-8 calculation (same as sender)
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

// Convert hex string to bytes
static int hex_to_bytes(const char *hex_str, uint8_t *bytes, int max_bytes) {
    int len = strlen(hex_str);
    if (len % 2 != 0) {
        printf("Error: Hex string length must be even\n");
        return -1;
    }
    
    int byte_count = len / 2;
    if (byte_count > max_bytes) {
        printf("Error: Hex string too long for buffer\n");
        return -1;
    }
    
    for (int i = 0; i < byte_count; i++) {
        char byte_str[3] = {hex_str[i*2], hex_str[i*2+1], '\0'};
        bytes[i] = (uint8_t)strtol(byte_str, NULL, 16);
    }
    
    return byte_count;
}

// Parse a single SCHC fragment from hex data
static bool parse_fragment(const char *hex_data, received_fragment_t *fragment) {
    uint8_t packet[SCHC_MTU_BYTES];
    int packet_len = hex_to_bytes(hex_data, packet, SCHC_MTU_BYTES);
    
    if (packet_len != SCHC_MTU_BYTES) {
        printf("Error: Fragment must be exactly %d bytes (%d hex chars)\n", 
               SCHC_MTU_BYTES, SCHC_MTU_BYTES * 2);
        return false;
    }
    
    // Extract Rule ID (bits 7-6 of first byte)
    fragment->rule_id = (packet[0] >> 6) & 0x03;
    
    // Extract FCN (bits 5-3 of first byte)
    fragment->fcn = (packet[0] >> 3) & 0x07;
    
    // Check if this is the final fragment (All-1)
    fragment->is_final = (fragment->fcn == SCHC_FCN_FINAL);
    
    printf("Parsed fragment: RuleID=%d, FCN=%d, is_final=%s\n", 
           fragment->rule_id, fragment->fcn, fragment->is_final ? "yes" : "no");
    
    if (fragment->is_final) {
        // Final fragment: RuleID(2)|FCN(3)|RCS(8)|5_reserved_bits + Payload
        // RCS: upper 3 bits from byte 0 (bits 2-0), lower 5 bits from byte 1 (bits 7-3)
        fragment->rcs = ((packet[0] & 0x07) << 5) | ((packet[1] >> 3) & 0x1F);
        
        // Payload starts from byte 2 (simplified byte-aligned approach)
        fragment->payload_len = 0;
        for (int i = 2; i < SCHC_MTU_BYTES; i++) {
            if (packet[i] != 0 || fragment->payload_len > 0) {  // Include zeros in middle of data
                fragment->payload[fragment->payload_len++] = packet[i];
            }
        }
        
        // Remove trailing padding (zeros at the end)
        while (fragment->payload_len > 0 && fragment->payload[fragment->payload_len - 1] == 0) {
            fragment->payload_len--;
        }
        
        printf("Final fragment RCS: 0x%02X, payload_len: %d\n", fragment->rcs, fragment->payload_len);
    } else {
        // Intermediate fragment: RuleID(2)|FCN(3)|3_reserved_bits + Payload
        fragment->rcs = 0;
        
        // Payload starts from byte 1
        fragment->payload_len = 0;
        for (int i = 1; i < SCHC_MTU_BYTES; i++) {
            if (packet[i] != 0 || fragment->payload_len > 0) {  // Include zeros in middle of data
                fragment->payload[fragment->payload_len++] = packet[i];
            }
        }
        
        // Remove trailing padding (zeros at the end)
        while (fragment->payload_len > 0 && fragment->payload[fragment->payload_len - 1] == 0) {
            fragment->payload_len--;
        }
        
        printf("Intermediate fragment payload_len: %d\n", fragment->payload_len);
    }
    
    fragment->is_valid = true;
    return true;
}

// Reassemble fragments into original message
static bool reassemble_message(reassembly_buffer_t *buffer) {
    // Find the final fragment to get expected fragment count
    int final_fragment_index = -1;
    for (int i = 0; i < MAX_FRAGMENTS; i++) {
        if (buffer->fragments_received[i] && buffer->fragments[i].is_final) {
            final_fragment_index = i;
            buffer->expected_fragments = buffer->fragments[i].fcn + 1; // FCN is 0-based
            break;
        }
    }
    
    if (final_fragment_index == -1) {
        printf("No final fragment found\n");
        return false;
    }
    
    printf("Expected fragments: %d (final fragment FCN: %d)\n", 
           buffer->expected_fragments, buffer->fragments[final_fragment_index].fcn);
    
    // Check if we have all fragments
    for (int fcn = 0; fcn < buffer->expected_fragments; fcn++) {
        bool found = false;
        for (int i = 0; i < MAX_FRAGMENTS; i++) {
            if (buffer->fragments_received[i] && buffer->fragments[i].fcn == fcn) {
                found = true;
                break;
            }
        }
        if (!found) {
            printf("Missing fragment with FCN: %d\n", fcn);
            return false;
        }
    }
    
    // Reassemble in FCN order
    buffer->total_length = 0;
    for (int fcn = 0; fcn < buffer->expected_fragments; fcn++) {
        for (int i = 0; i < MAX_FRAGMENTS; i++) {
            if (buffer->fragments_received[i] && buffer->fragments[i].fcn == fcn) {
                received_fragment_t *frag = &buffer->fragments[i];
                
                // For all fragments, just copy the payload as-is (padding already removed in parsing)
                memcpy(&buffer->data[buffer->total_length], frag->payload, frag->payload_len);
                buffer->total_length += frag->payload_len;
                
                printf("Added fragment FCN=%d, payload_len=%d bytes\n", 
                       fcn, frag->payload_len);
                break;
            }
        }
    }
    
    // Verify CRC
    buffer->calculated_rcs = calculate_crc8(buffer->data, buffer->total_length);
    uint8_t received_rcs = buffer->fragments[final_fragment_index].rcs;
    
    printf("CRC check: calculated=0x%02X, received=0x%02X\n", 
           buffer->calculated_rcs, received_rcs);
    
    if (buffer->calculated_rcs == received_rcs) {
        printf(" CRC verification passed\n");
        buffer->is_complete = true;
        return true;
    } else {
        printf(" CRC verification failed\n");
        return false;
    }
}

// Read fragments from file and decode
static bool process_fragments_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Error: Cannot open file %s\n", filename);
        return false;
    }
    
    printf("Processing fragments from file: %s\n", filename);
    printf("===========================================\n");
    
    reassembly_buffer_t buffer = {0};
    char line[1024];
    int fragment_count = 0;
    
    // Read each line (fragment)
    while (fgets(line, sizeof(line), file) && fragment_count < MAX_FRAGMENTS) {
        // Remove newline and semicolon
        char *pos = strchr(line, '\n');
        if (pos) *pos = '\0';
        pos = strchr(line, ';');
        if (pos) *pos = '\0';
        
        // Skip empty lines
        if (strlen(line) == 0) continue;
        
        printf("\nProcessing fragment %d: %s\n", fragment_count + 1, line);
        
        received_fragment_t fragment = {0};
        if (parse_fragment(line, &fragment)) {
            // Store fragment by FCN (not arrival order)
            if (fragment.fcn < MAX_FRAGMENTS) {
                buffer.fragments[fragment.fcn] = fragment;
                buffer.fragments_received[fragment.fcn] = true;
                fragment_count++;
                printf(" Fragment stored at FCN position %d\n", fragment.fcn);
            } else {
                printf(" Invalid FCN: %d\n", fragment.fcn);
            }
        } else {
            printf(" Failed to parse fragment\n");
        }
    }
    
    fclose(file);
    
    printf("\n===========================================\n");
    printf("Total fragments processed: %d\n", fragment_count);
    
    // Attempt reassembly
    if (reassemble_message(&buffer)) {
        printf("\n Message successfully reassembled!\n");
        printf("Total length: %d bytes\n", buffer.total_length);
        printf("Decoded message: \"%.*s\"\n", buffer.total_length, buffer.data);
        return true;
    } else {
        printf("\n Failed to reassemble message\n");
        return false;
    }
}

int main(int argc, char *argv[]) {
    printf("SCHC No-ACK Fragment Decoder\n");
    printf("============================\n");
    printf("MTU: %d bytes\n", SCHC_MTU_BYTES);
    printf("Rule ID bits: %d, FCN bits: %d, RCS bits: %d\n\n", 
           SCHC_RULE_ID_BITS, SCHC_FCN_BITS, SCHC_RCS_BITS);
    
    if (argc != 2) {
        printf("Usage: %s <fragments_file.txt>\n", argv[0]);
        printf("File format: One hex fragment per line, separated by semicolons or newlines\n");
        printf("Example:\n");
        printf("407468652074656D706572617475726520726513;\n");
        printf("4869737465726564206279207468652073656E13;\n");
        printf("7BF076657220736174656C6C6974650000000000\n");
        return 1;
    }
    
    if (process_fragments_file(argv[1])) {
        printf("\nDecoding completed successfully!\n");
        return 0;
    } else {
        printf("\nDecoding failed!\n");
        return 1;
    }
}