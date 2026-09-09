#include "settings_store.h"

#include <errno.h>
#include <string.h>

#include <nebulizer/app_config.h>
#include <platform/board_devices.h>
#include <protocols/common/crc16_modbus.h>
#include <services/storage/settings_defaults.h>
#include <platform/hardware.h>
#include <platform/hardware.h>
#include <platform/runtime.h>
#include <platform/log.h>

#define SETTINGS_BLOB_SIZE 64U
#define SETTINGS_CRC_OFFSET 62U
#define SETTINGS_VERSION 1U

static const uint8_t settings_magic[4] = { 'N', 'E', 'B', '2' };
static StackType_t settings_store_stack[APP_STORAGE_STACK_SIZE / sizeof(StackType_t)];
static StaticTask_t settings_task_storage;
static TaskHandle_t settings_task;
static volatile TickType_t save_deadline;
static volatile bool save_pending;

static struct {
	StaticSemaphore_t lock_storage;
	SemaphoreHandle_t lock;
	treatment_config_t cached;
	pid_params_t pid_cached;
	pid_params_t preheat_pid_cached;
	pid_params_t fan_pid_cached;
	const struct board_device *i2c;
	const struct board_gpio *wp;
	bool eeprom_ready;
	bool initialized;
} store_ctx;

static void settings_put_le16(uint8_t *buf, uint16_t value)
{
	buf[0] = (uint8_t)value;
	buf[1] = (uint8_t)(value >> 8);
}

static uint16_t settings_get_le16(const uint8_t *buf)
{
	return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static void settings_put_le32(uint8_t *buf, int32_t value)
{
	uint32_t raw = (uint32_t)value;

	buf[0] = (uint8_t)raw;
	buf[1] = (uint8_t)(raw >> 8);
	buf[2] = (uint8_t)(raw >> 16);
	buf[3] = (uint8_t)(raw >> 24);
}

static int32_t settings_get_le32(const uint8_t *buf)
{
	return (int32_t)((uint32_t)buf[0] |
			 ((uint32_t)buf[1] << 8) |
			 ((uint32_t)buf[2] << 16) |
			 ((uint32_t)buf[3] << 24));
}

static void settings_encode_pid(uint8_t *buf, const pid_params_t *pid)
{
	settings_put_le32(&buf[0], pid->kp_milli);
	settings_put_le32(&buf[4], pid->ki_milli);
	settings_put_le32(&buf[8], pid->kd_milli);
	settings_put_le32(&buf[12], pid->integral_limit_permille);
}

static void settings_decode_pid(const uint8_t *buf, pid_params_t *pid)
{
	pid->kp_milli = settings_get_le32(&buf[0]);
	pid->ki_milli = settings_get_le32(&buf[4]);
	pid->kd_milli = settings_get_le32(&buf[8]);
	pid->integral_limit_permille = settings_get_le32(&buf[12]);
}

static void settings_encode_locked(uint8_t blob[SETTINGS_BLOB_SIZE])
{
	memset(blob, 0, SETTINGS_BLOB_SIZE);
	memcpy(&blob[0], settings_magic, sizeof(settings_magic));
	blob[4] = SETTINGS_VERSION;
	blob[5] = 56U;
	blob[6] = (uint8_t)store_ctx.cached.mode;
	settings_put_le16(&blob[7], store_ctx.cached.target_temp_deci_c);
	settings_put_le16(&blob[9], store_ctx.cached.duration_sec);
	blob[11] = (uint8_t)store_ctx.cached.air_level;
	blob[12] = (uint8_t)store_ctx.cached.mist_level;
	blob[13] = store_ctx.cached.keep_warm_enabled ? 1U : 0U;
	settings_encode_pid(&blob[14], &store_ctx.pid_cached);
	settings_encode_pid(&blob[30], &store_ctx.preheat_pid_cached);
	settings_encode_pid(&blob[46], &store_ctx.fan_pid_cached);
	settings_put_le16(&blob[SETTINGS_CRC_OFFSET],
			  crc16_modbus_compute(blob, SETTINGS_CRC_OFFSET));
}

static bool settings_decode_locked(const uint8_t blob[SETTINGS_BLOB_SIZE])
{
	treatment_config_t config;
	uint16_t expected_crc;

	if ((memcmp(&blob[0], settings_magic, sizeof(settings_magic)) != 0) ||
	    (blob[4] != SETTINGS_VERSION) || (blob[5] != 56U)) {
		return false;
	}

	expected_crc = crc16_modbus_compute(blob, SETTINGS_CRC_OFFSET);
	if (settings_get_le16(&blob[SETTINGS_CRC_OFFSET]) != expected_crc) {
		return false;
	}

	config.mode = (treatment_mode_t)blob[6];
	config.target_temp_deci_c = settings_get_le16(&blob[7]);
	config.duration_sec = settings_get_le16(&blob[9]);
	config.air_level = (air_level_t)blob[11];
	config.mist_level = (mist_level_t)blob[12];
	config.keep_warm_enabled = blob[13] != 0U;
	if ((config.mode > TREATMENT_MODE_COLD) ||
	    (config.target_temp_deci_c > APP_MAX_TARGET_TEMP_DECI_C) ||
	    (config.duration_sec < APP_MIN_TREATMENT_TIME_SEC) ||
	    (config.duration_sec > APP_MAX_TREATMENT_TIME_SEC) ||
	    (config.air_level > AIR_LEVEL_HIGH) ||
	    (config.mist_level > MIST_LEVEL_UI_HIGH) || (blob[13] > 1U)) {
		return false;
	}

	store_ctx.cached = config;
	settings_decode_pid(&blob[14], &store_ctx.pid_cached);
	settings_decode_pid(&blob[30], &store_ctx.preheat_pid_cached);
	settings_decode_pid(&blob[46], &store_ctx.fan_pid_cached);
	return true;
}

static size_t settings_eeprom_address(uint16_t offset, uint8_t *buf)
{
#if APP_SETTINGS_EEPROM_ADDR_WIDTH_BYTES == 2U
	buf[0] = (uint8_t)(offset >> 8);
	buf[1] = (uint8_t)offset;
	return 2U;
#else
	buf[0] = (uint8_t)offset;
	return 1U;
#endif
}

static int settings_eeprom_read(uint8_t blob[SETTINGS_BLOB_SIZE])
{
	uint8_t address[2];
	size_t address_len = settings_eeprom_address(0U, address);

	return board_i2c_write_read(store_ctx.i2c, APP_SETTINGS_EEPROM_I2C_ADDR,
			      address, address_len, blob, SETTINGS_BLOB_SIZE);
}

static int settings_eeprom_write(const uint8_t blob[SETTINGS_BLOB_SIZE])
{
	uint8_t tx[3];
	int ret = 0;

	ret = board_gpio_set(store_ctx.wp, 0);
	if (ret != 0) {
		return ret;
	}

	for (uint16_t offset = 0U; offset < SETTINGS_BLOB_SIZE; ++offset) {
		size_t address_len = settings_eeprom_address(offset, tx);

		tx[address_len] = blob[offset];
		ret = board_i2c_write(store_ctx.i2c, tx, address_len + 1U,
				APP_SETTINGS_EEPROM_I2C_ADDR);
		if (ret != 0) {
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(APP_SETTINGS_EEPROM_WRITE_CYCLE_MS));
	}

	(void)board_gpio_set(store_ctx.wp, 1);
	return ret;
}

static void settings_store_commit(void)
{
	uint8_t blob[SETTINGS_BLOB_SIZE];
	int ret;

	if (!store_ctx.eeprom_ready) {
		LOG_WRN("settings remain in RAM: EEPROM is unavailable");
		return;
	}

	xSemaphoreTake(store_ctx.lock, portMAX_DELAY);
	settings_encode_locked(blob);
	xSemaphoreGive(store_ctx.lock);

	ret = settings_eeprom_write(blob);
	if (ret != 0) {
		LOG_ERR("EEPROM settings write failed: %d", ret);
		return;
	}

	LOG_INF("settings committed to I2C EEPROM: keep_warm=%u mode=%u",
		blob[13], blob[6]);
}

static int settings_schedule_save(TickType_t delay)
{
    taskENTER_CRITICAL();
    save_deadline = xTaskGetTickCount() + delay;
    save_pending = true;
    taskEXIT_CRITICAL();
    xTaskNotifyGive(settings_task);
    return 0;
}

static void settings_task_entry(void *argument)
{
    (void)argument;
    for (;;) {
        bool commit = false;
        TickType_t wait = portMAX_DELAY;
        taskENTER_CRITICAL();
        if (save_pending) {
            int32_t remaining = (int32_t)(save_deadline - xTaskGetTickCount());
            if (remaining <= 0) { save_pending = false; commit = true; }
            else { wait = (TickType_t)remaining; }
        }
        taskEXIT_CRITICAL();
        if (commit) { settings_store_commit(); continue; }
        ulTaskNotifyTake(pdTRUE, wait);
    }
}

int settings_store_init(void)
{
	const struct board_resources *resources = board_get_resources();
	uint8_t blob[SETTINGS_BLOB_SIZE];
	int ret;

	store_ctx.lock = xSemaphoreCreateMutexStatic(&store_ctx.lock_storage);
	configASSERT(store_ctx.lock != NULL);
	settings_defaults_get(&store_ctx.cached);
	settings_defaults_get_pid(&store_ctx.pid_cached);
	settings_defaults_get_preheat_pid(&store_ctx.preheat_pid_cached);
	settings_defaults_get_fan_pid(&store_ctx.fan_pid_cached);
	store_ctx.i2c = resources->eeprom_i2c;
	store_ctx.wp = &resources->ee_wp;
	store_ctx.eeprom_ready = board_device_ready(store_ctx.i2c) &&
				 board_gpio_ready(store_ctx.wp);
	store_ctx.initialized = true;
	settings_task = xTaskCreateStatic(settings_task_entry, "Settings", ARRAY_SIZE(settings_store_stack), NULL, 1, settings_store_stack, &settings_task_storage);
	configASSERT(settings_task != NULL);

	if (!store_ctx.eeprom_ready) {
		LOG_WRN("I2C EEPROM unavailable, using default RAM settings");
		return 0;
	}

	ret = board_gpio_configure(store_ctx.wp, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		store_ctx.eeprom_ready = false;
		LOG_ERR("EEPROM WP configure failed: %d", ret);
		return 0;
	}

	ret = settings_eeprom_read(blob);
	if (ret != 0) {
		LOG_WRN("EEPROM settings read failed: %d; using defaults", ret);
		return 0;
	}

	xSemaphoreTake(store_ctx.lock, portMAX_DELAY);
	if (!settings_decode_locked(blob)) {
		LOG_WRN("EEPROM settings record invalid; using defaults");
	} else {
		LOG_INF("settings restored from I2C EEPROM: keep_warm=%u mode=%u",
			store_ctx.cached.keep_warm_enabled ? 1U : 0U,
			(uint8_t)store_ctx.cached.mode);
	}
	xSemaphoreGive(store_ctx.lock);
	return 0;
}

int settings_store_load(treatment_config_t *config)
{
	if ((!store_ctx.initialized) || (config == NULL)) {
		return -EINVAL;
	}
	xSemaphoreTake(store_ctx.lock, portMAX_DELAY);
	*config = store_ctx.cached;
	xSemaphoreGive(store_ctx.lock);
	return 0;
}

int settings_store_save(const treatment_config_t *config)
{
	if ((!store_ctx.initialized) || (config == NULL)) {
		return -EINVAL;
	}
	xSemaphoreTake(store_ctx.lock, portMAX_DELAY);
	store_ctx.cached = *config;
	xSemaphoreGive(store_ctx.lock);
	return 0;
}

int settings_store_save_delayed(const treatment_config_t *config)
{
	int ret = settings_store_save(config);
	return (ret != 0) ? ret : settings_schedule_save(pdMS_TO_TICKS(APP_SETTINGS_SAVE_DELAY_MS));
}

#define SETTINGS_PID_ACCESSORS(name, field) \
	int settings_store_load_##name(pid_params_t *pid) \
	{ \
		if ((!store_ctx.initialized) || (pid == NULL)) { \
			return -EINVAL; \
		} \
		xSemaphoreTake(store_ctx.lock, portMAX_DELAY); \
		*pid = store_ctx.field; \
		xSemaphoreGive(store_ctx.lock); \
		return 0; \
	} \
	int settings_store_save_##name(const pid_params_t *pid) \
	{ \
		if ((!store_ctx.initialized) || (pid == NULL)) { \
			return -EINVAL; \
		} \
		xSemaphoreTake(store_ctx.lock, portMAX_DELAY); \
		store_ctx.field = *pid; \
		xSemaphoreGive(store_ctx.lock); \
		return 0; \
	} \
	int settings_store_save_##name##_delayed(const pid_params_t *pid) \
	{ \
		int ret = settings_store_save_##name(pid); \
		return (ret != 0) ? ret : settings_schedule_save(pdMS_TO_TICKS(APP_SETTINGS_SAVE_DELAY_MS)); \
	}

SETTINGS_PID_ACCESSORS(pid, pid_cached)
SETTINGS_PID_ACCESSORS(preheat_pid, preheat_pid_cached)
SETTINGS_PID_ACCESSORS(fan_pid, fan_pid_cached)
