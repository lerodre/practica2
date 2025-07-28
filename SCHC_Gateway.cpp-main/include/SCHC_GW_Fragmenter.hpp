#ifndef SCHC_GW_Fragmenter_hpp
#define SCHC_GW_Fragmenter_hpp

#include "SCHC_GW_Macros.hpp"
#include "SCHC_GW_Stack_L2.hpp"
#include "SCHC_GW_TTN_MQTT_Stack.hpp"
#include "SCHC_GW_Session.hpp"
#include <cstdint>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <mosquitto.h>
#include "SCHC_GW_ThreadSafeQueue.hpp"
#include "SCHC_GW_TTN_Parser.hpp"
#include <random>

class SCHC_GW_Fragmenter
{
    public:
        uint8_t     set_mqtt_stack(mosquitto* mosqStack);
        uint8_t     initialize(uint8_t protocol, uint8_t ack_mode, uint8_t error_prob = 0);
        uint8_t     listen_messages(char *buffer);
        uint8_t     disassociate_session_id(std::string deviceId);      
    private:
        int         get_free_session_id(uint8_t direction);
        uint8_t     associate_session_id(std::string deviceId, int sessionId);
        int         get_session_id(std::string deviceId);
        uint8_t                                 _protocol;
        SCHC_GW_Session                         _uplinkSessionPool[_SESSION_POOL_SIZE];
        SCHC_GW_Session                         _downlinkSessionPool[_SESSION_POOL_SIZE];
        SCHC_GW_Stack_L2*                          _stack;
        std::unordered_map<std::string, int>    _associationMap;
        struct mosquitto*                       _mosq;
        uint8_t                                 _error_prob;
};
#endif  // SCHC_GW_Fragmenter_hpp