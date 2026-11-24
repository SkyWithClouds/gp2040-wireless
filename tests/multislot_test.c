/* SPDX-License-Identifier: MIT */
#include "hub.h"
#include "layout.h"
#include <assert.h>
#include <stdio.h>
static uint64_t nonce(void) { static uint64_t n=100;return ++n; }
struct bench {
    struct match rx, tx[4];
    struct receiver receivers[4];
    struct rf_stats stats[4];
    struct gp_frame ack[4];
    uint64_t session[4];
};
/* ESB routes by the five on-air address bytes and channel; ACKs are
 * queued per physical pipe and clocked out by the following transaction. */
static bool transmit(struct bench *b,unsigned i,struct gp_frame f,unsigned ms)
{
    struct match m=b->tx[i];
    if(memcmp(m.base,b->rx.base,4) || m.channel!=b->rx.channel ||
       !(match_mask(b->rx,true)&match_mask(m,false))) return false;
    uint8_t bytes[24];
    assert(gp_encode(bytes,24,&f) && gp_decode(&f,bytes,24));
    return hub_route(b->receivers,b->stats,b->rx.slots,m.slots,f,ms,(1u<<24)|i,
                     nonce,&b->ack[m.slots-1]);
}
static void complete(struct bench *b,unsigned i,unsigned ms)
{
    struct delivery d;
    assert(receiver_peek(&b->receivers[i],&d));
    receiver_complete(&b->receivers[i],&d,ms);
}
static void join(struct bench *b,unsigned i,unsigned ms)
{
    struct gp_frame f={.type=GP_HELLO,.data.hello_nonce=nonce()};
    assert(transmit(b,i,f,ms));
    assert(b->ack[i].type==GP_OFFER && b->ack[i].data.hello_nonce==f.data.hello_nonce);
    b->session[i]=b->ack[i].session;
    f=(struct gp_frame){.type=GP_SYNC,.session=b->session[i]};
    assert(transmit(b,i,f,ms));
    complete(b,i,ms);complete(b,i,ms);
}
static void concurrent(unsigned count)
{
    struct bench b={.rx=match_factory(PC_RX)};b.rx.slots=count;
    for(unsigned i=0;i<count;i++) {
        b.tx[i]=match_factory(HITBOX);b.tx[i].slots=i+1;join(&b,i,0);
    }
    for(unsigned seq=1;seq<=100;seq++) {
        for(unsigned i=0;i<count;i++) {
            if(i==0 && seq>10) continue;
            struct gp_frame f={.type=GP_INPUT,.session=b.session[i],
                .data.input={.seq=seq,.buttons=1u<<i}};
            assert(transmit(&b,i,f,seq));assert(b.ack[i].session==b.session[i]);
            if(i || seq<10) complete(&b,i,seq);
            if(seq==5) assert(transmit(&b,i,f,seq));
        }
        for(unsigned i=0;i<count;i++) receiver_tick(&b.receivers[i],seq);
        if(seq==59) assert(b.receivers[0].session);
        if(seq==60) assert(!b.receivers[0].session && b.receivers[0].neutral);
    }
    for(unsigned i=1;i<count;i++) {
        assert(b.receivers[i].done==100 && b.stats[i].duplicates==1);
        struct gp_frame poll={.type=GP_POLL,.session=b.session[i]};
        assert(transmit(&b,i,poll,101) && b.ack[i].data.status.usb_done==100);
        assert(b.ack[i].data.status.led_mode==i);
    }
    struct gp_frame hello={.type=GP_HELLO,.data.hello_nonce=nonce()};
    b.tx[0].channel=80;assert(!transmit(&b,0,hello,102));
    b.rx.channel=80;b.rx.base[0]^=1;assert(!transmit(&b,0,hello,102));
    b.tx[0].base[0]^=1;join(&b,0,103);
    b.tx[0].slots=count+1;
    if(count<4) assert(!transmit(&b,0,hello,104));
}
static void saturated_windows(void)
{
    struct bench b={.rx=match_factory(PC_RX)};
    b.rx.slots=4;
    for(unsigned i=0;i<4;i++) {
        b.tx[i]=match_factory(HITBOX);
        b.tx[i].slots=i+1;
        join(&b,i,0);
    }
    /* Lose P2 sequence 3, deliver later numbers out of order, then replay.
       Hold all four USB endpoints busy until all 32 RF input snapshots arrive. */
    for(unsigned seq=1;seq<=GP_WINDOW;seq++) {
        for(unsigned i=0;i<4;i++) {
            if(i==1 && seq==3) continue;
            struct gp_frame f={.type=GP_INPUT,.session=b.session[i],
                .data.input={.seq=seq,.buttons=(uint16_t)(16*i+seq)}};
            assert(transmit(&b,i,f,seq));
            assert(b.ack[i].data.status.usb_done==0);
        }
    }
    assert(b.receivers[1].accepted==2);
    for(unsigned seq=3;seq<=GP_WINDOW;seq++) {
        struct gp_frame replay={.type=GP_INPUT,.session=b.session[1],
            .data.input={.seq=seq,.buttons=(uint16_t)(16+seq)}};
        assert(transmit(&b,1,replay,10));
    }
    for(unsigned i=0;i<4;i++) assert(b.receivers[i].count==GP_WINDOW);
    struct delivery stale;
    assert(receiver_peek(&b.receivers[0],&stale));
    for(unsigned i=1;i<4;i++) {
        for(unsigned seq=1;seq<=GP_WINDOW;seq++) {
            struct delivery d;
            assert(receiver_peek(&b.receivers[i],&d));
            assert(d.session==b.session[i] && d.input.seq==seq && d.input.buttons==16*i+seq);
            receiver_complete(&b.receivers[i],&d,12+seq);
        }
        struct gp_frame f={.type=GP_POLL,.session=b.session[i]};
        assert(transmit(&b,i,f,30) && b.ack[i].data.status.usb_done==GP_WINDOW);
    }
    /* P1 USB stalls for 50 ms. Other sessions continue to receive heartbeats. */
    for(unsigned i=1;i<4;i++) {
        struct gp_frame f={.type=GP_HEARTBEAT,.session=b.session[i],.data.heartbeat.number=1};
        assert(transmit(&b,i,f,40));
    }
    for(unsigned i=0;i<4;i++) receiver_tick(&b.receivers[i],51);
    assert(!b.receivers[0].session);
    for(unsigned i=1;i<4;i++) assert(b.receivers[i].session==b.session[i]);
    join(&b,0,52);
    receiver_complete(&b.receivers[0],&stale,53);
    assert(b.receivers[0].done==0);
}

int main(void)
{
    saturated_windows();
    concurrent(2);concurrent(4);
    struct match m=match_factory(HITBOX);
    const uint8_t legacy[]={0xb7,0x46,0x32,0xda};
    assert(!memcmp(m.base,legacy,4) && m.channel==40 && match_prefix[1]==0xc5);
    for(unsigned n=1;n<=4;n++) {
        struct usb_layout l;make_layout(&l,n,3+n,"PC Receiver");
        assert(l.blob[22]==n+1 && l.blob[20]==9+40*n+25 && l.compat_length==16+24*n);
        for(unsigned i=0;i<n;i++) {
            const uint8_t *p=l.blob+27+i*40;
            assert(p[2]==i && p[15]==0x81+i && p[22]==2+i && p[28]==p[15] && p[35]==p[22]);
            assert(l.compat[16+24*i]==i && !memcmp(l.compat+18+24*i,"XUSB10",6));
        }
        assert(l.blob[l.web_offset+2]==n && l.blob[l.web_offset+20]==0x85);
        assert(match_delay(n)==600+(n-1)*250);
    }
    m.channel=1;assert(!match_valid(m,HITBOX));m.channel=81;assert(!match_valid(m,PC_RX));
    m.channel=2;m.slots=2;assert(!match_valid(m,CONSOLE_RX));m.slots=1;assert(match_valid(m,CONSOLE_RX));
    puts("Two/four pipes: order, ACKs, USB isolation at 50 ms, RF retune, legacy address and USB layouts passed");
}
