#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>

// 启用日志记录
LOG_MODULE_REGISTER(LED_TASK, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/util.h>

#define STRIP_NODE		DT_ALIAS(led_strip)

#if DT_NODE_HAS_PROP(DT_ALIAS(led_strip), chain_length)
#define STRIP_NUM_PIXELS	DT_PROP(DT_ALIAS(led_strip), chain_length)
#else
#error Unable to determine length of LED strip
#endif

#define DELAY_TIME K_MSEC(1000)

#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b) }

static const struct led_rgb colors[] = {
	RGB(0x40, 0x00, 0x00), /* red */
	RGB(0x00, 0x40, 0x00), /* green */
	RGB(0x00, 0x00, 0x40), /* blue */
};

static struct led_rgb pixels[STRIP_NUM_PIXELS];

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

void led_thread_entry(void *p1, void *p2, void *p3)
{
    k_msleep(100);
    // ... 在这里初始化您的 LED 设备 ...
    LOG_INF("LED Thread started");
    
	size_t color = 0;
	int rc;

	if (device_is_ready(strip)) {
		LOG_INF("Found LED strip device %s", strip->name);
	} else {
		LOG_ERR("LED strip device %s is not ready", strip->name);
		return;
	}

	LOG_INF("Displaying pattern on strip");
	while (1) {
		for (size_t cursor = 0; cursor < ARRAY_SIZE(pixels); cursor++) {
			memset(&pixels, 0x00, sizeof(pixels));
			memcpy(&pixels[cursor], &colors[color], sizeof(struct led_rgb));

			rc = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
			if (rc) {
				LOG_ERR("couldn't update strip: %d", rc);
			}

			k_sleep(DELAY_TIME);
		}

		color = (color + 1) % ARRAY_SIZE(colors);
	}
}

// 线程栈和定义
#define LED_STACK_SIZE 1024
#define LED_PRIORITY 15

K_THREAD_DEFINE(led_tid, LED_STACK_SIZE, 
                led_thread_entry, NULL, NULL, NULL,
                LED_PRIORITY, 0, 0);
