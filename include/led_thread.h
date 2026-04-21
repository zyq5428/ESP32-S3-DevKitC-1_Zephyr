#ifndef LED_THREAD_H
#define LED_THREAD_H

#include <zephyr/types.h>

/**
 * @brief 启动 LED 控制线程。
 */
void start_led_thread(void);

// 可以在这里声明其他供外部调用的 API
//例如：void set_led_state(bool on);

#endif // LED_THREAD_H