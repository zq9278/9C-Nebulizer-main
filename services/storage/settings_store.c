#include "settings_store.h"

#include <errno.h>
#include <string.h>

#include <nebulizer/app_config.h>
#include <services/storage/settings_defaults.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(settings_store, CONFIG_NEBULIZER_LOG_LEVEL);

static struct {
	struct k_mutex lock;
	struct k_work_delayable save_work;
	treatment_config_t cached;
	pid_params_t pid_cached;
	bool initialized;
} store_ctx;

static void settings_store_save_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("settings save committed to RAM cache");
}

int settings_store_init(void)
{
	k_mutex_init(&store_ctx.lock);
	k_work_init_delayable(&store_ctx.save_work, settings_store_save_work_handler);
	settings_defaults_get(&store_ctx.cached);
	settings_defaults_get_pid(&store_ctx.pid_cached);
	store_ctx.initialized = true;
	return 0;
}

int settings_store_load(treatment_config_t *config)
{
	if ((!store_ctx.initialized) || (config == NULL)) {
		return -EINVAL;
	}

	k_mutex_lock(&store_ctx.lock, K_FOREVER);
	*config = store_ctx.cached;
	k_mutex_unlock(&store_ctx.lock);
	return 0;
}

int settings_store_save(const treatment_config_t *config)
{
	if ((!store_ctx.initialized) || (config == NULL)) {
		return -EINVAL;
	}

	k_mutex_lock(&store_ctx.lock, K_FOREVER);
	store_ctx.cached = *config;
	k_mutex_unlock(&store_ctx.lock);

	LOG_INF("settings cached");
	return 0;
}

int settings_store_save_delayed(const treatment_config_t *config)
{
	int ret = settings_store_save(config);

	if (ret != 0) {
		return ret;
	}

	return k_work_schedule(&store_ctx.save_work, K_MSEC(APP_SETTINGS_SAVE_DELAY_MS));
}

int settings_store_load_pid(pid_params_t *pid)
{
	if ((!store_ctx.initialized) || (pid == NULL)) {
		return -EINVAL;
	}

	k_mutex_lock(&store_ctx.lock, K_FOREVER);
	*pid = store_ctx.pid_cached;
	k_mutex_unlock(&store_ctx.lock);
	return 0;
}

int settings_store_save_pid(const pid_params_t *pid)
{
	if ((!store_ctx.initialized) || (pid == NULL)) {
		return -EINVAL;
	}

	k_mutex_lock(&store_ctx.lock, K_FOREVER);
	store_ctx.pid_cached = *pid;
	k_mutex_unlock(&store_ctx.lock);

	LOG_INF("pid settings cached");
	return 0;
}

int settings_store_save_pid_delayed(const pid_params_t *pid)
{
	int ret = settings_store_save_pid(pid);

	if (ret != 0) {
		return ret;
	}

	return k_work_schedule(&store_ctx.save_work, K_MSEC(APP_SETTINGS_SAVE_DELAY_MS));
}
