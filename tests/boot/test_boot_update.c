/* Exercise the production update core with flash/ECC and power-loss simulation. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <boot/update.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
static uint8_t memory[128*1024];
static bool programmed[128*1024/8];
static unsigned calls, fail_at, erases;
static uint32_t unreadable;
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static bool read_flash(uint32_t a,uint8_t *p,size_t n)
{
    assert(a>=NB_APP_BASE && a<=NB_FLASH_END && n<=NB_FLASH_END-a);
    if(unreadable>=a && unreadable<a+n)return false;
    memcpy(p,memory+a-NB_FLASH_BASE,n);return true;
}
static bool erase_flash(uint32_t a,size_t n)
{
    assert(a>=NB_APP_BASE && a%2048==0 && n%2048==0 && n<=NB_FLASH_END-a);
    erases++; calls++;
    if(calls==fail_at) {memset(memory+a-NB_FLASH_BASE,0xFF,n/2);return false;}
    memset(memory+a-NB_FLASH_BASE,0xFF,n);
    if(unreadable>=a && unreadable<a+n)unreadable=0;
    memset(programmed+(a-NB_FLASH_BASE)/8,0,n/8*sizeof(bool));return true;
}
static bool program_flash(uint32_t a,const uint8_t *p,size_t n)
{
    assert(a>=NB_APP_BASE && !(a&7) && ((n+7)&~7U)<=NB_FLASH_END-a);
    calls++;
    if(calls==fail_at) {
        size_t partial=n>4?4:n;
        memcpy(memory+a-NB_FLASH_BASE,p,partial);programmed[(a-NB_FLASH_BASE)/8]=true;
        return false;
    }
    for(size_t at=0;at<n;at+=8) {
        unsigned word=(a-NB_FLASH_BASE+at)/8;
        assert(!programmed[word]);programmed[word]=true;
        size_t count=n-at;if(count>8)count=8;
        memcpy(memory+word*8,p+at,count);
    }
    return true;
}
static const struct nb_flash flash={read_flash,erase_flash,program_flash};
static struct nb_update update;
static struct frame_codec_frame response;
static uint8_t image[257];
static void reset(void)
{
    memset(memory,0xFF,sizeof(memory));memset(programmed,0,sizeof(programmed));
    calls=fail_at=erases=0;unreadable=0;nb_update_init(&update,&flash);
    for(unsigned i=0;i<sizeof(image);i++) image[i]=(uint8_t)(i*7);
    put32(image,NB_RAM_END);put32(image+4,NB_APP_BASE+193);
}
static uint8_t request(uint16_t id,uint8_t cmd,const uint8_t *p,size_t n)
{
    struct frame_codec_frame req={0};req.frame_id=id;req.type=1;req.payload_len=n+1;
    req.payload[0]=cmd;if(n)memcpy(req.payload+1,p,n);
    nb_update_handle(&update,&req,&response);
    assert(response.frame_id==id && response.type==NB_FRAME_REPLY && response.payload_len==32);
    return response.payload[1];
}
static uint8_t start(void)
{
    uint8_t p[16];put32(p,NB_TARGET_ID);put32(p+4,NB_APP_BASE);
    put32(p+8,sizeof(image));put32(p+12,nb_crc32(image,sizeof(image)));
    return request(1,NB_CMD_BEGIN,p,16);
}
static uint8_t block(uint32_t at,size_t n)
{
    uint8_t p[124];put32(p,at);memcpy(p+4,image+at,n);
    return request(2,NB_CMD_DATA,p,n+4);
}
static uint8_t finish(void)
{
    uint8_t p[8];put32(p,sizeof(image));put32(p+4,nb_crc32(image,sizeof(image)));
    return request(3,NB_CMD_END,p,8);
}
static uint8_t install(void)
{
    uint8_t error=start(); if(error)return error;
    for(size_t at=0;at<sizeof(image);at+=120) {
        size_t n=sizeof(image)-at;if(n>120)n=120;
        error=block(at,n);if(error)return error;
    }
    return finish();
}
static void test_flow_and_retries(void)
{
    reset();assert(nb_crc32((const uint8_t *)"123456789",9)==0xCBF43926);
    assert(!update.valid);assert(start()==NB_OK);assert(erases==2);
    assert(start()==NB_OK && erases==2);assert(finish()==NB_BAD_STATE);
    assert(block(8,120)==NB_SEQUENCE);assert(block(0,120)==NB_OK);
    unsigned before=calls;assert(block(0,120)==NB_OK && before==calls);
    image[10]^=1;assert(block(0,120)==NB_SEQUENCE);image[10]^=1;
    assert(block(120,120)==NB_OK);assert(block(240,17)==NB_OK);
    assert(finish()==NB_OK && update.valid);before=calls;
    assert(finish()==NB_OK && before==calls);
    nb_update_init(&update,&flash);assert(update.valid && !update.active);
    assert(finish()==NB_OK);
    uint8_t target[4];put32(target,NB_TARGET_ID);
    assert(request(4,NB_CMD_RUN,target,4)==NB_OK && update.run_requested);
    memory[NB_APP_BASE-NB_FLASH_BASE+100]^=1;
    assert(request(5,NB_CMD_RUN,target,4)==NB_BAD_STATE && !update.valid && !update.run_requested);
    nb_update_init(&update,&flash);assert(!update.valid);
    assert(request(5,NB_CMD_RUN,target,4)==NB_BAD_STATE);
}
static void test_guards_and_corruption(void)
{
    reset();uint8_t p[16]={0};assert(request(1,NB_CMD_BEGIN,p,16)==NB_WRONG_TARGET);
    put32(p,NB_TARGET_ID);put32(p+4,NB_FLASH_BASE);put32(p+8,sizeof(image));
    assert(request(1,NB_CMD_BEGIN,p,16)==NB_RANGE && !erases);
    put32(p+4,NB_APP_BASE);put32(p+8,NB_APP_MAX+1);
    assert(request(1,NB_CMD_BEGIN,p,16)==NB_RANGE && !erases);
    assert(block(0,120)==NB_BAD_STATE);
    assert(start()==NB_OK);assert(block(0,119)==NB_RANGE);
    assert(block(0,120)==NB_OK);assert(block(120,120)==NB_OK);assert(block(240,17)==NB_OK);
    memory[NB_APP_BASE-NB_FLASH_BASE+100]^=1;assert(finish()==NB_CRC_ERROR);
    nb_update_init(&update,&flash);assert(!update.valid);
    reset();put32(image+4,NB_FLASH_BASE+1);
    assert(install()==NB_VECTOR_ERROR && !update.valid);
}
static void test_power_loss_every_operation(void)
{
    reset();assert(install()==NB_OK);unsigned total=calls;
    for(unsigned failure=1;failure<=total;failure++) {
        reset();fail_at=failure;assert(install()==NB_FLASH_ERROR);
        nb_update_init(&update,&flash);assert(!update.valid);
        /* Partial doublewords cannot be programmed again; new BEGIN erases all. */
        fail_at=0;assert(install()==NB_OK);nb_update_init(&update,&flash);assert(update.valid);
    }
    reset();assert(install()==NB_OK);assert(start()==NB_OK);
    nb_update_init(&update,&flash);assert(!update.valid); /* Power loss after erase. */
    assert(install()==NB_OK);
    memory[NB_META_BASE-NB_FLASH_BASE+24]^=1;
    nb_update_init(&update,&flash);assert(!update.valid);
    reset();assert(install()==NB_OK);unreadable=NB_META_BASE+32;
    nb_update_init(&update,&flash);assert(!update.valid);assert(install()==NB_OK);
    unreadable=NB_APP_BASE+100;
    nb_update_init(&update,&flash);assert(!update.valid);assert(install()==NB_OK);
}
static void emulate(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    reset();bool app_started=false;
    uint8_t prefix[2],bytes[FRAME_CODEC_MAX_FRAME];
    while(fread(prefix,1,2,stdin)==2) {
        size_t n=prefix[0]|(size_t)prefix[1]<<8;
        if(n>sizeof(bytes) || fread(bytes,1,n,stdin)!=n)return;
        struct frame_codec_frame req;size_t count=0;
        if(frame_codec_decode(bytes,n,&req)==0) {
            if(app_started && req.type==1 && req.payload_len==1 && req.payload[0]==0x24) {
                uint8_t ack[4]={0x24,1,0,0};
                assert(frame_codec_encode(req.frame_id,2,ack,4,bytes,sizeof(bytes),&count)==0);
            } else {
                nb_update_handle(&update,&req,&response);
                assert(frame_codec_encode(response.frame_id,response.type,response.payload,response.payload_len,
                    bytes,sizeof(bytes),&count)==0);
                if(update.run_requested)app_started=true;
            }
        }
        prefix[0]=(uint8_t)count;prefix[1]=(uint8_t)(count>>8);
        if(fwrite(prefix,1,2,stdout)!=2 || fwrite(bytes,1,count,stdout)!=count)return;
        fflush(stdout);
    }
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--emulate")) {emulate();return 0;}
    test_flow_and_retries();test_guards_and_corruption();test_power_loss_every_operation();
    puts("test_boot_update: PASS (CRC, bounds, retries, ECC, commit, power-loss recovery)");return 0;
}
