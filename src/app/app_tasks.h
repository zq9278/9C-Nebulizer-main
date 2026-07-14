#ifndef SRC_APP_APP_TASKS_H_
#define SRC_APP_APP_TASKS_H_

/*
 * 创建应用运行期所需线程。初始化完成后由 main() 调用。
 *
 * 当前会创建的线程主要包括：
 * 1. AppTask：统一处理事件和状态迁移
 * 2. HostCommTask：负责上位机收发调度
 * 3. MistCommTask：负责雾化板通信处理
 * 4. ControlSupervisorTask：负责周期采样、控制、安全和遥测
 *
 * 这样 main() 保持很薄，只负责启动，不直接承载业务循环。
 */
int app_tasks_start(void);

#endif /* SRC_APP_APP_TASKS_H_ */
