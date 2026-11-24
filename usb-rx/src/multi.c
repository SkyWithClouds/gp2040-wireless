/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include "air.h"
#include "hub.h"
#include "match_usb.h"
struct lane {

    struct delivery transfer;
    unsigned completion;
    bool submitted;
};
static struct lane lanes[4];
static struct receiver receivers[4];
static void tick_lane(unsigned i,uint32_t now)
{
    struct lane *l=&lanes[i];
    bool online=receivers[i].session!=0;
    receiver_tick(&receivers[i],now);
    if(online && !receivers[i].session) match_stats[i].timeouts++;
    if(l->submitted && match_usb_completions(i)!=l->completion) {
        receiver_complete(&receivers[i],&l->transfer,now);l->submitted=false;
    }
    if(!l->submitted && receiver_peek(&receivers[i],&l->transfer)) {
        l->completion=match_usb_completions(i);
        l->submitted=match_usb_submit(i,&l->transfer.input);
    }
    match_stats[i].online=receivers[i].session && receivers[i].done_valid;
}
int main(void)
{
    match_init(PC_RX);
    const unsigned slots=match_saved.slots;
    int rc=match_usb_open(slots);
    if(rc) return rc;
    rc=air_start(true);if(rc) return rc;
    unsigned epoch=~0u;
    for(;;) {
        uint32_t now=k_uptime_get_32();
        unsigned current=match_usb_epoch();
        if(current!=epoch) {
            epoch=current;
            for(unsigned i=0;i<slots;i++) {
                receiver_reset(&receivers[i]);lanes[i].submitted=false;
            }
        }
        for(unsigned i=0;i<slots;i++) tick_lane(i,now);
        struct gp_frame f;uint8_t pipe;
        while(air_receive_pipe(&pipe,&f)) {
            if(!match_usb_ready() || pipe<1 || pipe>slots || f.type==GP_STATUS || f.type==GP_OFFER) continue;
            struct gp_frame reply;
            if(!hub_route(receivers,match_stats,slots,pipe,f,now,match_usb_lamp(pipe-1),air_nonce,&reply))
                continue;
            air_send_pipe(pipe,&reply);
        }
        match_usb_service();k_sleep(K_USEC(180));
    }
}
