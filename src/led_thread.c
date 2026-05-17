#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>

// 注册日志模块，名称为 LED_TASK，日志级别设置为 INF (信息)
// 这样在控制台可以看到系统的运行状态和错误信息
LOG_MODULE_REGISTER(LED_TASK, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/util.h>

// 从设备树 (Device Tree) 中获取别名为 "led_strip" 的节点标识符
#define STRIP_NODE    DT_ALIAS(led_strip)

// 检查设备树节点中是否定义了灯珠数量属性 (chain_length)
#if DT_NODE_HAS_PROP(DT_ALIAS(led_strip), chain_length)
// 获取灯带的像素点总数
#define STRIP_NUM_PIXELS    DT_PROP(DT_ALIAS(led_strip), chain_length)
#else
// 如果没定义长度，编译时会报错
#error Unable to determine length of LED strip
#endif

// 定义呼吸灯亮度改变的间隔(ms)
#define DELAY_TIME K_MSEC(50)
// 定义呼吸灯亮度上线(0-255)
#define LIGHT_MAX 0x40

// 定义一个宏，方便快速创建 led_rgb 结构体实例（包含红、绿、蓝三个分量）
#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b) }

// 定义一个预设颜色数组：红、绿、蓝
// 注意：0x40 只是中等亮度，最大值为 0xFF
// static const struct led_rgb colors[] = {
//     RGB(0x40, 0x00, 0x00), /* 红色 */
//     RGB(0x00, 0x40, 0x00), /* 绿色 */
//     RGB(0x00, 0x00, 0x40), /* 蓝色 */
// };

// 定义红、绿、蓝三种颜色
static const struct led_rgb red   = { .r = 0x40, .g = 0x00, .b = 0x00 };
static const struct led_rgb green = { .r = 0x00, .g = 0x40, .b = 0x00 };
static const struct led_rgb blue  = { .r = 0x00, .g = 0x00, .b = 0x40 };

// 定义一个数组作为显示缓冲区，对应灯带上的每一颗灯珠
static struct led_rgb pixels[STRIP_NUM_PIXELS];

// 获取 LED 灯带设备的控制句柄
static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

/**
 * @brief 流水灯封装函数
 * 
 * @param color_to_run 想要跑的颜色结构体（包含R,G,B数值）
 * @param delay_ms 每一颗灯移动的间隔时间
 */
void run_marquee(struct led_rgb color_to_run, uint32_t delay_ms)
{
    for (size_t cursor = 0; cursor < STRIP_NUM_PIXELS; cursor++) {
        // 1. 先把所有灯熄灭
        memset(pixels, 0x00, sizeof(pixels));
        
        // 2. 将传入的颜色赋值给当前索引的灯珠
        pixels[cursor] = color_to_run; 
        
        // 3. 更新硬件显示
        led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
        
        // 4. 等待
        k_msleep(delay_ms);
    }
}

/**
 * @brief LED 线程入口函数
 * 
 * @param p1, p2, p3 线程参数（由 K_THREAD_DEFINE 传入，此处未使用）
 */
void led_thread_entry(void *p1, void *p2, void *p3)
{
    // 线程启动后先短暂休眠 100 毫秒，确保系统其他组件初始化完成
    k_msleep(100);
    
    LOG_INF("LED Thread started");
    
    int rc;           // 用于接收函数返回状态码

    // 检查硬件设备是否已准备就绪（驱动是否加载成功）
    if (device_is_ready(strip)) {
        LOG_INF("Found LED strip device %s", strip->name);
    } else {
        LOG_ERR("LED strip device %s is not ready", strip->name);
        return; // 如果设备不可用，终止线程
    }

	/* --- 第一部分：流水灯演示 (红 -> 绿 -> 蓝) --- */
    LOG_DBG("Starting startup sequence...");
	// 分别调用封装好的函数，跑一遍红色，再跑一遍绿色，再跑一遍蓝色
	// 1000ms 一颗，速度较快
    run_marquee(red, 1000);
    run_marquee(green, 1000);
    run_marquee(blue, 1000);

	// 清屏，准备进入呼吸
    memset(pixels, 0x00, sizeof(pixels));
    led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);

	/* --- 第二部分：呼吸灯模式 (主循环) --- */
    LOG_DBG("Entering main breathing loop...");
    
    int brightness = 0;
    int step = 1;

    // 线程主循环
    while (1) {
		for (size_t cursor = 0; cursor < ARRAY_SIZE(pixels); cursor++) {
            // 这里以白光呼吸为例 (红绿蓝三色同步变化)
            pixels[cursor].r = 0;
            pixels[cursor].g = brightness;
            pixels[cursor].b = 0;
        }

        rc = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
		if (rc) {
			LOG_ERR("couldn't update strip: %d", rc);
		}

        brightness += step;
        if (brightness <= 0 || brightness >= LIGHT_MAX) {
            step = -step;
        }

        k_sleep(DELAY_TIME);
    }
}

/* 线程配置参数 */
#define LED_STACK_SIZE 1024  // 线程栈大小（单位：字节）
#define LED_PRIORITY 15      // 线程优先级（数字越大优先级越低）

/**
 * @brief 定义并自动启动线程
 * 
 * 参数依次为：线程 ID, 栈大小, 入口函数, 参数1, 参数2, 参数3, 优先级, 选项, 启动延迟
 */
K_THREAD_DEFINE(led_tid, LED_STACK_SIZE, 
                led_thread_entry, NULL, NULL, NULL,
                LED_PRIORITY, 0, 0);