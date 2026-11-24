/* SPDX-License-Identifier: MIT */
#pragma once
#include "match.h"
#include "receiver.h"
static inline bool hub_route(struct receiver receivers[4],struct rf_stats counters[4],
    unsigned slots,uint8_t pipe,struct gp_frame f,uint32_t now,uint32_t lamp,
    uint64_t (*nonce)(void),struct gp_frame *reply)
{
    if(pipe<1 || pipe>slots || f.type==GP_STATUS || f.type==GP_OFFER) return false;
    struct receiver *rx=&receivers[pipe-1];
    struct rf_stats *stats=&counters[pipe-1];
    stats->frames++;
    if(f.session && f.session==rx->session) {
        if(f.type==GP_HEARTBEAT && rx->heartbeat_valid) {
            uint32_t delta=f.data.heartbeat.number-rx->heartbeat;
            if(delta>1 && delta<0x80000000u) stats->gaps+=delta-1;
        }
        if(f.type==GP_INPUT && (uint16_t)(rx->accepted-f.data.input.seq)<GP_WINDOW)
            stats->duplicates++;
    }
    *reply=receiver_frame(rx,&f,now,lamp,nonce);
    return true;
}
