#include "../include/canvasfs.h"
#include "../include/canvasfs_bpage.h"
#include <string.h>

/* =====================================================
 * CanvasFS v1.3
 * VOLH+VOLT 2-tile volume, 256 full slots, reserved protection
 * ===================================================== */

/* ---- primitives ---- */
static inline uint32_t pidx(uint16_t x, uint16_t y) {
    return (uint32_t)y * CANVAS_W + x;
}
static inline void gate_xy(uint16_t g, uint16_t *x0, uint16_t *y0) {
    *x0 = (uint16_t)((g % TILES_X) * TILE);
    *y0 = (uint16_t)((g / TILES_X) * TILE);
}
static inline Cell *tc(CanvasFS *fs, uint16_t g, uint8_t r, uint8_t c) {
    uint16_t x0,y0; gate_xy(g,&x0,&y0);
    return &fs->canvas[pidx((uint16_t)(x0+c),(uint16_t)(y0+r))];
}
static inline bool magic4(CanvasFS *fs, uint16_t g,
                            uint8_t m0,uint8_t m1,uint8_t m2,uint8_t m3) {
    Cell *c=tc(fs,g,0,0);
    return c->B==m0&&c->G==m1&&c->R==m2&&c->pad==m3;
}
static inline void set_magic(CanvasFS *fs,uint16_t g,
                               char m0,char m1,char m2,char m3) {
    Cell *c=tc(fs,g,0,0);
    c->A=0; c->B=(uint8_t)m0; c->G=(uint8_t)m1;
    c->R=(uint8_t)m2; c->pad=(uint8_t)m3;
}
static inline void clear_tile(CanvasFS *fs, uint16_t g) {
    uint16_t x0,y0; gate_xy(g,&x0,&y0);
    for(int r=0;r<TILE;r++) for(int c=0;c<TILE;c++){
        Cell *ce=&fs->canvas[pidx((uint16_t)(x0+c),(uint16_t)(y0+r))];
        ce->A=ce->B=ce->G=ce->R=ce->pad=0;
    }
}

/* ---- magic macros ---- */
#define IS_VOLH(fs,g) magic4(fs,g,'V','O','L','H')
#define IS_VOLT(fs,g) magic4(fs,g,'V','O','L','T')
#define IS_DAT(fs,g)  magic4(fs,g,'D','A','T','1')
#define IS_DIR(fs,g)  magic4(fs,g,'D','I','R','1')
#define IS_META(fs,g) magic4(fs,g,'M','E','T','1')
#define IS_FREE(fs,g) magic4(fs,g,'F','R','E','1')

/* ---- gate check ---- */
static inline FsResult gate_check(CanvasFS *fs, uint16_t g) {
    if (!fs->aset) return FS_OK;
    if (g >= TILE_COUNT) return FS_ERR_OOB;
    return fs->aset->open[g] ? FS_OK : FS_ERR_GATE;
}

/* =====================================================
 * Adapter Chain
 * ===================================================== */
FsBpageChain bpchain_make(uint16_t id0) {
    FsBpageChain c; memset(&c,0,sizeof(c));
    c.ids[0]=id0; c.len=1; return c;
}
FsBpageChain bpchain_push(FsBpageChain c, uint16_t id) {
    if (c.len < FS_CHAIN_MAX) { c.ids[c.len++] = id; }
    return c;
}
void bpchain_encode(const FsBpageChain *c,uint16_t g,uint8_t *buf,size_t n){
    for(int i=0;i<c->len;i++){
        uint8_t k=fs_bpage_default_key(g,c->ids[i]);
        fs_bpage_encode(c->ids[i],k,buf,n);
    }
}
void bpchain_decode(const FsBpageChain *c,uint16_t g,uint8_t *buf,size_t n){
    for(int i=c->len-1;i>=0;i--){
        uint8_t k=fs_bpage_default_key(g,c->ids[i]);
        fs_bpage_decode(c->ids[i],k,buf,n);
    }
}

/* =====================================================
 * VOLH helpers
 *
 * VOLH layout:
 *   (0,0) magic 'VOLH'
 *   (0,1).A = volt_gate_id (u16 low)
 *   (0,2)   B=bpage_lo G=bpage_hi R=chain_len pad=reserved
 *   (0,3..10) chain ids: B=id_lo G=id_hi
 * ===================================================== */
static inline uint16_t volh_volt(CanvasFS *fs, uint16_t volh) {
    return (uint16_t)(tc(fs,volh,0,1)->A & 0xFFFF);
}
static inline uint16_t volh_bpage(CanvasFS *fs, uint16_t volh) {
    Cell *c=tc(fs,volh,0,2);
    return (uint16_t)(c->B|((uint16_t)c->G<<8));
}
static FsBpageChain volh_chain(CanvasFS *fs, uint16_t volh) {
    Cell *hdr=tc(fs,volh,0,2);
    uint8_t len=hdr->R;
    if(!len||len>FS_CHAIN_MAX) return bpchain_make(volh_bpage(fs,volh));
    FsBpageChain ch; memset(&ch,0,sizeof(ch));
    ch.len=len;
    for(int i=0;i<len;i++){
        Cell *cc=tc(fs,volh,0,(uint8_t)(3+i));
        ch.ids[i]=(uint16_t)(cc->B|((uint16_t)cc->G<<8));
    }
    return ch;
}
static void volh_set_bpage(CanvasFS *fs, uint16_t volh, uint16_t bp) {
    Cell *c=tc(fs,volh,0,2);
    c->B=(uint8_t)bp; c->G=(uint8_t)(bp>>8); c->R=0;
}
static void volh_set_chain(CanvasFS *fs, uint16_t volh, const FsBpageChain *c) {
    Cell *hdr=tc(fs,volh,0,2);
    hdr->R=(uint8_t)c->len;
    for(int i=0;i<c->len&&i<FS_CHAIN_MAX;i++){
        Cell *cc=tc(fs,volh,0,(uint8_t)(3+i));
        cc->B=(uint8_t)c->ids[i]; cc->G=(uint8_t)(c->ids[i]>>8);
    }
    if(c->len>0){ hdr->B=(uint8_t)c->ids[0]; hdr->G=(uint8_t)(c->ids[0]>>8); }
}

/* slot lives in VOLT tile */
static inline Cell *slotcell(CanvasFS *fs, FsKey key) {
    uint16_t volt=volh_volt(fs,key.gate_id);
    return tc(fs,volt,(uint8_t)(key.slot>>4),(uint8_t)(key.slot&15));
}
static inline uint16_t slot_bpage(CanvasFS *fs, FsKey key) {
    Cell *sc=slotcell(fs,key);
    return (sc->pad==0xFF) ? volh_bpage(fs,key.gate_id) : (uint16_t)sc->pad;
}
static inline bool slot_chain_active(CanvasFS *fs, FsKey key) {
    return tc(fs,key.gate_id,0,2)->R > 0 && slotcell(fs,key)->pad==0xFF;
}

/* slot.A encoding: [31:16]=meta_gate [15:0]=head_dat */
static inline uint16_t slot_dat_gate(Cell *sc)  { return (uint16_t)(sc->A&0xFFFF); }
static inline uint16_t slot_meta_gate(Cell *sc) { return (uint16_t)((sc->A>>16)&0xFFFF); }
static inline void slot_set_ptrs(Cell *sc, uint16_t dat, uint16_t meta) {
    sc->A=(uint32_t)dat|((uint32_t)meta<<16);
}

/* =====================================================
 * FreeMap
 * Bitmap stored in FRE1 tile.
 * Header cells (row0,col0..1) store magic+meta.
 * Bitmap starts at byte offset 6 (after 2 cells × 3 bytes).
 * ===================================================== */
#define FM_BYTE_OFFSET 6u

static void fm_setbit(CanvasFS *fs, uint16_t gate, bool used) {
    uint32_t bi  = FM_BYTE_OFFSET + gate/8;
    uint32_t ci  = bi/3;
    uint32_t off = bi%3;
    uint8_t  bit = (uint8_t)(1u<<(gate%8));
    uint16_t g   = fs->freemap_gate;
    uint8_t  row = (uint8_t)(ci/TILE), col=(uint8_t)(ci%TILE);
    Cell *c = tc(fs,g,row,col);
    uint8_t *f[3]={&c->B,&c->G,&c->R};
    if(used) *f[off]|=bit; else *f[off]&=(uint8_t)~bit;
}
static bool fm_getbit(CanvasFS *fs, uint16_t gate) {
    uint32_t bi  = FM_BYTE_OFFSET + gate/8;
    uint32_t ci  = bi/3, off=bi%3;
    uint8_t  bit = (uint8_t)(1u<<(gate%8));
    uint16_t g   = fs->freemap_gate;
    Cell *c = tc(fs,g,(uint8_t)(ci/TILE),(uint8_t)(ci%TILE));
    uint8_t *f[3]={&c->B,&c->G,&c->R};
    return (*f[off]&bit)!=0;
}
static inline bool is_reserved(uint16_t gate) {
    return gate>=FS_RESERVED_LO && gate<=FS_RESERVED_HI;
}

/* =====================================================
 * DataTile / MetaTile
 * ===================================================== */
#define DAT_PAYLOAD 224u
#define DAT_END     0xFFFFu

static void dat_init(CanvasFS *fs, uint16_t g, uint16_t next) {
    clear_tile(fs,g); set_magic(fs,g,'D','A','T','1');
    tc(fs,g,0,1)->A=(uint32_t)next;
}
static uint16_t dat_next(CanvasFS *fs, uint16_t g) {
    return (uint16_t)(tc(fs,g,0,1)->A&0xFFFF);
}
static void dat_write(CanvasFS *fs, uint16_t g, const uint8_t *d, size_t n) {
    if(n>DAT_PAYLOAD) n=DAT_PAYLOAD;
    for(size_t i=0;i<DAT_PAYLOAD;i++)
        tc(fs,g,(uint8_t)(2+i/TILE),(uint8_t)(i%TILE))->R=(i<n)?d[i]:0;
}
static size_t dat_read(CanvasFS *fs,uint16_t g,uint8_t *out,size_t cap,size_t want){
    if(want>DAT_PAYLOAD) want=DAT_PAYLOAD;
    size_t n=(want<cap)?want:cap;
    for(size_t i=0;i<n;i++)
        out[i]=tc(fs,g,(uint8_t)(2+i/TILE),(uint8_t)(i%TILE))->R;
    return n;
}
static void meta_init(CanvasFS *fs, uint16_t g, uint32_t real_len) {
    clear_tile(fs,g); set_magic(fs,g,'M','E','T','1');
    tc(fs,g,0,1)->A=real_len;
}
static uint32_t meta_get_len(CanvasFS *fs, uint16_t g) {
    return tc(fs,g,0,1)->A;
}
static void meta_set_len(CanvasFS *fs, uint16_t g, uint32_t v) {
    tc(fs,g,0,1)->A=v;
}

/* =====================================================
 * Public API
 * ===================================================== */

void fs_init(CanvasFS *fs, Cell *canvas, uint32_t sz, ActiveSet *aset) {
    fs->canvas=canvas; fs->canvas_size=sz;
    fs->freemap_gate=0xFFFF; fs->aset=aset;
}

/* ---- FreeMap ---- */
FsResult fs_freemap_init(CanvasFS *fs, uint16_t fm_gate) {
    if(fm_gate>=TILE_COUNT) return FS_ERR_OOB;
    clear_tile(fs,fm_gate);
    set_magic(fs,fm_gate,'F','R','E','1');
    fs->freemap_gate=fm_gate;
    /* pre-mark reserved + freemap tile itself */
    fm_setbit(fs,fm_gate,true);
    for(uint16_t g=FS_RESERVED_LO; g<=FS_RESERVED_HI; g++)
        fm_setbit(fs,g,true);
    return FS_OK;
}
FsResult fs_freemap_alloc(CanvasFS *fs, uint16_t *out) {
    if(fs->freemap_gate==0xFFFF) return FS_ERR_NOMEM;
    for(uint16_t g=0;g<TILE_COUNT;g++){
        if(is_reserved(g)) continue;
        if(!fm_getbit(fs,g)){ fm_setbit(fs,g,true); *out=g; return FS_OK; }
    }
    return FS_ERR_NOMEM;
}
FsResult fs_freemap_free(CanvasFS *fs, uint16_t gate) {
    if(fs->freemap_gate==0xFFFF) return FS_ERR_NOMEM;
    if(is_reserved(gate)) return FS_ERR_OOB;
    fm_setbit(fs,gate,false); return FS_OK;
}

/* internal alloc wrapper — falls back to scan if no FreeMap */
static FsResult alloc_tile(CanvasFS *fs, uint16_t *out) {
    if(fs->freemap_gate!=0xFFFF) return fs_freemap_alloc(fs,out);
    for(uint16_t g=0;g<TILE_COUNT;g++){
        if(is_reserved(g)) continue;
        if(!IS_VOLH(fs,g)&&!IS_VOLT(fs,g)&&!IS_DAT(fs,g)&&
           !IS_DIR(fs,g)&&!IS_META(fs,g)&&!IS_FREE(fs,g)){
            *out=g; return FS_OK;
        }
    }
    return FS_ERR_NOMEM;
}

/* ---- Volume ---- */
FsResult fs_format_volume(CanvasFS *fs, uint16_t volh, uint16_t default_bp) {
    if(volh>=TILE_COUNT) return FS_ERR_OOB;
    if(is_reserved(volh)) return FS_ERR_OOB;
    FsResult r=gate_check(fs,volh); if(r!=FS_OK) return r;

    /* alloc VOLT tile */
    uint16_t volt;
    r=alloc_tile(fs,&volt); if(r!=FS_OK) return r;

    /* init VOLH */
    clear_tile(fs,volh); set_magic(fs,volh,'V','O','L','H');
    tc(fs,volh,0,1)->A=(uint32_t)volt;
    volh_set_bpage(fs,volh,default_bp);

    /* init VOLT: 256 slots, no reserved rows */
    clear_tile(fs,volt); set_magic(fs,volt,'V','O','L','T');
    for(int row=0;row<TILE;row++) for(int col=0;col<TILE;col++){
        Cell *sc=tc(fs,volt,(uint8_t)row,(uint8_t)col);
        sc->A=0; sc->B=FS_SLOT_FREE; sc->G=0; sc->R=0; sc->pad=0xFF;
    }
    if(fs->freemap_gate!=0xFFFF){ fm_setbit(fs,volh,true); fm_setbit(fs,volt,true); }
    return FS_OK;
}

FsResult fs_alloc_slot(CanvasFS *fs, uint16_t volh, uint8_t *out) {
    if(!out||volh>=TILE_COUNT) return FS_ERR_OOB;
    FsResult r=gate_check(fs,volh); if(r!=FS_OK) return r;
    if(!IS_VOLH(fs,volh)) return FS_ERR_NOTVOL;
    uint16_t volt=volh_volt(fs,volh);
    r=gate_check(fs,volt); if(r!=FS_OK) return r;
    for(uint16_t s=0;s<256;s++){
        Cell *sc=tc(fs,volt,(uint8_t)(s>>4),(uint8_t)(s&15));
        if(sc->B==FS_SLOT_FREE){
            sc->B=FS_SLOT_TINY; sc->G=0; sc->R=0; sc->pad=0xFF;
            *out=(uint8_t)s; return FS_OK;
        }
    }
    return FS_ERR_NOSLOT;
}

FsResult fs_free_slot(CanvasFS *fs, FsKey key) {
    if(key.gate_id>=TILE_COUNT) return FS_ERR_OOB;
    FsResult r=gate_check(fs,key.gate_id); if(r!=FS_OK) return r;
    Cell *sc=slotcell(fs,key);
    sc->A=0; sc->B=FS_SLOT_FREE; sc->G=0; sc->R=0; sc->pad=0xFF;
    return FS_OK;
}

/* ---- bpage ---- */
FsResult fs_set_bpage(CanvasFS *fs, uint16_t volh, uint16_t bp) {
    if(volh>=TILE_COUNT) return FS_ERR_OOB;
    if(!IS_VOLH(fs,volh)) return FS_ERR_NOTVOL;
    volh_set_bpage(fs,volh,bp); return FS_OK;
}
FsResult fs_get_bpage(CanvasFS *fs, uint16_t volh, uint16_t *out) {
    if(!out||volh>=TILE_COUNT) return FS_ERR_OOB;
    if(!IS_VOLH(fs,volh)) return FS_ERR_NOTVOL;
    *out=volh_bpage(fs,volh); return FS_OK;
}
FsResult fs_set_bpage_chain(CanvasFS *fs, uint16_t volh, const FsBpageChain *c) {
    if(volh>=TILE_COUNT) return FS_ERR_OOB;
    if(!IS_VOLH(fs,volh)) return FS_ERR_NOTVOL;
    volh_set_chain(fs,volh,c); return FS_OK;
}
FsResult fs_slot_set_bpage(CanvasFS *fs, FsKey key, uint16_t bp) {
    if(key.gate_id>=TILE_COUNT) return FS_ERR_OOB;
    if(!IS_VOLH(fs,key.gate_id)) return FS_ERR_NOTVOL;
    Cell *sc=slotcell(fs,key);
    sc->pad=(bp<=254)?(uint8_t)bp:0xFF; sc->R|=0x08; return FS_OK;
}
FsResult fs_slot_get_bpage(CanvasFS *fs, FsKey key, uint16_t *out) {
    if(!out||key.gate_id>=TILE_COUNT) return FS_ERR_OOB;
    *out=slot_bpage(fs,key); return FS_OK;
}

/* ---- resolve chain for a slot ---- */
static FsBpageChain resolve_chain(CanvasFS *fs, FsKey key) {
    if(slot_chain_active(fs,key)) return volh_chain(fs,key.gate_id);
    return bpchain_make(slot_bpage(fs,key));
}

/* ---- write/read/stat ---- */
FsResult fs_write(CanvasFS *fs, FsKey key, const uint8_t *data, size_t len) {
    if(key.gate_id>=TILE_COUNT) return FS_ERR_OOB;
    FsResult r=gate_check(fs,key.gate_id); if(r!=FS_OK) return r;
    if(!IS_VOLH(fs,key.gate_id)) return FS_ERR_NOTVOL;
    uint16_t volt=volh_volt(fs,key.gate_id);
    r=gate_check(fs,volt); if(r!=FS_OK) return r;

    Cell *sc=slotcell(fs,key);
    if(sc->B==FS_SLOT_FREE) return FS_ERR_NOSLOT;
    FsBpageChain chain=resolve_chain(fs,key);

    /* TINY */
    if(len<=4){
        uint8_t tmp[4]={0,0,0,0};
        for(size_t i=0;i<len;i++) tmp[i]=data[i];
        bpchain_encode(&chain,key.gate_id,tmp,len);
        uint32_t v=0;
        for(size_t i=0;i<len;i++) v|=((uint32_t)tmp[i])<<(8*i);
        sc->A=v; sc->B=FS_SLOT_TINY; sc->G=(uint8_t)len; sc->R&=0xF8;
        return FS_OK;
    }
    /* SMALL */
    if(len<=DAT_PAYLOAD){
        uint16_t dat; r=alloc_tile(fs,&dat); if(r!=FS_OK) return r;
        dat_init(fs,dat,DAT_END);
        uint8_t tmp[DAT_PAYLOAD];
        for(size_t i=0;i<len;i++) tmp[i]=data[i];
        bpchain_encode(&chain,key.gate_id,tmp,len);
        dat_write(fs,dat,tmp,len);
        sc->A=(uint32_t)dat; sc->B=FS_SLOT_SMALL;
        sc->G=(uint8_t)len; sc->R=(sc->R&0xF8)|0x01;
        return FS_OK;
    }
    /* LARGE */
    uint16_t head,meta_g;
    r=alloc_tile(fs,&head);  if(r!=FS_OK) return r;
    r=alloc_tile(fs,&meta_g);if(r!=FS_OK) return r;
    meta_init(fs,meta_g,(uint32_t)len);
    size_t rem=len; const uint8_t *p=data; uint16_t cur=head;
    while(rem>0){
        size_t chunk=(rem>DAT_PAYLOAD)?DAT_PAYLOAD:rem;
        uint16_t nxt=DAT_END;
        if(rem>chunk){ r=alloc_tile(fs,&nxt); if(r!=FS_OK) return r; }
        dat_init(fs,cur,nxt);
        uint8_t tmp[DAT_PAYLOAD];
        for(size_t i=0;i<chunk;i++) tmp[i]=p[i];
        bpchain_encode(&chain,key.gate_id,tmp,chunk);
        dat_write(fs,cur,tmp,chunk);
        p+=chunk; rem-=chunk; cur=nxt;
    }
    sc->B=FS_SLOT_LARGE; sc->G=0xFF; sc->R=(sc->R&0xF8)|0x03;
    slot_set_ptrs(sc,head,meta_g);
    return FS_OK;
}

FsResult fs_read(CanvasFS *fs, FsKey key, uint8_t *out, size_t cap, size_t *olen) {
    if(olen) *olen=0;
    if(key.gate_id>=TILE_COUNT) return FS_ERR_OOB;
    FsResult r=gate_check(fs,key.gate_id); if(r!=FS_OK) return r;
    if(!IS_VOLH(fs,key.gate_id)) return FS_ERR_NOTVOL;
    uint16_t volt=volh_volt(fs,key.gate_id);
    r=gate_check(fs,volt); if(r!=FS_OK) return r;

    Cell *sc=slotcell(fs,key);
    if(sc->B==FS_SLOT_FREE) return FS_ERR_NOSLOT;
    FsBpageChain chain=resolve_chain(fs,key);

    if(sc->B==FS_SLOT_TINY){
        uint8_t sz=(sc->G<4)?sc->G:4;
        size_t n=(sz<cap)?(size_t)sz:cap;
        uint32_t v=sc->A;
        for(size_t i=0;i<n;i++) out[i]=(uint8_t)((v>>(8*i))&0xFF);
        bpchain_decode(&chain,key.gate_id,out,n);
        if(olen){ *olen=n; } return FS_OK;
    }
    uint16_t head=slot_dat_gate(sc);
    if(sc->B==FS_SLOT_SMALL){
        uint8_t sz=sc->G;
        if(!IS_DAT(fs,head)) return FS_ERR_NOMEM;
        size_t n=dat_read(fs,head,out,cap,sz);
        bpchain_decode(&chain,key.gate_id,out,n);
        if(olen){ *olen=n; } return FS_OK;
    }
    if(sc->B==FS_SLOT_LARGE){
        uint16_t meta=slot_meta_gate(sc);
        uint32_t real=IS_META(fs,meta)?meta_get_len(fs,meta):0;
        size_t want=(real>0&&(size_t)real<cap)?(size_t)real:cap;
        size_t total=0; uint16_t cur=head;
        while(cur!=DAT_END&&total<want){
            if(!IS_DAT(fs,cur)) break;
            size_t chunk=DAT_PAYLOAD;
            if(total+chunk>want) chunk=want-total;
            total+=dat_read(fs,cur,out+total,want-total,chunk);
            cur=dat_next(fs,cur);
        }
        bpchain_decode(&chain,key.gate_id,out,total);
        if(olen){ *olen=total; } return FS_OK;
    }
    return FS_ERR_NOSLOT;
}

FsResult fs_stat(CanvasFS *fs, FsKey key, FsSlotClass *cls, uint32_t *real_len) {
    if(key.gate_id>=TILE_COUNT) return FS_ERR_OOB;
    FsResult r=gate_check(fs,key.gate_id); if(r!=FS_OK) return r;
    Cell *sc=slotcell(fs,key);
    if(sc->B==FS_SLOT_FREE) return FS_ERR_NOSLOT;
    if(cls) *cls=(FsSlotClass)sc->B;
    if(real_len){
        if(sc->B==FS_SLOT_LARGE){
            uint16_t meta=slot_meta_gate(sc);
            *real_len=IS_META(fs,meta)?meta_get_len(fs,meta):0xFFFFFFFFu;
        } else *real_len=(uint32_t)sc->G;
    }
    return FS_OK;
}

FsResult fs_meta_set_len(CanvasFS *fs, FsKey key, uint32_t v) {
    Cell *sc=slotcell(fs,key);
    if(sc->B!=FS_SLOT_LARGE) return FS_ERR_NOTMETA;
    uint16_t meta=slot_meta_gate(sc);
    if(!IS_META(fs,meta)) return FS_ERR_NOTMETA;
    meta_set_len(fs,meta,v); return FS_OK;
}
FsResult fs_meta_get_len(CanvasFS *fs, FsKey key, uint32_t *out) {
    Cell *sc=slotcell(fs,key);
    if(sc->B!=FS_SLOT_LARGE) return FS_ERR_NOTMETA;
    uint16_t meta=slot_meta_gate(sc);
    if(!IS_META(fs,meta)) return FS_ERR_NOTMETA;
    if(out) { *out=meta_get_len(fs,meta); } return FS_OK;
}

/* =====================================================
 * DirectoryBlock
 * ===================================================== */
static uint32_t fnv1a(const char *s){
    uint32_t h=0x811c9dc5u;
    for(;*s;s++) h=(h^(uint8_t)*s)*0x01000193u;
    return h;
}
static Cell *dep(CanvasFS *fs,uint16_t g,int ei,int ci){
    size_t base=(size_t)(ei*FS_DIR_ENTRY_CELLS+ci);
    return tc(fs,g,(uint8_t)(2+base/TILE),(uint8_t)(base%TILE));
}
static bool dir_slot_read(CanvasFS *fs,uint16_t g,int i,
                           uint32_t *h_out,FsKey *k_out,char name[5]){
    Cell *c0=dep(fs,g,i,0);
    if(!c0->B) return false;
    uint32_t h=(uint32_t)c0->G|((uint32_t)c0->R<<8);
    Cell *c1=dep(fs,g,i,1); h|=((uint32_t)c1->B<<16)|((uint32_t)c1->G<<24);
    if(h_out)*h_out=h;
    if(k_out){
        k_out->gate_id=(uint16_t)c1->R|((uint16_t)dep(fs,g,i,2)->B<<8);
        k_out->slot=dep(fs,g,i,2)->G;
    }
    if(name){
        name[0]=(char)dep(fs,g,i,2)->R; name[1]=(char)dep(fs,g,i,3)->B;
        name[2]=(char)dep(fs,g,i,3)->G; name[3]=(char)dep(fs,g,i,3)->R;
        name[4]='\0';
    }
    return true;
}
static void dir_slot_write(CanvasFS *fs,uint16_t g,int i,
                            uint32_t h,FsKey key,const char *name){
    Cell *c0=dep(fs,g,i,0); c0->B=1; c0->G=(uint8_t)h; c0->R=(uint8_t)(h>>8);
    Cell *c1=dep(fs,g,i,1); c1->B=(uint8_t)(h>>16); c1->G=(uint8_t)(h>>24);
    c1->R=(uint8_t)key.gate_id;
    Cell *c2=dep(fs,g,i,2); c2->B=(uint8_t)(key.gate_id>>8); c2->G=key.slot;
    c2->R=name[0]?(uint8_t)name[0]:0;
    Cell *c3=dep(fs,g,i,3); c3->B=name[1]?(uint8_t)name[1]:0;
    c3->G=name[2]?(uint8_t)name[2]:0; c3->R=name[3]?(uint8_t)name[3]:0;
    tc(fs,g,0,1)->B++;
}
static void dir_slot_clear(CanvasFS *fs,uint16_t g,int i){
    for(int k=0;k<FS_DIR_ENTRY_CELLS;k++){
        Cell *c=dep(fs,g,i,k); c->A=c->B=c->G=c->R=c->pad=0;
    }
    Cell *hdr=tc(fs,g,0,1); if(hdr->B>0) hdr->B--;
}
static int dir_find(CanvasFS *fs,uint16_t g,const char *name,uint32_t h){
    int s=(int)(h%FS_DIR_MAX_ENTRIES);
    for(int p=0;p<FS_DIR_MAX_ENTRIES;p++){
        int i=(s+p)%FS_DIR_MAX_ENTRIES;
        uint32_t fh; char fn[5];
        if(!dir_slot_read(fs,g,i,&fh,NULL,fn)) continue;
        if(fh==h&&strncmp(fn,name,4)==0) return i;
    }
    return -1;
}
static int dir_find_empty(CanvasFS *fs,uint16_t g,uint32_t h){
    int s=(int)(h%FS_DIR_MAX_ENTRIES);
    for(int p=0;p<FS_DIR_MAX_ENTRIES;p++){
        int i=(s+p)%FS_DIR_MAX_ENTRIES;
        if(!dep(fs,g,i,0)->B) return i;
    }
    return -1;
}

FsResult fs_mkdir(CanvasFS *fs, uint16_t g){
    if(g>=TILE_COUNT) return FS_ERR_OOB;
    FsResult r=gate_check(fs,g); if(r!=FS_OK) return r;
    clear_tile(fs,g); set_magic(fs,g,'D','I','R','1');
    if(fs->freemap_gate!=0xFFFF) fm_setbit(fs,g,true);
    return FS_OK;
}
FsResult fs_dir_create(CanvasFS *fs,uint16_t dir_g,const char *name,
                        uint16_t file_volh,FsKey *out_key){
    FsResult r=gate_check(fs,dir_g); if(r!=FS_OK) return r;
    if(!IS_DIR(fs,dir_g)) return FS_ERR_NOTDIR;
    uint32_t h=fnv1a(name);
    if(dir_find(fs,dir_g,name,h)>=0) return FS_ERR_EXISTS;
    int slot=dir_find_empty(fs,dir_g,h);
    if(slot<0) return FS_ERR_NOMEM;
    r=fs_format_volume(fs,file_volh,FS_BPAGE_IDENTITY);
    if(r!=FS_OK) return r;
    uint8_t fs_slot;
    r=fs_alloc_slot(fs,file_volh,&fs_slot); if(r!=FS_OK) return r;
    FsKey key={.gate_id=file_volh,.slot=fs_slot};
    dir_slot_write(fs,dir_g,slot,h,key,name);
    if(out_key)*out_key=key;
    return FS_OK;
}
FsResult fs_dir_open(CanvasFS *fs,uint16_t dir_g,const char *name,FsKey *out){
    FsResult r=gate_check(fs,dir_g); if(r!=FS_OK) return r;
    if(!IS_DIR(fs,dir_g)) return FS_ERR_NOTDIR;
    uint32_t h=fnv1a(name);
    int slot=dir_find(fs,dir_g,name,h);
    if(slot<0) return FS_ERR_NOTFOUND;
    if(out) dir_slot_read(fs,dir_g,slot,NULL,out,NULL);
    return FS_OK;
}
FsResult fs_dir_unlink(CanvasFS *fs,uint16_t dir_g,const char *name){
    FsResult r=gate_check(fs,dir_g); if(r!=FS_OK) return r;
    if(!IS_DIR(fs,dir_g)) return FS_ERR_NOTDIR;
    uint32_t h=fnv1a(name);
    int slot=dir_find(fs,dir_g,name,h);
    if(slot<0) return FS_ERR_NOTFOUND;
    dir_slot_clear(fs,dir_g,slot); return FS_OK;
}
FsResult fs_dir_ls(CanvasFS *fs,uint16_t dir_g,FsLsCb cb,void *u){
    FsResult r=gate_check(fs,dir_g); if(r!=FS_OK) return r;
    if(!IS_DIR(fs,dir_g)) return FS_ERR_NOTDIR;
    int n=0;
    for(int i=0;i<FS_DIR_MAX_ENTRIES;i++){
        FsKey key; char name[5];
        if(dir_slot_read(fs,dir_g,i,NULL,&key,name)) cb(n++,name,key,u);
    }
    return FS_OK;
}
