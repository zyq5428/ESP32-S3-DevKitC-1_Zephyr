#ifndef LED_CONTROL_H
#define LED_CONTROL_H

// 定义呼吸灯亮度上限(0-255)，移到头文件方便蓝牙和LED线程共同使用
#define LIGHT_MAX 0x40

/**
 * @brief 定义 LED 的显示模式枚举
 */
typedef enum {
    LED_MODE_OFF = 0,       /* 关闭所有 LED 灯 */
    LED_MODE_BREATHE_RED,   /* 红色呼吸灯 */
    LED_MODE_BREATHE_GREEN, /* 绿色呼吸灯 */
    LED_MODE_BREATHE_BLUE   /* 蓝色呼吸灯 */
} led_mode_t;

/* 全局 LED 模式变量（volatile 防止编译器过度优化） */
extern volatile led_mode_t g_led_mode;

/* 【新增】全局 LED 实时亮度变量，由 LED 线程更新，蓝牙线程读取 */
extern volatile int g_led_brightness;

#endif /* LED_CONTROL_H */