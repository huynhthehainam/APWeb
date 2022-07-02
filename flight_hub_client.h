#pragma once
#include "includes.h"

void add_flight_hub_record(mavlink_message_t *gps_msg, mavlink_message_t *imu_msg);
char *load_device_token();

bool connect_to_flight_hub();
int count_records();
void send_to_flight_hub();