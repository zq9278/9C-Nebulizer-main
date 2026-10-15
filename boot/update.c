#include "update.h"
#include <string.h>

static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i=0; i<4; ++i) p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t crc_step(uint32_t crc, const uint8_t *p, size_t n)
{
    while (n--) {
        crc ^= *p++;
        for (unsigned bit=0; bit<8; ++bit) crc=(crc>>1)^((crc&1)?0xEDB88320U:0U);
    }
    return crc;
}
uint32_t nb_crc32(const uint8_t *p, size_t n) { return crc_step(0xFFFFFFFFU,p,n)^0xFFFFFFFFU; }
bool nb_vectors_valid(const uint8_t *p, uint32_t length)
{
    uint32_t sp=get32(p), pc=get32(p+4);
    return length>=192U && sp>NB_RAM_BASE && sp<=NB_RAM_END && !(sp&7U) &&
        (pc&1U) && (pc&~1U)>=NB_APP_BASE && (pc&~1U)<NB_APP_BASE+length;
}
static bool image_crc(struct nb_update *u, uint32_t length, uint32_t *out)
{
    uint8_t buffer[128];
    uint32_t crc=0xFFFFFFFFU;
    for (uint32_t at=0; at<length;) {
        size_t n=length-at; if(n>sizeof(buffer)) n=sizeof(buffer);
        if (!u->flash->read(NB_APP_BASE+at,buffer,n)) return false;
        crc=crc_step(crc,buffer,n); at+=(uint32_t)n;
    }
    *out=crc^0xFFFFFFFFU; return true;
}
static bool validate_record(struct nb_update *u)
{
    uint8_t record[40], vector[8]; uint32_t crc;
    if (!u->flash->read(NB_META_BASE,record,sizeof(record))) return false;
    uint32_t length=get32(record+8);
    if (get32(record)!=NB_META_MAGIC || get32(record+4)!=NB_PROTOCOL_VERSION ||
        length<192 || length>NB_APP_MAX || get32(record+16)!=NB_APP_BASE ||
        get32(record+20)!=NB_TARGET_ID || get32(record+24)!=nb_crc32(record,24) ||
        get32(record+28)!=0 || get32(record+32)!=NB_META_COMMIT ||
        get32(record+36)!=~NB_META_COMMIT) return false;
    if (!u->flash->read(NB_APP_BASE,vector,8) || !nb_vectors_valid(vector,length) ||
        !image_crc(u,length,&crc) || crc!=get32(record+12)) return false;
    u->length=length; u->crc=crc; return true;
}
void nb_update_init(struct nb_update *u,const struct nb_flash *flash)
{ memset(u,0,sizeof(*u)); u->flash=flash; u->valid=validate_record(u); }

static uint8_t begin(struct nb_update *u, uint16_t id, const uint8_t *p, size_t n)
{
    if(n!=16) return NB_BAD_REQUEST;
    if(get32(p)!=NB_TARGET_ID) return NB_WRONG_TARGET;
    uint32_t length=get32(p+8),crc=get32(p+12);
    if(get32(p+4)!=NB_APP_BASE || length<192 || length>NB_APP_MAX) return NB_RANGE;
    /* Lost BEGIN ACK: same transaction never erases a second time. */
    if(u->active && u->begin_id==id && u->length==length && u->crc==crc) return NB_OK;
    u->active=false; u->valid=false; u->run_requested=false; u->next=0;
    /* Invalidate before any application erase. A failed erase must not touch App. */
    if(!u->flash->erase(NB_META_BASE,NB_PAGE_SIZE)) return NB_FLASH_ERROR;
    uint8_t check[40];
    if(!u->flash->read(NB_META_BASE,check,sizeof(check))) return NB_FLASH_ERROR;
    for(unsigned i=0;i<sizeof(check);++i) if(check[i]!=0xFF) return NB_FLASH_ERROR;
    if(!u->flash->erase(NB_APP_BASE,NB_APP_MAX)) return NB_FLASH_ERROR;
    u->length=length; u->crc=crc; u->begin_id=id; u->active=true; return NB_OK;
}
static uint8_t data(struct nb_update *u,const uint8_t *p,size_t n)
{
    if(!u->active) return NB_BAD_STATE;
    if(n<=4 || n>4+NB_CHUNK_SIZE) return NB_BAD_REQUEST;
    uint32_t at=get32(p); size_t count=n-4;
    if(at>u->length || count>u->length-at || (at&7U) ||
        ((count&7U) && at+count!=u->length)) return NB_RANGE;
    uint8_t check[NB_CHUNK_SIZE];
    if(at<u->next) {
        if(at+count>u->next) return NB_SEQUENCE;
        if(!u->flash->read(NB_APP_BASE+at,check,count)) return NB_FLASH_ERROR;
        return memcmp(check,p+4,count)==0 ? NB_OK : NB_SEQUENCE;
    }
    if(at!=u->next) return NB_SEQUENCE;
    /* A partial flash program cannot be retried on G0 (ECC doublewords). */
    if(!u->flash->program(NB_APP_BASE+at,p+4,count) ||
        !u->flash->read(NB_APP_BASE+at,check,count) || memcmp(check,p+4,count)) {
        u->active=false; return NB_FLASH_ERROR;
    }
    u->next+=(uint32_t)count; return NB_OK;
}
static uint8_t finish(struct nb_update *u,const uint8_t *p,size_t n)
{
    if(n!=8) return NB_BAD_REQUEST;
    if(get32(p)!=u->length || get32(p+4)!=u->crc) return NB_BAD_REQUEST;
    if(u->valid) return NB_OK; /* Lost END ACK, including a reboot. */
    if(!u->active || u->next!=u->length) return NB_BAD_STATE;
    uint32_t crc; uint8_t vector[8],record[40]={0};
    if(!image_crc(u,u->length,&crc)) return NB_FLASH_ERROR;
    if(crc!=u->crc) return NB_CRC_ERROR;
    if(!u->flash->read(NB_APP_BASE,vector,8)) return NB_FLASH_ERROR;
    if(!nb_vectors_valid(vector,u->length)) return NB_VECTOR_ERROR;
    put32(record,NB_META_MAGIC); put32(record+4,NB_PROTOCOL_VERSION);
    put32(record+8,u->length); put32(record+12,u->crc);
    put32(record+16,NB_APP_BASE); put32(record+20,NB_TARGET_ID);
    put32(record+24,nb_crc32(record,24));
    put32(record+32,NB_META_COMMIT); put32(record+36,~NB_META_COMMIT);
    u->active=false;
    /* Commit occupies a separate doubleword and is always written last. */
    if(!u->flash->program(NB_META_BASE,record,32) ||
        !u->flash->program(NB_META_BASE+32,record+32,8)) return NB_FLASH_ERROR;
    u->valid=validate_record(u); return u->valid ? NB_OK : NB_FLASH_ERROR;
}
void nb_update_handle(struct nb_update *u,const struct frame_codec_frame *req,
                      struct frame_codec_frame *out)
{
    u->run_requested=false;
    uint8_t cmd=req->payload_len ? req->payload[0] : 0;
    uint8_t error=NB_BAD_REQUEST;
    const uint8_t *p=req->payload+1; size_t n=req->payload_len ? req->payload_len-1 : 0;
    if(req->type==1 && req->payload_len>=1 && req->payload_len<=FRAME_CODEC_MAX_PAYLOAD) {
        switch(cmd) {
        case NB_CMD_INFO: error=n==0?NB_OK:NB_BAD_REQUEST; break;
        case NB_CMD_BEGIN: error=begin(u,req->frame_id,p,n); break;
        case NB_CMD_DATA: error=data(u,p,n); break;
        case NB_CMD_END: error=finish(u,p,n); break;
        case NB_CMD_RUN:
            if(n!=4 || get32(p)!=NB_TARGET_ID) error=NB_BAD_REQUEST;
            else if(!u->valid || u->active) error=NB_BAD_STATE;
            else {
                u->valid=validate_record(u);
                if(!u->valid) error=NB_BAD_STATE;
                else {u->run_requested=true; error=NB_OK;}
            }
            break;
        default: break;
        }
    }
    memset(out,0,sizeof(*out)); out->frame_id=req->frame_id;
    out->type=NB_FRAME_REPLY; out->payload_len=32;
    out->payload[0]=cmd; out->payload[1]=error; out->payload[2]=NB_PROTOCOL_VERSION;
    put32(out->payload+4,NB_TARGET_ID); put32(out->payload+8,NB_APP_BASE);
    put32(out->payload+12,NB_APP_MAX); put32(out->payload+16,u->next);
    put32(out->payload+20,u->valid?1:0); put32(out->payload+24,u->length);
    put32(out->payload+28,u->crc);
}
