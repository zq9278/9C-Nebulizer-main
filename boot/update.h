#ifndef NEBULIZER_BOOT_UPDATE_H
#define NEBULIZER_BOOT_UPDATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <nebulizer/boot_layout.h>
#include <protocols/common/frame_codec.h>

enum nb_error { NB_OK, NB_BAD_REQUEST, NB_WRONG_TARGET, NB_RANGE,
    NB_BAD_STATE, NB_SEQUENCE, NB_FLASH_ERROR, NB_CRC_ERROR, NB_VECTOR_ERROR };

/* Only the platform adapter touches physical flash; core is host-testable. */
struct nb_flash {
    bool (*read)(uint32_t address, uint8_t *out, size_t length);
    bool (*erase)(uint32_t address, size_t length);
    bool (*program)(uint32_t address, const uint8_t *data, size_t length);
};
struct nb_update {
    const struct nb_flash *flash;
    uint32_t length, crc, next;
    uint16_t begin_id;
    bool active, valid, run_requested;
};
uint32_t nb_crc32(const uint8_t *data, size_t length);
bool nb_vectors_valid(const uint8_t *data, uint32_t length);
void nb_update_init(struct nb_update *update, const struct nb_flash *flash);
/* Response always echoes the request frame_id. Malformed wire frames are handled upstream. */
void nb_update_handle(struct nb_update *update, const struct frame_codec_frame *request,
                      struct frame_codec_frame *response);
#endif
