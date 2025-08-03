// SCHC Temperature Sensor - Simple Implementation
// Based on RFC 8724 - Static Context Header Compression and Fragmentation

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "flex.h"

#define APPLICATION_NAME "SCHC Temperature Sensor"

// SCHC Configuration
#define MTU_SIZE 20                   // Maximum packet size in bytes
#define RULE_ID 0x01                  // Rule ID (2 bits)
#define FCN_FINAL 0x3F                // FCN for final fragment (6 bits = 111111)
#define MAX_FRAGMENTS 64              // Maximum fragments allowed (6 bits)
#define MAX_MESSAGES_PER_DAY 24       // Daily message limit

// Message tracking
static uint8_t messages_sent_today = 0;
static uint32_t last_reset_day = 0;

//CRC-32 para RCS como recomendado por RFC 8724
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

// leer temp FlexSense
static int16_t read_temperature(void) {
    float temp_celsius;
    int result = FLEX_TemperatureGet(&temp_celsius);
    if (result == 0) {  // FLEX_SUCCESS = 0
        return (int16_t)temp_celsius;  // Float a Integer
    } else {
        printf("Error del sensor de temperatura: %d\n", result);
        return 99;  // Temperatura por defecto si falla el sensor
    }
}

// Creacion mensaje
static char* create_message(int16_t temp) {
    static char message[85];  // Increased buffer size
    snprintf(message, 85, "Test de fragmentacion, la temperatura del sensor flex es: %d grados celsius", temp);
    return message;
}

// Send SCHC fragment
static int send_fragment(uint8_t fragment_num, uint8_t total_fragments, 
                        const uint8_t *data, uint8_t data_len, 
                        const uint8_t *original_message, uint8_t msg_len) {
    uint8_t packet[MTU_SIZE];
    memset(packet, 0, MTU_SIZE);
    
    bool is_final = (fragment_num == total_fragments - 1);
    uint8_t fcn = is_final ? FCN_FINAL : fragment_num;
    
    if (is_final) {
        // Final fragment: RuleID(2)|FCN(6)|RCS(32)|payload|padding
        uint32_t rcs = calculate_crc32(original_message, msg_len);
        
        // Pack: RuleID(2)|FCN(6) = 8 bits in byte 0, then RCS(32) in bytes 1-4
        packet[0] = (RULE_ID << 6) | (fcn & 0x3F);  // RuleID(2) + FCN(6)
        packet[1] = (rcs >> 24) & 0xFF;  // RCS bits 31-24
        packet[2] = (rcs >> 16) & 0xFF;  // RCS bits 23-16
        packet[3] = (rcs >> 8) & 0xFF;   // RCS bits 15-8
        packet[4] = rcs & 0xFF;          // RCS bits 7-0
        
        // Copy payload starting from byte 5
        uint8_t payload_space = MTU_SIZE - 5;  // 15 bytes for payload
        uint8_t copy_len = (data_len <= payload_space) ? data_len : payload_space;
        memcpy(&packet[5], data, copy_len);
        
        printf("Fragmento final %d: Número identificador de fragmento=%d, RCS=0x%08lX, carga útil=%d bytes\n", 
               fragment_num, fcn, (unsigned long)rcs, copy_len);
    } else {
        // Normal fragment: RuleID(2)|FCN(6)|payload
        packet[0] = (RULE_ID << 6) | (fcn & 0x3F);  // RuleID(2) + FCN(6)
        
        // Copy payload starting from byte 1  
        uint8_t payload_space = MTU_SIZE - 1;
        uint8_t copy_len = (data_len <= payload_space) ? data_len : payload_space;
        memcpy(&packet[1], data, copy_len);
        
        printf("Fragmento %d: Número identificador de fragmento=%d, carga útil=%d bytes\n", 
               fragment_num, fcn, copy_len);
    }
    
    // Imprimir el paquete binario exacto que se envía (NO cadena hex)
    printf("Paquete binario (20 bytes): ");
    for (int i = 0; i < MTU_SIZE; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");
    
    // Mostrar carga útil como texto (si es imprimible)
    printf("Texto de carga útil: \"");
    uint8_t payload_start = is_final ? 5 : 1;
    for (int i = payload_start; i < MTU_SIZE; i++) {
        if (packet[i] >= 32 && packet[i] <= 126) {  // ASCII imprimible
            printf("%c", packet[i]);
        } else if (packet[i] == 0) {
            break;  // Parar en el relleno
        } else {
            printf(".");
        }
    }
    printf("\"\n");
    printf("FLEX convertirá este binario a hex para transmisión satelital\n");
    
    return FLEX_MessageSchedule(packet, MTU_SIZE);
}

// Check if we can send more messages today
static bool can_send_message_today(void) {
    time_t current_time = FLEX_TimeGet();
    uint32_t current_day = current_time / (24 * 3600);
    
    // Reset counter if it's a new day
    if (current_day != last_reset_day) {
        messages_sent_today = 0;
        last_reset_day = current_day;
        printf("Nuevo día - reiniciando contador de mensajes\n");
    }
    
    return messages_sent_today < MAX_MESSAGES_PER_DAY;
}

// Fragment and send message
static void send_schc_message(const uint8_t *message, uint8_t msg_len) {
    printf("Mandando mensaje: %s\n", message);
    printf("Largo del mensaje: %d bytes\n", msg_len);
    
    if (msg_len <= MTU_SIZE - 1) {
        printf("Mensaje más pequeño que MTU, no hay que fragmentar\n");
        // Enviar como un solo paquete
        send_fragment(0, 1, message, msg_len, message, msg_len);
        messages_sent_today++;
        return;
    }
    
    printf("Fragmentación necesaria\n");
    
    // Calculate fragments needed
    uint8_t first_fragment_payload = MTU_SIZE - 1;  // 19 bytes
    uint8_t final_fragment_payload = MTU_SIZE - 5;  // 15 bytes (CRC-32 takes 5 bytes)
    uint8_t remaining_after_first = msg_len - first_fragment_payload;
    uint8_t middle_fragments = 0;
    
    if (remaining_after_first > final_fragment_payload) {
        middle_fragments = (remaining_after_first - final_fragment_payload + first_fragment_payload - 1) / first_fragment_payload;
    }
    
    uint8_t total_fragments = 1 + middle_fragments + 1;  // first + middle + final
    
    if (total_fragments > MAX_FRAGMENTS) {
        printf("Error: Demasiados fragmentos necesarios (%d > %d)\n", total_fragments, MAX_FRAGMENTS);
        return;
    }
    
    printf("Total de fragmentos: %d\n", total_fragments);
    
    uint8_t offset = 0;
    
    // Send fragments
    for (uint8_t i = 0; i < total_fragments; i++) {
        if (messages_sent_today >= MAX_MESSAGES_PER_DAY) {
            printf("Límite diario de mensajes alcanzado\n");
            break;
        }
        
        bool is_final = (i == total_fragments - 1);
        uint8_t payload_size;
        
        if (is_final) {
            payload_size = msg_len - offset;
            if (payload_size > MTU_SIZE - 5) {  // CRC-32 takes 5 bytes
                payload_size = MTU_SIZE - 5;
            }
        } else {
            payload_size = MTU_SIZE - 1;
            if (offset + payload_size > msg_len) {
                payload_size = msg_len - offset;
            }
        }
        
        int result = send_fragment(i, total_fragments, &message[offset], 
                                  payload_size, message, msg_len);
        
        if (result == 0) {
            printf("Fragmento %d enviado exitosamente\n", i);
            messages_sent_today++;
            offset += payload_size;
        } else {
            printf("Falló el envío del fragmento %d\n", i);
            break;
        }
    }
}
    
// Main
static time_t send_temperature_message(void) {
    // Verificar límite diario con reinicio automático
    if (!can_send_message_today()) {
        printf("Límite diario de mensajes alcanzado (%d/%d)\n", 
               messages_sent_today, MAX_MESSAGES_PER_DAY);
        return FLEX_HoursFromNow(6);  // Intentar de nuevo en 6 horas (próxima hora programada)
    }
    
    printf("Leyendo sensor de temperatura...\n");
    int16_t temperature = read_temperature();
    printf("Temperatura: %d grados celsius\n", temperature);
    
    // Crear mensaje
    char *message_text = create_message(temperature);
    uint8_t msg_len = strlen(message_text);
    printf("Mensaje creado: %d bytes\n", msg_len);
    
    // Enviar vía SCHC
    send_schc_message((uint8_t*)message_text, msg_len);
    
    printf("Mensajes enviados hoy: %d/%d\n", 
           messages_sent_today, MAX_MESSAGES_PER_DAY);
    printf("Próxima transmisión en 6 horas\n");
    printf("=================================\n\n");
    
    // Programar próxima transmisión (4 veces por día = cada 6 horas)
    return FLEX_HoursFromNow(6);
}

void FLEX_AppInit() {
    printf("%s\n", APPLICATION_NAME);
    printf("Tamaño MTU: %d bytes\n", MTU_SIZE);
    printf("Máximo de mensajes por día: %d\n", MAX_MESSAGES_PER_DAY);
    printf("Iniciando monitoreo SCHC de temperatura...\n\n");
    
    // Inicializar seguimiento de días
    time_t current_time = FLEX_TimeGet();
    last_reset_day = current_time / (24 * 3600);
    
    // Programar primera transmisión
    FLEX_JobSchedule(send_temperature_message, FLEX_ASAP());
}
