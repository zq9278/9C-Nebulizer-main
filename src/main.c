#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <src/app/app_init.h>
#include <src/app/app_tasks.h>

LOG_MODULE_REGISTER(main, CONFIG_NEBULIZER_LOG_LEVEL);

/*
 * Zephyr 应用入口。
 *
 * 和传统裸机工程不同，这里的 main() 并不直接承载业务循环，而是只负责：
 * 1. 拉起应用初始化
 * 2. 创建各个业务线程
 * 3. 把 CPU 让给 RTOS 调度器
 *
 * 也就是说，真正的业务执行发生在 AppTask / HostCommTask /
 * MistTask / ControlSupervisorTask 这些线程中。
 */
int main(void)
{
	int ret;
	LOG_INF("9C Nebulizer boot");

	ret = app_init();
	if (ret != 0) {
		LOG_ERR("app init failed: %d", ret);
		return ret;
	}

	ret = app_tasks_start();
	if (ret != 0) {
		LOG_ERR("task start failed: %d", ret);
		return ret;
	}

	/*
	 * 主线程后续不再承担业务逻辑，只是周期 sleep 保持自身存活。
	 * 在 Zephyr 中这是一个很常见的写法：系统进入稳定运行后由其他线程工作，
	 * main 线程不退出，也不抢占 CPU。
	 */
	while (true) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
