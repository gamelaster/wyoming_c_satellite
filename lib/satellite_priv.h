// Copyright 2024 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#ifndef SATELLITE_PRIV_H_
#define SATELLITE_PRIV_H_

#include <wyoming_user.h>
#include <stdint.h>

#define DATA_BUFFER_SIZE (8096)

struct packet {
    cJSON* header;
    cJSON* data;
    uint8_t* payload;
    uint16_t payload_length;
};

struct wsat_inst_priv {
    uint8_t data_buf[DATA_BUFFER_SIZE];
    uint32_t data_buf_avail_bytes;

    int sockfd;
    int connfd;
};

extern struct wsat_inst_priv wsat_priv;

void wsat_process_data();

#endif