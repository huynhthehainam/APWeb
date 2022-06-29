
#include "includes.h"

struct flight_hub_packets
{
    struct flight_hub_packets *next;
    mavlink_message_t coordinate_msg;
    uint32_t receive_ms;
};

