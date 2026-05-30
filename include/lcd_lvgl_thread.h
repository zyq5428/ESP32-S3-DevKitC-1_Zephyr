#ifndef LCD_LVGL_THREAD_H
#define LCD_LVGL_THREAD_H

#include <zephyr/types.h>

/**
 * @brief LVGL 显示线程的全局控制变量声明
 *
 * 这些变量由 LCD/LVGL 线程维护，可供其他线程 (如 BLE) 读取或修改，
 * 实现跨线程的显示内容控制。
 */

/* [显示亮度] 背光亮度 0~100，0=关闭, 100=最亮 */
extern volatile int g_lcd_brightness;

/* [显示状态] 当前 LVGL 是否正在运行 */
extern volatile bool g_lvgl_ready;

/**
 * @brief LVGL 线程将由 K_THREAD_DEFINE 自动创建并启动，
 *        无需手动调用任何启动函数。
 *        此处仅声明 API 供外部模块 (如 Shell、BLE) 引用。
 */

#endif /* LCD_LVGL_THREAD_H */
