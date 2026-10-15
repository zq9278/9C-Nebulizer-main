#ifndef NEBULIZER_BOOT_LAYOUT_H
#define NEBULIZER_BOOT_LAYOUT_H
#include <stdint.h>
#define NB_FLASH_BASE       0x08000000U
#define NB_BOOT_SIZE        0x00004000U
#define NB_APP_BASE         0x08004000U
#define NB_META_BASE        0x0801F800U
#define NB_APP_MAX          (NB_META_BASE - NB_APP_BASE)
#define NB_FLASH_END        0x08020000U
#define NB_PAGE_SIZE        2048U
#define NB_RAM_BASE         0x20000000U
#define NB_RAM_END          0x20008FF0U
#define NB_REQUEST_ADDRESS NB_RAM_END
#define NB_REQUEST_MAGIC   0x55424C4EU
/* Product/layout identifier, not the STM32 device ID. */
#define NB_TARGET_ID       0x39430001U
#define NB_PROTOCOL_VERSION 1U
#define NB_FRAME_REPLY     0x0CU
#define NB_CMD_ENTER       0xF0U
#define NB_CMD_INFO        0xF1U
#define NB_CMD_BEGIN       0xF2U
#define NB_CMD_DATA        0xF3U
#define NB_CMD_END         0xF4U
#define NB_CMD_RUN         0xF5U
#define NB_CHUNK_SIZE      120U
#define NB_META_MAGIC      0x314D424EU
#define NB_META_COMMIT     0xC35A6996U
/* ENTER requires exactly these 8 bytes; this is an intent guard, not authentication. */
#define NB_ENTER_COOKIE    "NBLBOOT1"
void platform_enter_boot(uint16_t frame_id);
#endif
