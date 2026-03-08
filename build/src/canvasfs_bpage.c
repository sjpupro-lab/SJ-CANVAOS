#include "canvasfs_bpage.h"

static inline uint8_t rotl1(uint8_t x){ return (uint8_t)((x<<1) | (x>>7)); }
static inline uint8_t rotr1(uint8_t x){ return (uint8_t)((x>>1) | (x<<7)); }

uint8_t fs_bpage_default_key(uint16_t volume_gate_id, uint16_t bpage_id){
    return (uint8_t)(0x5A ^ (uint8_t)(bpage_id & 0xFF) ^ (uint8_t)(volume_gate_id & 0xFF));
}

void fs_bpage_encode(uint16_t bpage_id, uint8_t key, uint8_t* buf, size_t len){
    switch(bpage_id){
        case FS_BPAGE_IDENTITY:
            return;
        case FS_BPAGE_XOR8:
            for(size_t i=0;i<len;i++) buf[i] ^= key;
            return;
        case FS_BPAGE_NIBBLE:
            for(size_t i=0;i<len;i++){
                uint8_t x = buf[i];
                buf[i] = (uint8_t)((x>>4) | (x<<4));
            }
            return;
        case FS_BPAGE_ROTL1:
            for(size_t i=0;i<len;i++) buf[i] = rotl1(buf[i]);
            return;
        default:
            /* unknown bpage: treat as identity */
            return;
    }
}

void fs_bpage_decode(uint16_t bpage_id, uint8_t key, uint8_t* buf, size_t len){
    switch(bpage_id){
        case FS_BPAGE_IDENTITY:
            return;
        case FS_BPAGE_XOR8:
            for(size_t i=0;i<len;i++) buf[i] ^= key; /* self-inverse */
            return;
        case FS_BPAGE_NIBBLE:
            for(size_t i=0;i<len;i++){
                uint8_t x = buf[i];
                buf[i] = (uint8_t)((x>>4) | (x<<4)); /* self-inverse */
            }
            return;
        case FS_BPAGE_ROTL1:
            for(size_t i=0;i<len;i++) buf[i] = rotr1(buf[i]);
            return;
        default:
            return;
    }
}
