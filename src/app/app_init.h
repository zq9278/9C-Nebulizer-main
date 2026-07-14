#ifndef SRC_APP_APP_INIT_H_
#define SRC_APP_APP_INIT_H_

/*
 * 初始化应用所有核心模块，供 main() 在启动阶段调用。
 *
 * 这个函数只做“系统拉起”，不创建业务线程：
 * 1. 初始化全局上下文
 * 2. 检查关键板级设备
 * 3. 恢复掉电保存的配置
 * 4. 初始化传感器、执行器、通信、安全、遥测等服务
 *
 * 当它返回 0 时，说明应用已经具备进入运行态的基本条件，
 * 随后 main() 才会继续调用 app_tasks_start() 创建线程。
 */
int app_init(void);

#endif /* SRC_APP_APP_INIT_H_ */
