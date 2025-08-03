// SCHC enviador de imagenes - soporte de fragmentos grandes
// basado en RFC 8724 con identificador extendido para imagenes grandes

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "flex.h"

#define APPLICATION_NAME "SCHC Image Sender"

// configuracion SCHC para identificador de 6-bit
#define MTU_SIZE 20                   // tamano maximo de paquete en bytes
#define RULE_ID 0x01                  // rule ID (2 bits)
#define FCN_ALL1 0x3F                 // identificador para fragmento final (6 bits = 63 = All-1)
#define MAX_FRAGMENTS 64              // maximo fragmentos permitidos (6 bits = 0-63)
#define MAX_MESSAGES_PER_DAY 20       // limite diario de mensajes (conservador)

// configuracion de imagen
#define IMAGE_SIZE 711                // tamano de imagen comprimida (actualizado)
#define FRAGMENTS_PER_SESSION 20      // enviar 20 fragmentos por sesion (usar limite diario completo)
#define HOURS_BETWEEN_SESSIONS 24     // esperar 24 horas entre sesiones (una vez por dia)

// seguimiento de mensajes
static uint16_t messages_sent_today = 0;
static uint16_t current_fragment = 0;
static bool transmission_complete = false;
static uint16_t last_reset_day = 0;  // rastrear que dia reseteamos el contador por ultima vez

// datos de imagen embebidos
// esto es un placeholder - necesitas convertir tu imagen a un arreglo C
static const uint8_t compressed_image[711] = {
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,  // Offset 0000
    0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x50, 0x37, 0x3C, 0x46, 0x3C, 0x32, 0x50,  // Offset 0010
    0x46, 0x41, 0x46, 0x5A, 0x55, 0x50, 0x5F, 0x78, 0xC8, 0x82, 0x78, 0x6E, 0x6E, 0x78, 0xF5, 0xAF,  // Offset 0020
    0xB9, 0x91, 0xC8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Offset 0030
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Offset 0040
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x55, 0x5A,  // Offset 0050
    0x5A, 0x78, 0x69, 0x78, 0xEB, 0x82, 0x82, 0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Offset 0060
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Offset 0070
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Offset 0080
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0,  // Offset 0090
    0x00, 0x11, 0x08, 0x00, 0x5A, 0x00, 0x5A, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,  // Offset 00A0
    0x01, 0xFF, 0xC4, 0x00, 0x18, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Offset 00B0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0xFF, 0xC4, 0x00, 0x2A, 0x10,  // Offset 00C0
    0x00, 0x02, 0x02, 0x02, 0x03, 0x00, 0x01, 0x02, 0x04, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Offset 00D0
    0x00, 0x01, 0x02, 0x11, 0x03, 0x31, 0x12, 0x21, 0x41, 0x51, 0x04, 0x22, 0x13, 0x32, 0x33, 0x71,  // Offset 00E0
    0x42, 0x61, 0x81, 0x91, 0xA1, 0xB1, 0xF0, 0xFF, 0xC4, 0x00, 0x14, 0x01, 0x01, 0x00, 0x00, 0x00,  // Offset 00F0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xC4, 0x00,  // Offset 0100
    0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Offset 0110
    0x00, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3F,  // Offset 0120
    0x00, 0xE8, 0x01, 0x80, 0x00, 0xC4, 0x30, 0x14, 0xA4, 0xA2, 0xAE, 0x4E, 0x91, 0x11, 0xCF, 0x8E,  // Offset 0130
    0x4E, 0x94, 0xBB, 0xFD, 0x8C, 0x7E, 0xAE, 0x4D, 0xCA, 0x31, 0x46, 0x71, 0xC1, 0x26, 0xDA, 0x6E,  // Offset 0140
    0xAB, 0x6C, 0x0E, 0xE1, 0x19, 0x61, 0x72, 0x8C, 0x54, 0x64, 0x6A, 0x9A, 0x6A, 0xD0, 0x00, 0x86,  // Offset 0150
    0x00, 0x20, 0x00, 0x01, 0x80, 0x18, 0xE7, 0xCB, 0xC1, 0x52, 0xDB, 0x01, 0xE5, 0xCB, 0xC3, 0xA5,  // Offset 0160
    0xF9, 0x8E, 0x6E, 0x4F, 0xE4, 0x96, 0xC4, 0x06, 0xB8, 0x14, 0x5C, 0x9A, 0x97, 0xA6, 0xB8, 0xB2,  // Offset 0170
    0x72, 0x97, 0x0D, 0x9C, 0xCA, 0xDC, 0xBA, 0xD9, 0x71, 0x9F, 0x09, 0xF5, 0xAF, 0x5F, 0xC8, 0x1B,  // Offset 0180
    0xD3, 0x4D, 0xF1, 0x5B, 0x29, 0x39, 0x70, 0xFB, 0x55, 0xFF, 0x00, 0x52, 0x65, 0x3C, 0x7C, 0x5C,  // Offset 0190
    0x9B, 0xE4, 0xFF, 0x00, 0xB1, 0x78, 0xA3, 0xC6, 0x0A, 0xF7, 0xE8, 0x16, 0x00, 0x00, 0x20, 0x00,  // Offset 01A0
    0x01, 0x99, 0x66, 0xC3, 0xF8, 0x8A, 0xD6, 0xD1, 0xA9, 0x19, 0x65, 0xC2, 0x0D, 0xFA, 0x07, 0x0D,  // Offset 01B0
    0x8D, 0x2B, 0x15, 0x0D, 0x27, 0xF2, 0x03, 0x92, 0xE3, 0x69, 0x7A, 0x4F, 0xB4, 0x8A, 0xB3, 0x79,  // Offset 01C0
    0xE2, 0x8C, 0x78, 0x2B, 0xA7, 0xD8, 0x19, 0x49, 0x2E, 0x37, 0xC6, 0xBE, 0x1D, 0x9D, 0x91, 0x55,  // Offset 01D0
    0x14, 0x89, 0x8A, 0x5F, 0x87, 0x4F, 0x41, 0x89, 0xB7, 0x1A, 0x7B, 0x5B, 0x02, 0xC0, 0x03, 0xC0,  // Offset 01E0
    0x10, 0x0C, 0x40, 0x4D, 0xC9, 0x7F, 0x0F, 0xF9, 0x39, 0xFE, 0xA2, 0x4D, 0xC9, 0x45, 0xF8, 0x75,  // Offset 01F0
    0x36, 0x97, 0x6C, 0xE1, 0x93, 0xB7, 0x60, 0x20, 0x00, 0x02, 0xF0, 0xFE, 0xA2, 0xB5, 0x68, 0xDD,  // Offset 0200
    0xCF, 0x96, 0x4D, 0x75, 0x1D, 0xF6, 0x4F, 0xD3, 0x26, 0x93, 0x7F, 0x23, 0x8D, 0x4F, 0x24, 0x9F,  // Offset 0210
    0xAF, 0x60, 0x68, 0xA4, 0xB8, 0xA4, 0xD1, 0x12, 0x6A, 0x13, 0x52, 0x5D, 0xDE, 0xCD, 0x12, 0xAA,  // Offset 0220
    0x26, 0x71, 0x4E, 0x0D, 0x00, 0xF9, 0xFC, 0xF4, 0x82, 0x39, 0x23, 0x2F, 0x49, 0x83, 0x52, 0x87,  // Offset 0230
    0x63, 0xE3, 0x10, 0x34, 0x74, 0xB6, 0x2B, 0x44, 0xD2, 0x1F, 0x40, 0x33, 0x9B, 0x3E, 0x35, 0x16,  // Offset 0240
    0xB8, 0xED, 0x9D, 0x26, 0x19, 0x9F, 0xDD, 0xFB, 0x01, 0xCE, 0x9A, 0x05, 0xB6, 0x5E, 0x48, 0x2D,  // Offset 0250
    0xA0, 0xC5, 0x8E, 0xE4, 0x07, 0x4C, 0x7E, 0xCC, 0x7A, 0xD0, 0xB0, 0xC7, 0x8C, 0x37, 0x68, 0x79,  // Offset 0260
    0x22, 0xE5, 0x1E, 0x2B, 0x6C, 0x71, 0x54, 0xBA, 0x54, 0x05, 0x08, 0x00, 0x08, 0x49, 0x46, 0x6F,  // Offset 0270
    0xBA, 0xE5, 0xA4, 0x59, 0x19, 0x23, 0x71, 0xE9, 0x77, 0xE1, 0x7D, 0xFA, 0xA9, 0x80, 0x86, 0x20,  // Offset 0280
    0xEC, 0x06, 0x65, 0x96, 0xB9, 0x7F, 0xB3, 0x69, 0x68, 0xE7, 0x9E, 0xD8, 0x15, 0x55, 0x8E, 0xD8,  // Offset 0290
    0xA0, 0xAA, 0x6B, 0xBB, 0xB0, 0x9F, 0xE8, 0x3F, 0xFB, 0xD2, 0xA1, 0xB4, 0x06, 0x8B, 0xF3, 0x2F,  // Offset 02A0
    0xE4, 0x00, 0xB5, 0x21, 0x7A, 0xC0, 0x7A, 0x00, 0x17, 0xA0, 0x00, 0xB4, 0x30, 0xF4, 0x05, 0x5D,  // Offset 02B0
    0x08, 0x23, 0xB6, 0x58, 0x1F, 0xFF, 0xD9,  // Offset 02C0
};

//CRC-32 
static uint32_t calculate_crc32(const uint8_t *data, uint16_t length) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;  // polinomio CRC-32
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

// verificar si podemos enviar mensajes hoy y resetear contador si es nuevo dia
static bool can_send_messages_today(void) {
    time_t now = FLEX_TimeGet();
    uint16_t current_day = (uint16_t)(now / 86400);  // dias desde epoch
    
    // verificar si es un dia nuevo
    if (current_day != last_reset_day) {
        printf("Nuevo día detectado (día %d -> %d), reiniciando contador diario de mensajes\n", 
               last_reset_day, current_day);
        printf("Progreso de imagen: fragmento %d (continuará transmisión)\n", current_fragment);
        messages_sent_today = 0;  // solo resetear contador de mensajes, mantener progreso de imagen
        last_reset_day = current_day;
        
        // solo resetear estado de transmision si imagen previa estaba completa Y queremos nueva imagen
        if (transmission_complete) {
            printf("Imagen anterior completa, preparando para nueva transmisión\n");
            current_fragment = 0;
            transmission_complete = false;
        }
    }
    
    return messages_sent_today < MAX_MESSAGES_PER_DAY;
}

// enviar fragmento SCHC con FCN de 6-bit 
static int send_image_fragment(uint16_t fragment_num, uint16_t total_fragments, 
                              const uint8_t *data, uint8_t data_len) {
    uint8_t packet[MTU_SIZE];
    memset(packet, 0, MTU_SIZE);
    
    bool is_final = (fragment_num == total_fragments - 1);
    uint8_t fcn;
    
    if (is_final) {
        // fragmento final siempre usa identificador 63 (patron All-1 para 6 bits)
        fcn = FCN_ALL1;
    } else {
        // orden fijo: fragmento 0 = 62, fragmento 1 = 61, fragmento 2 = 60, etc.
        // FCN siempre comienza en 62 y disminuye, sin importar total de fragmentos
        fcn = 62 - fragment_num;
        
        // verificacion de seguridad: identificador debe estar en rango 0-62 para fragmentos normales
        if (fcn > 62) {
            printf("ERROR: Demasiados fragmentos para identificador de 6-bit! Fragmento %d necesitaría identificador %d\n", 
                   fragment_num, fcn);
            return -1;
        }
    }
    
    if (is_final) {
        // fragmento final: RuleID(2)|FCN(6)|RCS(32)|payload|padding
        uint32_t rcs = calculate_crc32(compressed_image, IMAGE_SIZE);
        
        // empacar: RuleID(2) + FCN(6) en byte 0, luego RCS(32) en bytes 1-4
        packet[0] = (RULE_ID << 6) | (fcn & 0x3F);  // RuleID(2) + FCN[5:0]
        packet[1] = (rcs >> 24) & 0xFF;  // RCS[31:24]
        packet[2] = (rcs >> 16) & 0xFF;  // RCS[23:16]
        packet[3] = (rcs >> 8) & 0xFF;   // RCS[15:8]
        packet[4] = rcs & 0xFF;          // RCS[7:0]
        
        // copiar payload comenzando desde byte 5
        uint8_t payload_space = MTU_SIZE - 5;  // 15 bytes para payload
        uint8_t copy_len = (data_len <= payload_space) ? data_len : payload_space;
        memcpy(&packet[5], data, copy_len);
        
        printf("Fragmento final %d: identificador=%d (All-1), RCS=0x%08lX, carga útil=%d bytes\n", 
               fragment_num, fcn, (unsigned long)rcs, copy_len);
    } else {
        // fragmento normal: RuleID(2)|FCN(6)|payload
        packet[0] = (RULE_ID << 6) | (fcn & 0x3F);  // RuleID(2) + FCN[5:0]
        
        // copiar payload comenzando desde byte 1
        uint8_t payload_space = MTU_SIZE - 1;  // 19 bytes para payload
        uint8_t copy_len = (data_len <= payload_space) ? data_len : payload_space;
        memcpy(&packet[1], data, copy_len);
        
        printf("Fragmento %d: identificador=%d, carga útil=%d bytes\n", 
               fragment_num, fcn, copy_len);
    }
    
    // imprimir el paquete binario exacto que se envia
    printf("Paquete binario (20 bytes): ");
    for (int i = 0; i < MTU_SIZE; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");
    
    printf("FLEX convertirá este binario a hex para transmisión satelital\n");
    
    return FLEX_MessageSchedule(packet, MTU_SIZE);
}

// enviar lote de fragmentos de imagen
static void send_image_batch(void) {
    if (transmission_complete) {
        printf("Transmisión de imagen ya completa\n");
        return;
    }
    
    // calcular fragmentos totales necesarios (FCN de 6-bit permite max 64 fragmentos: 0-63)
    uint16_t fragments_needed = (IMAGE_SIZE + 18) / 19;  // 19 bytes por fragmento normal
    
    // verificar si cabe en FCN de 6-bit (max 64 fragmentos)
    if (fragments_needed > MAX_FRAGMENTS) {
        printf("ERROR: Demasiados fragmentos para identificador de 6-bit (máx 63 fragmentos)\n");
        return;
    }
    
    uint16_t fragments_sent_this_session = 0;
    
    printf("=== SESIÓN DE TRANSMISIÓN DE IMAGEN (identificador de 6-bit) ===\n");
    printf("Tamaño de imagen: %d bytes\n", IMAGE_SIZE);
    printf("Total de fragmentos necesarios: %d (cabe en identificador de 6-bit)\n", fragments_needed);
    printf("Comenzando desde fragmento: %d\n", current_fragment);
    printf("Mapeo identificador: Fragmento 0→ID 62, Fragmento 1→ID 61, ..., Final→ID 63\n");
    
    // enviar fragmentos en lotes
    while (current_fragment < fragments_needed && 
           fragments_sent_this_session < FRAGMENTS_PER_SESSION &&
           messages_sent_today < MAX_MESSAGES_PER_DAY) {
        
        bool is_final = (current_fragment == fragments_needed - 1);
        uint16_t offset = current_fragment * 19;  // 19 bytes por fragmento
        uint8_t payload_size;
        
        if (is_final) {
            // fragmento final - limitado por overhead de CRC (5 bytes: 1 header + 4 CRC)
            uint16_t remaining = IMAGE_SIZE - offset;
            payload_size = (remaining <= 15) ? remaining : 15;  // 15 bytes max para final
        } else {
            // fragmento normal (1 byte header)
            uint16_t remaining = IMAGE_SIZE - offset;
            payload_size = (remaining >= 19) ? 19 : remaining;
        }
        
        int result = send_image_fragment(current_fragment, fragments_needed,
                                       &compressed_image[offset], payload_size);
        
        if (result == 0) {
            printf("Fragmento %d enviado exitosamente\n", current_fragment);
            current_fragment++;
            fragments_sent_this_session++;
            messages_sent_today++;
            
            if (current_fragment >= fragments_needed) {
                transmission_complete = true;
                printf("=== TRANSMISIÓN DE IMAGEN COMPLETA ===\n");
                break;
            }
        } else {
            printf("Falló el envío del fragmento %d\n", current_fragment);
            break;
        }
    }
    
    printf("Sesión completa: enviados %d fragmentos\n", fragments_sent_this_session);
    printf("Progreso: %d/%d fragmentos (%d%%)\n", 
           current_fragment, fragments_needed, 
           (current_fragment * 100) / fragments_needed);
    printf("Mensajes enviados hoy: %d/%d\n", messages_sent_today, MAX_MESSAGES_PER_DAY);
}

// funcion principal de transmision
static time_t send_image_session(void) {
    // verificar limite diario con reset automatico
    if (!can_send_messages_today()) {
        printf("Límite diario de mensajes alcanzado (%d/%d)\n", 
               messages_sent_today, MAX_MESSAGES_PER_DAY);
        return FLEX_HoursFromNow(24);  // intentar manana de nuevo
    }
    
    if (transmission_complete) {
        printf("Transmisión de imagen completa. Iniciando nueva imagen inmediatamente.\n");
        // resetear para siguiente imagen inmediatamente, no esperar siguiente dia
        current_fragment = 0;
        transmission_complete = false;
        // no resetear messages_sent_today aqui - dejar que reset diario lo maneje
    }
    
    // enviar lote de fragmentos
    send_image_batch();
    
    printf("Próxima sesión en %d horas\n", HOURS_BETWEEN_SESSIONS);
    printf("=================================\n\n");
    
    // programar siguiente sesion
    return FLEX_HoursFromNow(HOURS_BETWEEN_SESSIONS);
}

void FLEX_AppInit() {
    printf("%s\n", APPLICATION_NAME);
    printf("=== configuracion identificador de 6-BIT ===\n");
    printf("Tamaño de imagen: %d bytes\n", IMAGE_SIZE);
    printf("Tamaño MTU: %d bytes\n", MTU_SIZE);
    printf("Máximo de fragmentos (identificador de 6-bit): %d (0-63)\n", MAX_FRAGMENTS);
    printf("Identificador de fragmento final: %d (All-1)\n", FCN_ALL1);
    printf("Fragmentos por sesión: %d\n", FRAGMENTS_PER_SESSION);
    printf("Horas entre sesiones: %d\n", HOURS_BETWEEN_SESSIONS);
    
    // calcular y mostrar requerimientos de fragmentos
    uint16_t fragments_needed = (IMAGE_SIZE + 18) / 19;
    printf("Fragmentos requeridos para esta imagen: %d\n", fragments_needed);
    
    if (fragments_needed > MAX_FRAGMENTS) {
        printf("ERROR: ¡La imagen requiere %d fragmentos pero identificador de 6-bit solo permite %d!\n",
               fragments_needed, MAX_FRAGMENTS);
        printf("Considera reducir el tamaño de imagen o usar fragmentos más grandes.\n");
        return;
    }
    
    printf("Mapeo identificador fijo: Fragmento 0→ID 62, Fragmento 1→ID 61, Fragmento 2→ID 60, ..., Final→ID 63\n");
    
    // inicializar seguimiento de reset diario
    time_t now = FLEX_TimeGet();
    last_reset_day = (uint16_t)(now / 86400);
    printf("Inicializado en día %d\n", last_reset_day);
    
    printf("Iniciando transmisión de imagen con identificador de 6-bit...\n\n");
    
    // programar primera sesion de transmision
    FLEX_JobSchedule(send_image_session, FLEX_ASAP());
}