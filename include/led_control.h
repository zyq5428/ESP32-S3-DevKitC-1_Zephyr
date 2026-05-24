#ifndef LED_CONTROL_H
#define LED_CONTROL_H

/**
 * @brief 定义 LED 的显示模式枚举
 */
typedef enum {
    LED_MODE_OFF = 0,       /* 关闭所有 LED 灯 */
    LED_MODE_BREATHE_RED,   /* 红色呼吸灯 */
    LED_MODE_BREATHE_GREEN, /* 绿色呼吸灯 */
    LED_MODE_BREATHE_BLUE   /* 蓝色呼吸灯 */
} led_mode_t;

/* * 使用 extern 声明全局变量，告诉编译器这个变量在别的地方定义了，
 * 这样两个 .c 文件都能访问同一个变量。
 * volatile 关键字非常重要：它提醒编译器这个变量会被中断或另一个线程修改，
 * 每次读取时必须从内存中真实读取，不要做优化缓存。
 */
extern volatile led_mode_t g_led_mode;

#endif /* LED_CONTROL_H */