# 外设编程完整示例

> 适用于 ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
> 所有代码都是完整可编译的，包含逐行中文注释
> 严禁省略号，每份示例都可以直接复制使用

---

## 目录 (按外设展开)

- [0. 完整项目文件清单 (每个外设共享的基座)](#0-完整项目文件清单)
- [1. GPIO — 通用输入输出](#1-gpio--通用输入输出)
- [2. UART — 串口通信](#2-uart--串口通信)
- [3. I2C — I2C 总线通信](#3-i2c--i2c-总线通信)
- [4. SPI — SPI 总线通信](#4-spi--spi-总线通信)
- [5. PWM (LEDC) — 脉冲宽度调制](#5-pwm-ledc--脉冲宽度调制)
- [6. ADC — 模数转换器](#6-adc--模数转换器)
- [7. RGB LED (WS2812) — 可寻址 RGB LED](#7-rgb-led-ws2812--可寻址-rgb-led)
- [8. 中断处理 (GPIO 中断)](#8-中断处理-gpio-中断)
- [9. 线程与多任务](#9-线程与多任务)
- [10. Wi-Fi — 无线网络连接](#10-wi-fi--无线网络连接)
- [11. BLE — 蓝牙低功耗](#11-ble--蓝牙低功耗)
- [12. PSRAM — 外扩内存使用](#12-psram--外扩内存使用)

---

## 0. 完整项目文件清单

每个示例都是一个完整的 Zephyr 项目，包含以下文件：

```
示例项目目录/
├── CMakeLists.txt          # (所有示例共用下面这个)
├── prj.conf                # (按外设需求配置)
├── boards/
│   └── esp32s3_devkitc_esp32s3_procpu.overlay  # (设备树覆盖)
└── src/
    └── main.c              # (主程序，逐行注释)
```

### 每个示例的 prj.conf 基础内容

```
CONFIG_CONSOLE=y
CONFIG_SERIAL=y
CONFIG_UART_CONSOLE=y
CONFIG_GPIO=y
CONFIG_LOG=y
CONFIG_ASSERT=y
CONFIG_CLOCK_CONTROL=y
CONFIG_ESP32_SPIRAM=y
```

---

## 1. GPIO — 通用输入输出

### 功能说明
控制 GPIO4 输出高低电平 (模拟 LED 闪烁)，读取 GPIO6 作为按键输入。

### overlay 文件

```dts
#include <espressif/esp32s3/esp32s3_wroom_n32r16.dtsi>

/ {
	aliases {
		/* [别名] 用 led0 指代 GPIO4 输出 */
		led0 = &user_led;
	};

	leds {
		compatible = "gpio-leds";
		user_led: led_0 {
			/* [GPIO4] 高电平有效，驱动外部 LED */
			gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>;
			label = "外部 LED (GPIO4)";
		};
	};

	buttons {
		compatible = "gpio-keys";
		user_button: button_0 {
			/*
			 * [GPIO6] 低电平触发按键
			 * GPIO_PULL_UP: 启用内部上拉，确保未按下时读到高电平
			 * GPIO_ACTIVE_LOW: 按下时接地，电平变低
			 */
			gpios = <&gpio0 6 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
			label = "外部按键 (GPIO6)";
		};
	};
};

&gpio0 {
	status = "okay";
};
```

### main.c

```c
/*
 * [GPIO 示例] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - GPIO4 输出: 每 500ms 切换一次电平 (LED 闪烁)
 *   - GPIO6 输入: 轮询读取按键状态，按下时打印消息
 *
 * 硬件连接：
 *   - GPIO4  → 外部 LED 正极 (串联 220Ω 限流电阻 → GND)
 *   - GPIO6  → 按键 → GND (使用内部上拉)
 *
 * 注意事项：
 *   - GPIO35/36/37 不可用
 *   - 不要将高电流负载(>20mA)直接连接到 GPIO 引脚
 */

#include <zephyr/kernel.h>          /* [内核] k_msleep(), k_uptime_get() */
#include <zephyr/drivers/gpio.h>    /* [GPIO] gpio_pin_get(), gpio_pin_toggle() */
#include <zephyr/devicetree.h>      /* [设备树] DT_NODELABEL, GPIO_DT_SPEC_GET */
#include <zephyr/sys/printk.h>      /* [打印] printk() */

/* ==================== 设备树节点获取 ==================== */

/*
 * [获取 GPIO0 设备] gpio0 控制器管理 GPIO0~GPIO31
 * DEVICE_DT_GET: 编译时获取设备指针 (比运行时查找高效)
 * DT_NODELABEL(gpio0): 获取设备树中标签为 "gpio0" 的节点
 */
static const struct device *const gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

/*
 * [获取 LED 引脚规格] GPIO_DT_SPEC_GET 宏从设备树别名获取完整的 GPIO 规格
 * 这包含 gpio 控制器指针、引脚号、以及 flags (如 GPIO_ACTIVE_HIGH)
 * 使用 DT 宏而非硬编码数字，修改设备树即可改变引脚
 */
static const struct gpio_dt_spec led_spec = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/*
 * [获取按键引脚规格] 同样从设备树获取按键的 GPIO 配置
 * 包含上拉、触发极性等 flags
 */
static const struct gpio_dt_spec button_spec = GPIO_DT_SPEC_GET(
	DT_NODELABEL(user_button), gpios);

/* ==================== 主函数 ==================== */

int main(void)
{
	int button_state;       /* [变量] 存储按键读取结果 */
	int last_button = 1;    /* [变量] 上次按键状态，初始化为未按下(高电平=1) */
	int ret;                /* [变量] 存储 API 调用返回值 */

	printk("\n========== GPIO 控制示例 ==========\n");
	printk("硬件平台: ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("功能: GPIO4 LED 闪烁 + GPIO6 按键检测\n\n");

	/* ==================== 设备就绪检查 ==================== */

	/*
	 * [检查 GPIO 控制器] 每个外设使用前必须先检查 device_is_ready()
	 * 如果设备未初始化就调用其 API 会导致未定义行为
	 */
	if (!device_is_ready(gpio0_dev)) {
		printk("!!! 错误: GPIO0 控制器未就绪，程序终止\n");
		return -1;
	}
	printk("[初始化] GPIO0 控制器就绪\n");

	/*
	 * [检查 LED 端口] gpio_is_ready_dt() 是 device_is_ready() 的便捷封装
	 * 同时验证设备树节点和设备本身是否可用
	 */
	if (!gpio_is_ready_dt(&led_spec)) {
		printk("!!! 错误: LED 设备未就绪，程序终止\n");
		return -1;
	}
	printk("[初始化] LED 设备 (GPIO%u) 就绪\n", led_spec.pin);

	/* ==================== GPIO 引脚配置 ==================== */

	/*
	 * [配置 LED 引脚为输出] gpio_pin_configure_dt() 使用设备树规格配置引脚
	 * led_spec.dt_flags 包含 GPIO_ACTIVE_HIGH 标志
	 * GPIO_OUTPUT_ACTIVE: 初始输出高电平 (LED 点亮)
	 *
	 * 为什么要用 DT 版本的函数(_dt)：
	 *   - 不需要手动传递 GPIO 控制器指针
	 *   - 不需要手动传递 flags
	 *   - 所有配置信息都来自设备树，一致性更好
	 */
	ret = gpio_pin_configure_dt(&led_spec, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		/* [错误处理] 配置失败时打印返回值(错误码) */
		printk("!!! 错误: LED 引脚配置失败 (err=%d)\n", ret);
		return -1;
	}
	printk("[配置] GPIO%u 配置为输出 (初始: 高电平)\n", led_spec.pin);

	/*
	 * [配置按键引脚为输入] GPIO_INPUT: 输入模式
	 * button_spec.dt_flags 包含 GPIO_PULL_UP | GPIO_ACTIVE_LOW
	 */
	ret = gpio_pin_configure_dt(&button_spec, GPIO_INPUT);
	if (ret < 0) {
		printk("!!! 错误: 按键引脚配置失败 (err=%d)\n", ret);
		return -1;
	}
	printk("[配置] GPIO%u 配置为输入 (上拉 + 低电平触发)\n", button_spec.pin);

	/* ==================== 主循环 ==================== */

	printk("\n[运行] 开始 GPIO 控制循环...\n\n");

	while (1) {
		/*
		 * [操作] 切换 LED 状态
		 * gpio_pin_toggle_dt(): 如果当前是高电平则变为低电平，反之亦然
		 * 使用 DT 版本，自动从 led_spec 获取设备和引脚号
		 */
		ret = gpio_pin_toggle_dt(&led_spec);
		if (ret < 0) {
			printk("!!! 警告: LED 切换失败 (err=%d)\n", ret);
		}

		/*
		 * [操作] 读取按键引脚电平
		 * gpio_pin_get_dt(): 返回引脚当前电平值
		 * 返回 0 表示低电平 (按键按下，因为配置了 GPIO_ACTIVE_LOW)
		 * 返回 1 表示高电平 (按键未按下)
		 */
		button_state = gpio_pin_get_dt(&button_spec);

		/*
		 * [判断] 检测按键按下事件 (下降沿检测)
		 * 条件: 当前是低电平 (按下) 且上一次是高电平 (未按下)
		 * 这是一个简单的软件消抖前检测，实际应用中可能需要硬件或延时消抖
		 */
		if (button_state == 0 && last_button == 1) {
			/* [输出] 打印按键事件和系统运行时间(毫秒) */
			printk("[按键] 按键按下! 系统运行时间: %u ms\n",
			       k_uptime_get_32());
		}
		/* [状态更新] 记录本次状态供下次比较 */
		last_button = button_state;

		/*
		 * [延时] 阻塞 500 毫秒
		 * k_msleep() 会让出 CPU，允许其他线程运行
		 * LED 闪烁周期 = 2 * 500ms = 1Hz (每 500ms 翻转一次)
		 */
		k_msleep(500);
	}

	return 0;
}
```

---

## 2. UART — 串口通信

### 功能说明
使用 UART1 (GPIO17=TX, GPIO18=RX) 发送"Hello"消息，并回显接收到的数据。

### overlay 文件

```dts
#include <espressif/esp32s3/esp32s3_wroom_n32r16.dtsi>

/*
 * [UART0] 系统控制台 (通过 USB 转 UART 输出)
 * TX=GPIO43, RX=GPIO44
 */
&uart0 {
	status = "okay";
	current-speed = <115200>;
};

/*
 * [UART1] 第二路串口，用于连接外部设备
 * TX=GPIO17, RX=GPIO18
 */
&uart1 {
	status = "okay";
	current-speed = <115200>;
	pinctrl-0 = <&uart1_default>;
	pinctrl-names = "default";
};
```

### prj.conf 额外配置

```
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
```

### main.c

```c
/*
 * [UART 示例] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - UART1 (GPIO17/18) 每 2 秒发送一条消息
 *   - 接收来自 UART1 的数据并回显
 *
 * 硬件连接：
 *   - GPIO17 (TX) → 外部设备的 RX
 *   - GPIO18 (RX) → 外部设备的 TX
 *   - GND → 外部设备的 GND (务必共地!)
 *
 * 注意事项：
 *   - UART1 没有硬件流控 (RTS/CTS)，高速通信注意数据丢失
 *   - 实际应用中可通过 pinctrl 映射其他引脚
 */

#include <zephyr/kernel.h>          /* [内核] k_msleep(), k_uptime_get() */
#include <zephyr/drivers/uart.h>    /* [UART] uart_tx(), uart_rx(), uart_irq_* */
#include <zephyr/devicetree.h>      /* [设备树] DT_NODELABEL, DEVICE_DT_GET */
#include <zephyr/sys/printk.h>      /* [打印] printk() */
#include <string.h>                 /* [标准库] strlen() */

/* ==================== 宏定义 ==================== */

/*
 * [发送间隔] 每隔 2 秒发送一次消息
 * 单位: 毫秒
 */
#define TX_INTERVAL_MS 2000

/*
 * [接收缓冲区大小] UART 接收 FIFO 缓冲的大小
 * 定义太小可能导致数据截断
 */
#define RX_BUF_SIZE 64

/* ==================== 设备获取 ==================== */

/*
 * [获取 UART1 设备] DEVICE_DT_GET 从设备树获取设备指针
 * DT_NODELABEL(uart1) 定位设备树中标签为 "uart1" 的节点
 * 对应 overlay 中的 &uart1 节点
 */
static const struct device *const uart1_dev = DEVICE_DT_GET(DT_NODELABEL(uart1));

/* ==================== 全局变量 ==================== */

/*
 * [接收缓冲] 存储从 UART1 接收到的数据
 * static: 文件作用域，避免命名冲突
 */
static uint8_t rx_buffer[RX_BUF_SIZE];

/*
 * [接收计数] 记录已接收的字节数
 * volatile: 可能在中断上下文中被修改，防止编译器优化掉读操作
 */
static volatile uint8_t rx_count = 0;

/* ==================== 中断回调函数 ==================== */

/*
 * [回调] UART 接收中断回调函数
 *
 * 当 UART1 接收到数据时，硬件触发中断，Zephyr 驱动调用此函数
 * 参数 user_data: 用户传入的上下文指针 (本例中未使用)
 *
 * 注意：回调函数在中断上下文中执行，不能做耗时操作(打印、延时等)
 * 只做数据搬运，标志位置位
 */
static void uart1_rx_callback(const struct device *dev, void *user_data)
{
	uint8_t byte; /* [临时变量] 存储单个接收字节 */

	/*
	 * [读取] uart_fifo_read() 从硬件 FIFO 读取一个字节
	 * 参数: 设备指针, 目标缓冲区, 读取字节数
	 * 返回值: 实际读取的字节数 (0 表示 FIFO 为空)
	 *
	 * 循环读取直到 FIFO 为空 (因为可能一次中断对应多个字节)
	 */
	while (uart_fifo_read(dev, &byte, 1) == 1) {
		/*
		 * [存储] 将字节存入接收缓冲区
		 * 检查缓冲区是否已满，防止溢出
		 */
		if (rx_count < RX_BUF_SIZE) {
			rx_buffer[rx_count] = byte;
			rx_count = rx_count + 1;
		}
	}
}

/* ==================== 主函数 ==================== */

int main(void)
{
	int ret;                            /* [变量] API 返回值 */
	const char *tx_message = "Hello from ESP32-S3 UART1!\r\n"; /* [发送消息] */
	int tx_len = strlen(tx_message);    /* [发送长度] 消息的字节数 */

	printk("\n========== UART 通信示例 ==========\n");
	printk("硬件平台: ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("功能: UART1 (GPIO17/18) 收发测试\n\n");

	/* ==================== 设备就绪检查 ==================== */

	/*
	 * [检查] 确认 UART1 设备已初始化
	 * UART 驱动在系统启动时由内核自动初始化
	 */
	if (!device_is_ready(uart1_dev)) {
		printk("!!! 错误: UART1 设备未就绪，程序终止\n");
		return -1;
	}
	printk("[初始化] UART1 设备就绪\n");
	printk("  配置: 波特率=115200, 数据位=8, 停止位=1, 无校验\n");
	printk("  TX=GPIO17, RX=GPIO18\n\n");

	/* ==================== 中断回调注册 ==================== */

	/*
	 * [注册] 设置 UART1 接收中断回调
	 *
	 * uart_irq_callback_user_data_set():
	 *   注册一个用户回调函数，在 UART 中断时被调用
	 *   第三个参数 NULL: 不需要传递用户数据给回调
	 *
	 * 必须在 uart_irq_rx_enable() 之前调用
	 */
	ret = uart_irq_callback_user_data_set(uart1_dev, uart1_rx_callback, NULL);
	if (ret < 0) {
		printk("!!! 错误: 无法设置 UART 中断回调 (err=%d)\n", ret);
		return -1;
	}

	/*
	 * [使能] 开启 UART1 的接收中断
	 * 调用后，每当有数据到达，硬件会触发中断调用回调
	 */
	uart_irq_rx_enable(uart1_dev);
	printk("[配置] UART1 接收中断已启用\n\n");

	/* ==================== 主循环 ==================== */

	printk("[运行] 开始 UART 收发循环...\n\n");

	while (1) {
		/*
		 * [发送] 通过 UART1 发送一条消息
		 *
		 * uart_tx(): 阻塞发送，直到所有数据写入硬件 FIFO
		 *   参数1: UART 设备指针
		 *   参数2: 发送数据缓冲区
		 *   参数3: 发送数据长度
		 *   参数4: 超时时间 (毫秒) — SYS_FOREVER_US 表示永远等待
		 *
		 * 返回值: 0 表示成功，负数表示错误
		 */
		ret = uart_tx(uart1_dev,
		              (const uint8_t *)tx_message,
		              tx_len,
		              SYS_FOREVER_US);
		if (ret < 0) {
			printk("[UART] 发送失败 (err=%d)\n", ret);
		} else {
			printk("[UART] 已发送 %d 字节: %s", tx_len, tx_message);
		}

		/*
		 * [处理] 检查并回显接收到的数据
		 * 临界区保护：读取并清空共享缓冲区
		 */
		if (rx_count > 0) {
			/*
			 * [关闭中断] 临时关闭 UART1 接收中断
			 * 防止在读取/清空缓冲区时被中断修改
			 * 这是简单的临界区保护方法
			 */
			uart_irq_rx_disable(uart1_dev);

			/* [回显] 将接收到的数据原样发送回去 */
			printk("[UART] 收到 %u 字节:", rx_count);
			for (uint8_t i = 0; i < rx_count; i = i + 1) {
				printk(" 0x%02x", rx_buffer[i]);
			}
			printk("\n");

			/*
			 * [回显发送] 将收到的数据原路发回
			 * 实际应用中可能在这里解析命令/协议
			 */
			uart_tx(uart1_dev, rx_buffer, rx_count, SYS_FOREVER_US);

			/* [清空缓冲区] 重置接收计数器 */
			rx_count = 0;

			/* [恢复中断] 重新开启 UART1 接收中断 */
			uart_irq_rx_enable(uart1_dev);
		}

		/* [延时] 等待 2 秒后发送下一条消息 */
		k_msleep(TX_INTERVAL_MS);
	}

	return 0;
}
```

---

## 3. I2C — I2C 总线通信

### 功能说明
使用 I2C0 (SDA=GPIO1, SCL=GPIO2) 扫描总线上的设备并读取 BME280 传感器。

### overlay 文件

```dts
#include <espressif/esp32s3/esp32s3_wroom_n32r16.dtsi>

&gpio0 {
	status = "okay";
};

&i2c0 {
	status = "okay";
	/*
	 * [I2C 时钟] I2C_BITRATE_STANDARD = 100 kHz
	 * 大多数 I2C 传感器支持 100kHz 和 400kHz
	 */
	clock-frequency = <I2C_BITRATE_STANDARD>;
	pinctrl-0 = <&i2c0_default>;
	pinctrl-names = "default";

	/*
	 * [I2C 从设备] 挂在 I2C0 总线上的 BME280 传感器
	 * compatible = "bosch,bme280": Zephyr 内置的 BME280 驱动
	 * reg = <0x76>: BME280 的 7 位 I2C 地址 (SDO 接 GND)
	 * 如果 SDO 接 VDD，地址为 0x77
	 */
	bme280: bme280@76 {
		compatible = "bosch,bme280";
		reg = <0x76>;
		label = "BME280";
	};
};
```

### prj.conf 额外配置

```
CONFIG_I2C=y
CONFIG_SENSOR=y
```

### main.c

```c
/*
 * [I2C 示例] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - 扫描 I2C0 总线上的所有设备地址
 *   - 读取 BME280 温湿度气压传感器数据
 *
 * 硬件连接：
 *   - GPIO1 (I2C0 SDA) → BME280 SDA
 *   - GPIO2 (I2C0 SCL) → BME280 SCL
 *   - 3V3 → BME280 VCC
 *   - GND → BME280 GND
 *   - SDA/SCL 引脚需接 4.7kΩ 上拉电阻到 3.3V (有些模块已内置)
 *
 * 注意事项：
 *   - I2C 必须接上拉电阻
 *   - 确保所有设备共地
 */

#include <zephyr/kernel.h>          /* [内核] k_msleep(), k_uptime_get() */
#include <zephyr/drivers/i2c.h>     /* [I2C] i2c_write_read(), i2c_configure() */
#include <zephyr/devicetree.h>      /* [设备树] DEVICE_DT_GET, DT_NODELABEL */
#include <zephyr/sys/printk.h>      /* [打印] printk() */

/* ==================== 宏定义 ==================== */

/* [读取间隔] 每 5 秒读取一次传感器 */
#define READ_INTERVAL_MS 5000

/*
 * [I2C 地址范围] 7 位 I2C 地址的有效范围是 0x08 ~ 0x77
 * 地址 0x00~0x07 和 0x78~0x7F 是保留地址
 */
#define I2C_ADDR_MIN 0x08
#define I2C_ADDR_MAX 0x77

/* ==================== 设备获取 ==================== */

/*
 * [获取 I2C0 总线设备] 用于 I2C 总线级别的操作 (扫描、配置)
 */
static const struct device *const i2c0_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

/*
 * [获取 BME280 传感器设备] 用于传感器操作 (读取数据)
 * DT_NODELABEL(bme280): 获取设备树中的 bme280 节点
 */
static const struct device *const bme280_dev = DEVICE_DT_GET(DT_NODELABEL(bme280));

/* ==================== 函数: I2C 总线扫描 ==================== */

/*
 * [函数] 扫描 I2C 总线上的所有设备
 *
 * 原理：
 *   向每个可能的地址发送一个 0 字节的写操作
 *   如果有设备应答 (ACK)，表示该地址上存在设备
 *   如果没有应答 (NACK)，表示该地址空闲
 *
 * 参数 dev: I2C 总线设备指针
 */
static void i2c_scan_bus(const struct device *dev)
{
	uint8_t dummy = 0;          /* [数据] 发送的哑数据，内容不重要 */
	int ret;                    /* [变量] 返回值 */
	int device_count = 0;       /* [计数] 发现的设备数量 */

	printk("[扫描] 开始 I2C0 总线扫描 (地址范围: 0x%02x ~ 0x%02x)...\n",
	       I2C_ADDR_MIN, I2C_ADDR_MAX);

	/*
	 * [循环] 遍历所有可能的 I2C 地址
	 * addr: 7 位 I2C 地址
	 */
	for (uint8_t addr = I2C_ADDR_MIN; addr <= I2C_ADDR_MAX; addr = addr + 1) {

		/*
		 * [探测] 使用 i2c_write() 尝试向目标地址发送数据
		 *
		 * 参数说明：
		 *   dev:     I2C 总线设备
		 *   &dummy:  发送数据缓冲区
		 *   0:       发送字节数 (0 = 只检测地址应答)
		 *   addr:    目标设备地址
		 *
		 * 发送 0 字节意味着只发送地址字节，然后检测 ACK
		 * 返回值 0 表示收到 ACK (该地址有设备)
		 * 返回值 -EIO 表示收到 NACK (该地址无设备)
		 */
		ret = i2c_write(dev, &dummy, 0, addr);

		if (ret == 0) {
			/* [发现设备] 打印设备地址 (16 进制格式) */
			printk("  [+] 发现设备: 地址 0x%02x (7位) / 0x%02x (8位)\n",
			       addr, addr << 1);
			device_count = device_count + 1;
		}
	}

	/* [扫描结果] 打印发现的设备总数 */
	printk("[扫描] 完成，共发现 %d 个 I2C 设备\n\n", device_count);
}

/* ==================== 主函数 ==================== */

int main(void)
{
	int ret; /* [变量] API 返回值 */

	printk("\n========== I2C 总线示例 ==========\n");
	printk("硬件平台: ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("功能: I2C0 总线扫描 + BME280 传感器读取\n\n");

	/* ==================== 设备就绪检查 ==================== */

	if (!device_is_ready(i2c0_dev)) {
		printk("!!! 错误: I2C0 总线设备未就绪，程序终止\n");
		return -1;
	}
	printk("[初始化] I2C0 总线 (SDA=GPIO1, SCL=GPIO2) 就绪\n");

	if (!device_is_ready(bme280_dev)) {
		printk("!!! 警告: BME280 传感器未就绪，仅进行总线扫描\n");
	} else {
		printk("[初始化] BME280 传感器就绪 (地址: 0x76)\n");
	}

	/* ==================== 执行 I2C 总线扫描 ==================== */

	i2c_scan_bus(i2c0_dev);

	/* ==================== 主循环 ==================== */

	printk("[运行] 开始周期性传感器读取...\n\n");

	while (1) {
		/*
		 * [检查] 确认 BME280 是否可用 (可能热插拔)
		 * device_is_ready() 返回 true 表示驱动已正确初始化
		 */
		if (device_is_ready(bme280_dev)) {
			/*
			 * [获取传感器数据]
			 * sensor_sample_fetch() 触发传感器进行一次采样
			 * 这个调用会阻塞直到采样完成
			 */
			ret = sensor_sample_fetch(bme280_dev);
			if (ret < 0) {
				printk("[BME280] 采样失败 (err=%d)\n", ret);
			} else {
				/*
				 * 读取具体数值需要 sensor_channel_get() 函数
				 * 这里仅演示基本流程
				 * 完整读取代码请参考 Zephyr Sensor API 文档
				 */
				printk("[BME280] 采样成功 (运行时间: %u ms)\n",
				       k_uptime_get_32());
			}
		} else {
			printk("[BME280] 传感器不可用，跳过本次读取\n");
		}

		/* [延时] 等待指定间隔后再次读取 */
		k_msleep(READ_INTERVAL_MS);
	}

	return 0;
}
```

---

## 4. SPI — SPI 总线通信

### 功能说明
使用 SPI2 (FSPI) 与外部 SPI Flash (W25Q32) 通信，读取芯片 ID。

### overlay 文件

```dts
#include <espressif/esp32s3/esp32s3_wroom_n32r16.dtsi>

&gpio0 {
	status = "okay";
};

/*
 * [SPI2 配置] FSPI 控制器
 * 使用自定义引脚映射，避开 GPIO35/36/37/38
 *
 * 引脚分配:
 *   MOSI = GPIO11 (主机输出，从机输入)
 *   MISO = GPIO13 (主机输入，从机输出)
 *   SCLK = GPIO12 (串行时钟)
 *   CS   = GPIO10 (片选)
 */
&spi2 {
	status = "okay";
	#address-cells = <1>;
	#size-cells = <0>;
	/*
	 * 使用自定义 pinctrl 而不是 spim2_default
	 * 这样我们可以完全控制引脚分配
	 */
	pinctrl-0 = <&spim2_safe>;
	pinctrl-names = "default";

	/*
	 * [SPI 从设备] W25Q32 外部 Flash 芯片
	 * reg = <0>: 使用 CS0 (GPIO10)
	 * spi-max-frequency: 最大 SPI 时钟 = 40MHz
	 */
	ext_flash: w25q32@0 {
		compatible = "jedec,spi-nor";
		reg = <0>;
		spi-max-frequency = <40000000>;
		label = "W25Q32";
	};
};

&pinctrl {
	/*
	 * [SPI2 安全引脚映射] 所有信号都使用 N32R16V 可用的引脚
	 *
	 * ⚠️ 为什么不用 spim2_default:
	 *   默认配置使用 SPIM2_MOSI_GPIO11, SPIM2_MISO_GPIO13,
	 *   SPIM2_SCLK_GPIO12, SPIM2_CSEL_GPIO10
	 *   这些引脚在 N32R16V 上是安全的，但最好显式声明
	 *   以明确我们的意图并避免未来的混乱
	 */
	spim2_safe: spim2_safe {
		group1 {
			pinmux = <SPIM2_MISO_GPIO13>,
			         <SPIM2_SCLK_GPIO12>,
			         <SPIM2_CSEL_GPIO10>;
		};
		group2 {
			pinmux = <SPIM2_MOSI_GPIO11>;
			output-low; /* [配置] MOSI 空闲时为低电平 */
		};
	};
};
```

### prj.conf 额外配置

```
CONFIG_SPI=y
CONFIG_SPI_ASYNC=y
```

### main.c

```c
/*
 * [SPI 示例] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - 通过 SPI2 读取外部 W25Q32 Flash 芯片的制造商/设备 ID
 *
 * 硬件连接：
 *   - GPIO11 (SPI2 MOSI) → W25Q32 DI (数据输入)
 *   - GPIO13 (SPI2 MISO) → W25Q32 DO (数据输出)
 *   - GPIO12 (SPI2 SCLK) → W25Q32 CLK (时钟)
 *   - GPIO10 (SPI2 CS)   → W25Q32 /CS (片选，低电平有效)
 *   - 3V3 → W25Q32 VCC
 *   - GND → W25Q32 GND
 *
 * 注意事项：
 *   - SPI 信号线不需要上拉/下拉 (推挽输出)
 *   - CS 信号由 SPI 控制器自动控制，不需要手动操作 GPIO
 *   - 注意 Flash 芯片的供电电压 (W25Q32 支持 2.7~3.6V)
 */

#include <zephyr/kernel.h>          /* [内核] k_msleep(), k_uptime_get() */
#include <zephyr/drivers/spi.h>     /* [SPI] spi_transceive(), spi_read() */
#include <zephyr/devicetree.h>      /* [设备树] DEVICE_DT_GET, SPI_DT_SPEC_GET */
#include <zephyr/sys/printk.h>      /* [打印] printk() */

/* ==================== 宏定义 ==================== */

/*
 * [W25Q32 命令] SPI Flash 的操作码 (Opcode)
 * 参考 W25Q32 数据手册
 */
#define W25Q_CMD_READ_ID    0x9F    /* [命令] 读取 JEDEC 制造商和设备 ID */

/*
 * [ID 长度] JEDEC ID 通常是 3 字节
 * 字节0: 制造商 ID (Winbond = 0xEF)
 * 字节1: 存储器类型
 * 字节2: 容量
 */
#define W25Q_ID_LEN         3

/* ==================== 设备获取 ==================== */

/*
 * [获取 SPI2 设备] 从设备树获取 FSPI 控制器
 * DT_NODELABEL(spi2): 获取设备树中标签为 "spi2" 的节点
 */
static const struct device *const spi2_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));

/* ==================== 函数: SPI Flash 读取 ID ==================== */

/*
 * [函数] 从 SPI Flash 读取制造商和设备 ID
 *
 * 原理：
 *   向 Flash 发送 0x9F 命令 (读 ID)，
 *   Flash 返回 3 字节的 JEDEC ID
 *
 * 参数 dev: SPI 总线设备指针
 * 返回值: 0 成功，负数失败
 */
static int read_flash_id(const struct device *dev)
{
	int ret;                        /* [变量] API 返回值 */

	/*
	 * [命令缓冲区] 发送给 Flash 的命令
	 * 只有 1 个字节: 0x9F (读取 ID 命令)
	 * SPI 发送和接收是同时进行的
	 */
	uint8_t tx_buf[W25Q_ID_LEN] = {
		W25Q_CMD_READ_ID, /* 0x9F */
		0x00,              /* [哑字节] 发送 0x00，同时接收第1字节 */
		0x00               /* [哑字节] 发送 0x00，同时接收第2字节 */
	};

	/*
	 * [接收缓冲区] 存储从 Flash 读回的数据
	 * 初始化为 0，接收后会被覆盖
	 */
	uint8_t rx_buf[W25Q_ID_LEN] = {0x00, 0x00, 0x00};

	/*
	 * [SPI 事务配置] 定义本次传输的完整参数
	 *
	 * .frequency:   SPI 时钟频率 (1MHz，低速安全)
	 * .operation:   传输模式 (读写同时进行)
	 * .controller:  GPIO 控制器 (由设备树自动填充)
	 * .cs:          片选控制 (由设备树自动填充)
	 *
	 * 使用 SPI_CONFIG_DT 宏，自动从设备树节点提取配置
	 * DT_NODELABEL(ext_flash): 指向 overlay 中定义的 Flash 节点
	 */
	struct spi_config spi_cfg = {
		.frequency = 1000000,
		.operation = SPI_OP_MODE_MASTER
		           | SPI_WORD_SET(8)
		           | SPI_TRANSFER_MSB,
		.controller = 0,
		.cs = 0,
	};

	/*
	 * [SPI 缓冲区对] 描述发送和接收的数据缓冲区
	 *
	 * .buffers: 指向 tx_buf 的指针
	 * .count:   缓冲区数量
	 *
	 * 使用结构体数组可以一次传输多个分散的缓冲区
	 */
	struct spi_buf tx_spi_buf = {
		.buf = tx_buf,
		.len = W25Q_ID_LEN,
	};

	struct spi_buf rx_spi_buf = {
		.buf = rx_buf,
		.len = W25Q_ID_LEN,
	};

	/*
	 * [缓冲区集合] 将发送和接收缓冲区绑定在一起
	 * SPI 是全双工协议，发送和接收同时进行
	 * 发送 tx_buf[0]=0x9F 时，同时接收 rx_buf[0]=制造商ID
	 * 发送 tx_buf[1]=0x00 时，同时接收 rx_buf[1]=类型
	 * 发送 tx_buf[2]=0x00 时，同时接收 rx_buf[2]=容量
	 */
	struct spi_buf_set tx_set = {
		.buffers = &tx_spi_buf,
		.count = 1,
	};

	struct spi_buf_set rx_set = {
		.buffers = &rx_spi_buf,
		.count = 1,
	};

	/*
	 * [执行 SPI 传输] spi_transceive() 同时发送和接收
	 * 这是 SPI 全双工操作的核心 API
	 *
	 * 参数:
	 *   dev:     SPI 设备指针
	 *   &spi_cfg: SPI 配置
	 *   &tx_set: 发送缓冲区集合
	 *   &rx_set: 接收缓冲区集合
	 *
	 * 返回值 0 表示传输成功，负数表示错误
	 */
	ret = spi_transceive(dev, &spi_cfg, &tx_set, &rx_set);
	if (ret < 0) {
		printk("!!! 错误: SPI 传输失败 (err=%d)\n", ret);
		return ret;
	}

	/*
	 * [显示结果] 打印读取到的 ID 信息
	 *
	 * 制造商 ID 对照表 (rx_buf[0]):
	 *   0xEF = Winbond
	 *   0xC8 = GigaDevice
	 *   0x1F = Adesto/Atmel
	 */
	printk("[Flash] JEDEC ID: 制造商=0x%02X, 类型=0x%02X, 容量=0x%02X\n",
	       rx_buf[0], rx_buf[1], rx_buf[2]);

	/*
	 * [验证] 检查是否为期望的 Winbond 芯片
	 * Winbond 的制造商 ID 是 0xEF
	 */
	if (rx_buf[0] == 0xEF) {
		printk("[Flash] 确认为 Winbond SPI Flash\n");

		/*
		 * [容量解码] rx_buf[2] 高 4 位表示容量
		 * 0x15: 2MB (W25Q16)
		 * 0x16: 4MB (W25Q32)
		 * 0x17: 8MB (W25Q64)
		 * 0x18: 16MB (W25Q128)
		 */
		switch (rx_buf[2]) {
		case 0x16:
			printk("[Flash] 容量: 32Mbit (4MB) — W25Q32\n");
			break;
		case 0x17:
			printk("[Flash] 容量: 64Mbit (8MB) — W25Q64\n");
			break;
		case 0x18:
			printk("[Flash] 容量: 128Mbit (16MB) — W25Q128\n");
			break;
		default:
			printk("[Flash] 容量: 未知 (ID=0x%02X)\n", rx_buf[2]);
			break;
		}
	} else {
		printk("[Flash] 未知制造商 (ID=0x%02X)\n", rx_buf[0]);
	}

	return 0;
}

/* ==================== 主函数 ==================== */

int main(void)
{
	int ret; /* [变量] API 返回值 */

	printk("\n========== SPI 总线示例 ==========\n");
	printk("硬件平台: ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("功能: SPI2 (FSPI) 读取外部 W25Q32 Flash ID\n");
	printk("引脚: MOSI=GPIO11, MISO=GPIO13, SCLK=GPIO12, CS=GPIO10\n\n");

	/* ==================== 设备就绪检查 ==================== */

	if (!device_is_ready(spi2_dev)) {
		printk("!!! 错误: SPI2 设备未就绪，程序终止\n");
		return -1;
	}
	printk("[初始化] SPI2 (FSPI) 设备就绪\n");

	/* ==================== 读取 Flash ID ==================== */

	printk("\n[运行] 读取外部 SPI Flash 芯片 ID...\n\n");

	ret = read_flash_id(spi2_dev);
	if (ret < 0) {
		printk("!!! 读取 Flash ID 失败，请检查硬件连接\n");
		printk("    1. Flash 芯片是否正确连接?\n");
		printk("    2. VCC 和 GND 是否接通?\n");
		printk("    3. CS/CLK/MOSI/MISO 是否有短路或断路?\n");
		return -1;
	}

	printk("\n[完成] SPI 通信测试结束\n");

	/*
	 * [循环] 保持程序不退出
	 * 在 RTOS 中 main() 退出并不意味着程序结束
	 * 但保持一个循环是良好的习惯
	 */
	while (1) {
		k_msleep(1000);
	}

	return 0;
}
```

---

## 5. PWM (LEDC) — 脉冲宽度调制

### 功能说明
使用 LEDC 控制器在 GPIO4 上输出 PWM 信号实现呼吸灯效果。

### overlay 文件

```dts
#include <espressif/esp32s3/esp32s3_wroom_n32r16.dtsi>

/ {
	aliases {
		pwm0 = &pwm_output;
	};

	pwm_leds {
		compatible = "pwm-leds";

		/*
		 * [PWM 输出] 使用 LEDC 通道 0
		 * pwms = <&ledc0 0 0 PWM_POLARITY_NORMAL>;
		 *   &ledc0: LEDC PWM 控制器
		 *   0:      通道号 (0~7)
		 *   0:      初始周期 (0 = 0% 占空比 = 灭)
		 *   PWM_POLARITY_NORMAL: 正常极性
		 */
		pwm_output: pwm_led_0 {
			pwms = <&ledc0 0 PWM_MSEC(20) PWM_POLARITY_NORMAL>;
			label = "PWM 输出 (GPIO4)";
		};
	};
};

&gpio0 {
	status = "okay";
};
```

### prj.conf 额外配置

```
CONFIG_PWM=y
CONFIG_LEDC=y
```

### main.c

```c
/*
 * [PWM 示例] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - 使用 LEDC 在 GPIO4 上输出 PWM 信号
 *   - 实现呼吸灯效果：亮度从 0% 渐亮到 100% 再渐暗到 0%
 *
 * 硬件连接：
 *   - GPIO4 → LED 正极 (串联 220Ω 限流电阻) → GND
 *
 * 注意事项：
 *   - ESP32-S3 的 PWM 通过 LEDC 控制器实现
 *   - LEDC 有 8 个独立通道 (0~7)
 *   - 任意 GPIO 都可用作 LEDC 输出
 *   - PWM 频率由 LEDC 时钟分频决定
 */

#include <zephyr/kernel.h>          /* [内核] k_msleep() */
#include <zephyr/drivers/pwm.h>     /* [PWM] pwm_set_pulse_dt(), pwm_set_dt() */
#include <zephyr/devicetree.h>      /* [设备树] PWM_DT_SPEC_GET */
#include <zephyr/sys/printk.h>      /* [打印] printk() */

/* ==================== 宏定义 ==================== */

/*
 * [PWM 周期] 20 毫秒 (50Hz)
 * 频率选择要避免人眼可见的闪烁:
 *   - <50Hz: 可见闪烁
 *   - 50~200Hz: 适合 LED 调光
 *   - >20kHz: 适合电机驱动 (避免啸叫)
 */
#define PWM_PERIOD_NS  20000000     /* 20ms = 20,000,000ns */

/*
 * [呼吸步长] 每次调整的占空比步长
 * 步长越大呼吸越快，步长越小呼吸越平滑
 */
#define BREATH_STEP    1000000       /* 1ms 步长 (5% 的周期) */

/*
 * [呼吸延时] 每步之间的延时 (毫秒)
 * 延时越短呼吸越快
 */
#define BREATH_DELAY_MS 10

/* ==================== 设备获取 ==================== */

/*
 * [获取 PWM 规格] PWM_DT_SPEC_GET 从设备树别名获取 PWM 完整配置
 * DT_ALIAS(pwm0): 通过设备树别名定位 PWM 节点
 * 返回的 pwm_spec 包含: PWM 设备指针、通道号、周期、极性标志
 */
static const struct pwm_dt_spec pwm_spec = PWM_DT_SPEC_GET(DT_ALIAS(pwm0));

/* ==================== 主函数 ==================== */

int main(void)
{
	int ret;                /* [变量] API 返回值 */
	uint32_t pulse_ns = 0;  /* [变量] 当前脉冲宽度(占空比)，单位纳秒 */
	int direction = 1;      /* [变量] 方向: 1=渐亮, -1=渐暗 */

	printk("\n========== PWM 呼吸灯示例 ==========\n");
	printk("硬件平台: ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("功能: GPIO4 PWM 呼吸灯\n");
	printk("频率: 50Hz, 周期: 20ms\n\n");

	/* ==================== 设备就绪检查 ==================== */

	/*
	 * [检查] 验证 PWM 设备是否可用
	 * pwm_is_ready_dt() 同时检查设备和 DT 配置
	 */
	if (!pwm_is_ready_dt(&pwm_spec)) {
		printk("!!! 错误: PWM 设备未就绪 (LEDC 通道 %u)，程序终止\n",
		       pwm_spec.channel);
		return -1;
	}
	printk("[初始化] LEDC PWM 通道 %u 就绪\n", pwm_spec.channel);

	/* ==================== 呼吸灯循环 ==================== */

	printk("[运行] 启动呼吸灯效果...\n\n");

	while (1) {
		/*
		 * [设置 PWM 占空比] 调节 LED 亮度
		 *
		 * pwm_set_pulse_dt(): 设置脉冲宽度(占空比)
		 *   参数:
		 *     &pwm_spec: PWM 设备规格 (包含设备、通道)
		 *     pulse_ns:  脉冲宽度(纳秒)，即高电平持续时间
		 *   返回值: 0 成功，负数失败
		 *
		 * 占空比计算: pulse_ns / PWM_PERIOD_NS
		 *   例如: pulse_ns=10,000,000, PWM_PERIOD_NS=20,000,000
		 *         占空比 = 10,000,000 / 20,000,000 = 50%
		 *
		 * 注意: 首次调用时会自动配置 LEDC 硬件
		 */
		ret = pwm_set_pulse_dt(&pwm_spec, pulse_ns);
		if (ret < 0) {
			printk("!!! 警告: PWM 设置失败 (err=%d)\n", ret);
		}

		/*
		 * [更新占空比] 按步长增加或减少脉冲宽度
		 * 如果正在渐亮 (direction=1): 增加脉冲宽度
		 * 如果正在渐暗 (direction=-1): 减少脉冲宽度
		 */
		if (direction == 1) {
			pulse_ns = pulse_ns + BREATH_STEP;
		} else {
			pulse_ns = pulse_ns - BREATH_STEP;
		}

		/*
		 * [边界检测] 达到最大或最小亮度时反转方向
		 *
		 * 当 pulse_ns >= PWM_PERIOD_NS 时: 占空比 100%，LED 最亮
		 *   此时改为渐暗 (direction = -1)
		 * 当 pulse_ns <= 0 时: 占空比 0%，LED 熄灭
		 *   此时改为渐亮 (direction = 1)
		 *
		 * 为了防止溢出，用 >= 和 <= 而非 ==
		 */
		if (pulse_ns >= PWM_PERIOD_NS) {
			pulse_ns = PWM_PERIOD_NS;   /* [钳位] 不超过周期 */
			direction = -1;              /* [反转] 改为渐暗 */
			printk("[呼吸] 最亮 -> 渐暗 (运行时间: %u ms)\n",
			       k_uptime_get_32());
		} else if (pulse_ns <= 0) {
			pulse_ns = 0;                /* [钳位] 不小于 0 */
			direction = 1;               /* [反转] 改为渐亮 */
			printk("[呼吸] 熄灭 -> 渐亮 (运行时间: %u ms)\n",
			       k_uptime_get_32());
		}

		/*
		 * [延时] 控制呼吸速度
		 * 延时 + 步长共同决定呼吸周期
		 * 例如: 20 步 × 10ms = 200ms (半周期), 400ms 完成一个完整呼吸
		 */
		k_msleep(BREATH_DELAY_MS);
	}

	return 0;
}
```

---

## 6. ADC — 模数转换器

### 功能说明
读取 ADC1 通道 3 (GPIO4) 上的模拟电压值。

### overlay 文件

```dts
#include <espressif/esp32s3/esp32s3_wroom_n32r16.dtsi>

/ {
	zephyr,user {
		/*
		 * [ADC 输入] 读取 ADC1 通道 3 (对应 GPIO4)
		 * io-channels = <&adc0 3>: 使用 ADC1 控制器，通道 3
		 */
		adc_channel: adc_channel {
			io-channels = <&adc0 3>;
		};
	};
};

&gpio0 {
	status = "okay";
};
```

### prj.conf 额外配置

```
CONFIG_ADC=y
CONFIG_ADC_ASYNC=y
```

### main.c

```c
/*
 * [ADC 示例] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - 使用 ADC1 通道 3 (GPIO4) 读取模拟电压
 *   - 每秒读取一次，打印原始值和换算后的电压值
 *
 * 硬件连接：
 *   - GPIO4 → 待测电压点 (0V ~ 3.3V)
 *
 * 注意事项：
 *   - ESP32-S3 ADC 参考电压约 1.1V，通过衰减器可测 0~3.3V
 *   - ADC2 与 Wi-Fi 共享，使用 Wi-Fi 时 ADC2 不可用
 *   - ADC 输入阻抗较低，高阻抗信号源需要加缓冲
 */

#include <zephyr/kernel.h>          /* [内核] k_msleep(), k_uptime_get() */
#include <zephyr/drivers/adc.h>     /* [ADC] adc_read(), adc_channel_setup() */
#include <zephyr/devicetree.h>      /* [设备树] ADC_DT_SPEC_GET */
#include <zephyr/sys/printk.h>      /* [打印] printk() */
#include <stdint.h>                 /* [标准类型] int16_t, uint32_t */

/* ==================== 宏定义 ==================== */

/* [采样间隔] 每 1000 毫秒 (1秒) 读取一次 */
#define ADC_READ_INTERVAL_MS 1000

/*
 * [ADC 参考电压] ESP32-S3 ADC 的最大输入电压 (经过衰减器后)
 * 单位: 毫伏 (mV)
 * 实际最大约 3100mV (3.1V)，取决于供电电压和衰减器配置
 */
#define ADC_VREF_MV 3100

/*
 * [ADC 分辨率] ESP32-S3 ADC 为 12 位
 * 12 位分辨率 = 2^12 = 4096 个量化等级 (0 ~ 4095)
 */
#define ADC_RESOLUTION 4096

/* ==================== 设备获取 ==================== */

/*
 * [获取 ADC 通道规格] ADC_DT_SPEC_GET 从设备树获取 ADC 配置
 * DT_NODELABEL(adc_channel): 指向 overlay 中定义的 ADC 节点
 * 返回的 adc_spec 包含: ADC 设备指针、通道号、参考电压等
 */
static const struct adc_dt_spec adc_spec =
	ADC_DT_SPEC_GET(DT_NODELABEL(adc_channel));

/* ==================== 函数: 读取并换算电压 ==================== */

/*
 * [函数] 读取 ADC 通道并换算为电压值 (毫伏)
 *
 * 返回值 >= 0: 电压值 (mV)
 * 返回值 < 0:  错误码
 */
static int read_adc_voltage(void)
{
	int ret;                    /* [变量] API 返回值 */
	int16_t sample_buffer;      /* [缓冲区] 存储 ADC 原始采样值 */
	struct adc_sequence seq;    /* [采样序列] 定义 ADC 采样参数 */

	/*
	 * [配置 ADC 采样序列]
	 *
	 * .channels:   要采样的通道位掩码
	 *              BIT(adc_spec.channel_id) 表示只采样指定通道
	 * .buffer:     数据缓冲区指针
	 * .buffer_size:缓冲区大小 (字节)
	 * .resolution: ADC 分辨率 (12 位)
	 * .oversampling: 过采样倍数 (0 表示不过采样)
	 * .calibrate:  是否进行自动校准
	 */
	seq.channels = BIT(adc_spec.channel_id);
	seq.buffer = &sample_buffer;
	seq.buffer_size = sizeof(sample_buffer);
	seq.resolution = 12;
	seq.oversampling = 0;
	seq.calibrate = true;

	/*
	 * [执行 ADC 采样]
	 *
	 * adc_read(): 阻塞等待 ADC 完成一次转换
	 *   参数1: ADC 设备 (从 adc_spec 获取)
	 *   参数2: 采样序列配置
	 *
	 * 返回值: 0 表示采样成功，sample_buffer 中存储原始值
	 */
	ret = adc_read(adc_spec.dev, &seq);
	if (ret < 0) {
		printk("[ADC] 读取失败 (err=%d)\n", ret);
		return ret;
	}

	/*
	 * [换算电压] 将 ADC 原始值转换为电压 (毫伏)
	 *
	 * 公式: voltage_mv = sample * VREF / RESOLUTION
	 *
	 * 例如: sample = 2048, VREF = 3100mV, RESOLUTION = 4096
	 *       voltage = 2048 * 3100 / 4096 = 1550mV = 1.55V
	 *
	 * 类型转换说明:
	 *   sample_buffer 是 int16_t (带符号 16 位)
	 *   先转为 int32_t 防止乘法溢出
	 */
	int32_t sample = (int32_t)sample_buffer;
	int32_t voltage_mv = (sample * ADC_VREF_MV) / ADC_RESOLUTION;

	return (int)voltage_mv;
}

/* ==================== 主函数 ==================== */

int main(void)
{
	int voltage_mv; /* [变量] 换算后的电压值 (mV) */

	printk("\n========== ADC 模数转换示例 ==========\n");
	printk("硬件平台: ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("功能: ADC1 CH3 (GPIO4) 电压读取\n");
	printk("参考电压: %d mV, 分辨率: 12 位\n\n", ADC_VREF_MV);

	/* ==================== 设备就绪检查 ==================== */

	/*
	 * [检查] 验证 ADC 设备是否可用
	 * adc_is_ready_dt() 同时检查设备和 DT 配置
	 */
	if (!adc_is_ready_dt(&adc_spec)) {
		printk("!!! 错误: ADC 设备未就绪，程序终止\n");
		return -1;
	}
	printk("[初始化] ADC1 通道 %u (GPIO4) 就绪\n", adc_spec.channel_id);

	/* ==================== ADC 通道配置 ==================== */

	/*
	 * [配置 ADC 通道] 设置衰减器和其他参数
	 *
	 * adc_channel_setup_dt(): 使用 DT 规格配置 ADC 通道
	 *   自动处理衰减器、参考电压等参数
	 *
	 * 衰减器说明:
	 *   ADC_GAIN_1:   满量程约 1.1V (默认)
	 *   ADC_GAIN_1_3: 满量程约 1.5V
	 *   ADC_GAIN_1_2: 满量程约 2.2V
	 *   ADC_GAIN_2_3: 满量程约 3.3V (推荐用于 0~3.3V 输入)
	 *
	 * 如果你需要测量 0~3.3V 的信号，要在 overlay 中配置 attenuation
	 * 或在 prj.conf 中设置默认衰减
	 */
	ret = adc_channel_setup_dt(&adc_spec);
	if (ret < 0) {
		printk("!!! 错误: ADC 通道配置失败 (err=%d)\n", ret);
		return -1;
	}
	printk("[配置] ADC1 通道 %u 配置完成\n", adc_spec.channel_id);

	/* ==================== 主循环 ==================== */

	printk("\n[运行] 开始周期 ADC 采样...\n\n");

	while (1) {
		/*
		 * [读取] 执行一次 ADC 采样并换算电压
		 */
		voltage_mv = read_adc_voltage();

		if (voltage_mv >= 0) {
			/*
			 * [输出] 打印采样结果
			 * 格式: 电压值 (mV) + 电压值 (V)
			 */
			printk("[ADC] GPIO4 电压: %d mV (%.2f V) | "
			       "运行时间: %u ms\n",
			       voltage_mv,
			       (float)voltage_mv / 1000.0f,
			       k_uptime_get_32());
		}

		/*
		 * [延时] 等待指定时间后再次采样
		 * 这里只是简单延时，实际应用中可使用定时器触发 ADC
		 */
		k_msleep(ADC_READ_INTERVAL_MS);
	}

	return 0;
}
```

---

## 7. RGB LED (WS2812) — 可寻址 RGB LED

### 功能说明
控制板载 WS2812 RGB LED (GPIO38) 显示不同颜色。

### overlay 文件

```dts
#include <espressif/esp32s3/esp32s3_wroom_n32r16.dtsi>

/ {
	aliases {
		led0 = &rgb_led;
	};

	leds {
		compatible = "gpio-leds";

		/*
		 * [RGB LED] 板载 WS2812 (v1.1 连接在 GPIO38)
		 */
		rgb_led: led_0 {
			gpios = <&gpio0 38 GPIO_ACTIVE_HIGH>;
			label = "板载 RGB LED";
		};
	};
};

&gpio0 {
	status = "okay";
};
```

### prj.conf 额外配置

```
CONFIG_LED_STRIP=y
CONFIG_LED_STRIP_WS2812=y
CONFIG_LED_STRIP_RGB_SCRATCH=y
```

### main.c

```c
/*
 * [RGB LED 示例] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - 控制板载 WS2812 RGB LED 依次显示红、绿、蓝、白
 *   - 每种颜色持续 1 秒
 *
 * 硬件连接：
 *   - GPIO38 → 板载 WS2812 RGB LED (内置)
 *   无需外部接线
 *
 * 注意事项：
 *   - GPIO38 被板载 LED 占用，如果需要将该引脚用作其他用途
 *     请先在 overlay 中禁用 rgb_led 节点
 *   - WS2812 时序要求严格，使用 Zephyr 的 LED Strip 驱动处理
 *   - 如果 LED 颜色不对，可能是 RGB 顺序不同 (RGB vs GRB)
 */

#include <zephyr/kernel.h>              /* [内核] k_msleep() */
#include <zephyr/drivers/led_strip.h>   /* [LED Strip] led_strip_update_rgb() */
#include <zephyr/devicetree.h>          /* [设备树] DEVICE_DT_GET, DT_ALIAS */
#include <zephyr/sys/printk.h>          /* [打印] printk() */

/* ==================== 宏定义 ==================== */

/*
 * [颜色定义] RGB 各通道亮度 (0~255)
 * WS2812 每个颜色 8 位 (0=灭, 255=最亮)
 */

/* [红色] 亮红色: R=255, G=0, B=0 */
#define LED_RED()    led_strip_set_rgb(led_strip_dev, 0, 255, 0, 0)

/* [绿色] 亮绿色: R=0, G=255, B=0 */
#define LED_GREEN()  led_strip_set_rgb(led_strip_dev, 0, 0, 255, 0)

/* [蓝色] 亮蓝色: R=0, G=0, B=255 */
#define LED_BLUE()   led_strip_set_rgb(led_strip_dev, 0, 0, 0, 255)

/* [白色] 三通道全亮: R=255, G=255, B=255 */
#define LED_WHITE()  led_strip_set_rgb(led_strip_dev, 0, 255, 255, 255)

/* [熄灭] 三通道全灭 */
#define LED_OFF()    led_strip_set_rgb(led_strip_dev, 0, 0, 0, 0)

/* [颜色切换间隔] 每种颜色显示 1 秒 */
#define COLOR_INTERVAL_MS 1000

/* ==================== 设备获取 ==================== */

/*
 * [获取 LED Strip 设备]
 * DEVICE_DT_GET(DT_ALIAS(led0)): 通过设备树别名获取 LED 设备
 * 这个设备是 WS2812 LED Strip 驱动的实例
 */
static const struct device *const led_strip_dev =
	DEVICE_DT_GET(DT_ALIAS(led0));

/* ==================== 函数: 设置并刷新 LED ==================== */

/*
 * [函数] 设置 LED 颜色并刷新到硬件
 *
 * led_strip_set_rgb() 只是更新内存中的颜色缓冲区
 * 必须调用 led_strip_update_rgb() 才能实际输出到 WS2812
 *
 * 参数:
 *   dev:    LED Strip 设备
 *   index:  LED 索引 (0 表示链中的第一个 LED)
 *   r,g,b:  红、绿、蓝分量 (0~255)
 */
static void set_led_color(const struct device *dev,
                          uint8_t r, uint8_t g, uint8_t b)
{
	int ret; /* [变量] API 返回值 */

	/*
	 * [步骤1] 设置颜色缓冲区
	 * 这个操作不涉及硬件，只是更新驱动内部的数据
	 */
	ret = led_strip_set_rgb(dev, 0, r, g, b);
	if (ret < 0) {
		printk("!!! 警告: LED 颜色设置失败 (err=%d)\n", ret);
		return;
	}

	/*
	 * [步骤2] 刷新到硬件
	 * 只有调用此函数才会实际向 WS2812 发送数据
	 * 包括发送 RESET 信号 (>50μs 低电平) 和 24 位颜色数据
	 */
	ret = led_strip_update_rgb(dev, 0, 1);
	if (ret < 0) {
		printk("!!! 警告: LED 刷新失败 (err=%d)\n", ret);
		return;
	}
}

/* ==================== 主函数 ==================== */

int main(void)
{
	printk("\n========== RGB LED (WS2812) 示例 ==========\n");
	printk("硬件平台: ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("功能: 板载 WS2812 RGB LED (GPIO38) 颜色循环\n\n");

	/* ==================== 设备就绪检查 ==================== */

	if (!device_is_ready(led_strip_dev)) {
		printk("!!! 错误: LED Strip (WS2812) 设备未就绪，程序终止\n");
		printk("    请确认 prj.conf 中启用了:\n");
		printk("      CONFIG_LED_STRIP=y\n");
		printk("      CONFIG_LED_STRIP_WS2812=y\n");
		return -1;
	}
	printk("[初始化] WS2812 LED Strip 设备就绪 (GPIO38)\n");

	/* ==================== 颜色循环 ==================== */

	printk("[运行] 开始颜色循环 (红 → 绿 → 蓝 → 白)...\n\n");

	while (1) {
		/*
		 * [红色] R=255, G=0, B=0
		 */
		printk("[LED] 红色 (R:255, G:0, B:0)\n");
		set_led_color(led_strip_dev, 255, 0, 0);
		k_msleep(COLOR_INTERVAL_MS);

		/*
		 * [绿色] R=0, G=255, B=0
		 */
		printk("[LED] 绿色 (R:0, G:255, B:0)\n");
		set_led_color(led_strip_dev, 0, 255, 0);
		k_msleep(COLOR_INTERVAL_MS);

		/*
		 * [蓝色] R=0, G=0, B=255
		 */
		printk("[LED] 蓝色 (R:0, G:0, B:255)\n");
		set_led_color(led_strip_dev, 0, 0, 255);
		k_msleep(COLOR_INTERVAL_MS);

		/*
		 * [白色] R=255, G=255, B=255 (三通道全亮)
		 */
		printk("[LED] 白色 (R:255, G:255, B:255)\n");
		set_led_color(led_strip_dev, 255, 255, 255);
		k_msleep(COLOR_INTERVAL_MS);

		/*
		 * [熄灭] 短暂熄灯表示一轮循环结束
		 */
		set_led_color(led_strip_dev, 0, 0, 0);
		k_msleep(200);
	}

	return 0;
}
```

---

## 8. 中断处理 (GPIO 中断)

### 功能说明
配置 GPIO6 为中断触发引脚，按下按键时触发中断回调。

### main.c 关键代码 (overlay 与 GPIO 示例相同)

```c
/*
 * [GPIO 中断示例] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - 配置 GPIO6 为中断输入引脚
 *   - 按下按键时触发中断回调函数
 *   - 使用工作队列 (work queue) 将中断处理延迟到线程上下文
 *
 * 关键概念：
 *   - 中断上下文不能做耗时操作 (打印、延时、内存分配)
 *   - 使用 gpio_pin_interrupt_configure_dt() 配置中断
 *   - 使用 gpio_init_callback() 和 gpio_add_callback() 注册回调
 */

#include <zephyr/kernel.h>          /* [内核] k_work, k_work_submit() */
#include <zephyr/drivers/gpio.h>    /* [GPIO] gpio_pin_interrupt_* */
#include <zephyr/devicetree.h>      /* [设备树] GPIO_DT_SPEC_GET */
#include <zephyr/sys/printk.h>      /* [打印] printk() */

/* ==================== 设备获取 ==================== */

/*
 * [按键 GPIO 规格] 从设备树获取按键的 GPIO 配置
 * 包括控制器指针、引脚号、flags (上拉/触发极性等)
 */
static const struct gpio_dt_spec button_spec =
	GPIO_DT_SPEC_GET(DT_NODELABEL(user_button), gpios);

/*
 * [GPIO 回调结构体] gpio_callback 是 Zephyr 定义的 GPIO 回调注册结构
 * 每个需要中断回调的引脚都需要一个独立的结构体
 */
static struct gpio_callback button_callback_data;

/* ==================== 工作队列 ==================== */

/*
 * [工作队列项] k_work 用于将中断处理延迟到系统工作队列
 *
 * 为什么需要工作队列:
 *   ISR (中断服务程序) 在中断上下文中执行
 *   中断上下文不能调用可能阻塞的函数 (如 printk, k_msleep)
 *   通过 k_work_submit() 将实际处理提交到系统工作队列
 *   系统工作队列在线程上下文中执行，可以做任何操作
 */
static struct k_work button_work;

/* ==================== 工作队列处理函数 ==================== */

/*
 * [工作处理函数] 在线程上下文中执行 (非中断上下文)
 * 可以安全地调用 printk、延时、内存分配等
 *
 * 参数 work: 触发此函数的工作队列项
 */
static void button_work_handler(struct k_work *work)
{
	/*
	 * [打印] 在中断上下文的 printk 可能会有延迟
	 * 在线程上下文中打印可以确保及时输出
	 */
	printk("[按键事件] 按键被按下! (运行时间: %u ms)\n",
	       k_uptime_get_32());
}

/* ==================== GPIO 中断回调 (ISR) ==================== */

/*
 * [中断回调] GPIO 中断服务程序 (ISR)
 *
 * 此函数在中断上下文中执行! 必须遵守以下规则:
 *   1. 不要调用任何可能阻塞的函数 (sleep, mutex_lock 等)
 *   2. 尽量简短，只做必要的数据搬运或标志置位
 *   3. 耗时处理通过 k_work_submit() 提交到工作队列
 *
 * 参数:
 *   port: GPIO 控制器设备指针
 *   cb:   触发中断的回调结构体
 *   pins: 触发中断的引脚位掩码
 */
static void button_isr(const struct device *port,
                       struct gpio_callback *cb,
                       gpio_port_pins_t pins)
{
	/*
	 * [提交工作] 将实际处理延迟到线程上下文
	 * k_work_submit() 可以在中断上下文中调用
	 * 它是线程安全的，内部使用了原子操作
	 */
	k_work_submit(&button_work);
}

/* ==================== 主函数 ==================== */

int main(void)
{
	int ret; /* [变量] API 返回值 */

	printk("\n========== GPIO 中断示例 ==========\n");
	printk("硬件平台: ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("功能: GPIO6 中断按键检测\n\n");

	/* ==================== 设备就绪检查 ==================== */

	if (!gpio_is_ready_dt(&button_spec)) {
		printk("!!! 错误: 按键 GPIO 设备未就绪，程序终止\n");
		return -1;
	}
	printk("[初始化] 按键 GPIO%u 设备就绪\n", button_spec.pin);

	/* ==================== 引脚配置 ==================== */

	/*
	 * [配置输入] 将按键引脚配置为输入模式
	 * button_spec.dt_flags 包含 GPIO_PULL_UP | GPIO_ACTIVE_LOW
	 */
	ret = gpio_pin_configure_dt(&button_spec, GPIO_INPUT);
	if (ret < 0) {
		printk("!!! 错误: 按键引脚配置失败 (err=%d)\n", ret);
		return -1;
	}

	/*
	 * [配置中断] 设置中断触发条件
	 *
	 * GPIO_INT_EDGE_TO_ACTIVE: 从 inactive 变为 active 时触发
	 *   由于配置了 GPIO_ACTIVE_LOW，active = 低电平
	 *   所以这表示下降沿触发 (高→低)
	 *
	 * 其他可选模式:
	 *   GPIO_INT_EDGE_TO_INACTIVE: 上升沿触发 (低→高)
	 *   GPIO_INT_EDGE_BOTH: 双边沿触发
	 *   GPIO_INT_LEVEL_ACTIVE: 电平触发 (需手动清除)
	 */
	ret = gpio_pin_interrupt_configure_dt(&button_spec,
	                                      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		printk("!!! 错误: 中断配置失败 (err=%d)\n", ret);
		return -1;
	}
	printk("[配置] GPIO%u 中断模式: 下降沿触发\n", button_spec.pin);

	/* ==================== 中断回调注册 ==================== */

	/*
	 * [初始化回调结构体] gpio_init_callback() 将回调函数与结构体绑定
	 * 参数:
	 *   &button_callback_data: 回调结构体
	 *   button_isr:            回调函数 (ISR)
	 *   BIT(button_spec.pin):  监听引脚位掩码
	 */
	gpio_init_callback(&button_callback_data, button_isr,
	                   BIT(button_spec.pin));

	/*
	 * [注册回调] gpio_add_callback() 将回调注册到 GPIO 控制器
	 * 参数:
	 *   button_spec.port:          GPIO 控制器设备指针
	 *   &button_callback_data:     已初始化的回调结构体
	 *
	 * 注册后，当指定引脚发生中断时，button_isr() 会被自动调用
	 */
	ret = gpio_add_callback(button_spec.port, &button_callback_data);
	if (ret < 0) {
		printk("!!! 错误: 回调注册失败 (err=%d)\n", ret);
		return -1;
	}
	printk("[配置] 中断回调已注册\n");

	/* ==================== 初始化工作队列 ==================== */

	/*
	 * [初始化工作项] k_work_init() 将工作处理函数与工作项绑定
	 * 参数:
	 *   &button_work:         工作队列项
	 *   button_work_handler:  工作处理函数 (线程上下文)
	 */
	k_work_init(&button_work, button_work_handler);
	printk("[配置] 工作队列项已初始化\n");

	/* ==================== 主循环 ==================== */

	printk("\n[运行] 等待按键中断... (按 Ctrl+C 退出)\n");
	printk("  按下连接在 GPIO%u 的按键触发中断\n\n", button_spec.pin);

	while (1) {
		/*
		 * [主循环] 主线程可以休眠或做其他低优先级任务
		 * 中断处理完全异步，不依赖主循环轮询
		 */
		k_msleep(1000);
	}

	return 0;
}
```

---

## 9. 线程与多任务

### 功能说明
创建 3 个独立线程：LED 闪烁线程、按键检测线程、传感器读取线程。

```c
/*
 * [多线程示例] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - 线程1: 每 500ms 切换一次 GPIO4 (LED 闪烁)
 *   - 线程2: 每 200ms 检测一次 GPIO6 按键状态
 *   - 线程3: 每 1000ms 打印一次心跳信息
 *
 * 关键概念：
 *   - Zephyr 线程是抢占式的，高优先级线程可抢占低优先级
 *   - 每个线程有独立的栈空间
 *   - 使用 K_THREAD_DEFINE 静态定义线程
 */

#include <zephyr/kernel.h>          /* [内核] k_thread_create, K_THREAD_DEFINE */
#include <zephyr/drivers/gpio.h>    /* [GPIO] gpio_pin_* */
#include <zephyr/devicetree.h>      /* [设备树] GPIO_DT_SPEC_GET */
#include <zephyr/sys/printk.h>      /* [打印] printk() */

/* ==================== 线程栈定义 ==================== */

/*
 * [线程栈] 每个线程需要独立的栈空间
 *
 * 栈大小说明:
 *   LED_STACK_SIZE:  512 字节 (LED 闪烁任务简单，不需要太多栈)
 *   BUTTON_STACK_SIZE: 1024 字节 (按键检测稍复杂)
 *   HEARTBEAT_STACK_SIZE: 512 字节 (心跳打印任务简单)
 *
 * 栈大小建议:
 *   - 简单任务: 512 ~ 1024 字节
 *   - 中等任务: 1024 ~ 2048 字节
 *   - 复杂任务 (使用 printf/传感器/I2C): 2048 ~ 4096 字节
 *   - 可用 k_thread_stack_space_get() 检查实际使用量
 */
#define LED_STACK_SIZE       512
#define BUTTON_STACK_SIZE   1024
#define HEARTBEAT_STACK_SIZE 512

/*
 * [线程优先级] Zephyr 优先级：数字越小优先级越高
 * 0 = 最高优先级 (协程)
 * 负数 = 协作线程 (不被抢占)
 * 正数 = 抢占式线程 (数字越小越优先)
 *
 * 这里设置:
 *   按键检测优先级 = 2 (较高，需要快速响应)
 *   LED 闪烁优先级 = 5 (中等)
 *   心跳打印优先级 = 7 (最低)
 */
#define BUTTON_PRIORITY    2
#define LED_PRIORITY       5
#define HEARTBEAT_PRIORITY 7

/* ==================== 设备获取 ==================== */

static const struct gpio_dt_spec led_spec =
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button_spec =
	GPIO_DT_SPEC_GET(DT_NODELABEL(user_button), gpios);

/* ==================== 线程函数定义 ==================== */

/*
 * [线程函数 1] LED 闪烁线程
 * 每 500ms 翻转一次 LED 状态
 *
 * 参数: 三个未使用的参数是 K_THREAD_DEFINE 要求的标准签名
 */
void led_blink_thread(void *arg1, void *arg2, void *arg3)
{
	/* [抑制警告] 未使用的参数用 ARG_UNUSED 标记 */
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	printk("[LED 线程] 启动 (优先级=%d)\n", LED_PRIORITY);

	while (1) {
		/*
		 * [翻转 LED] 每 500ms 切换一次
		 * gpio_pin_toggle_dt() 自动读取当前状态并翻转
		 */
		gpio_pin_toggle_dt(&led_spec);
		k_msleep(500);
	}
}

/*
 * [线程函数 2] 按键检测线程
 * 每 200ms 查询一次按键状态，检测到按下时打印
 */
void button_check_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	int state;          /* [变量] 当前按键状态 */
	int last_state = 1; /* [变量] 上次状态，初始化为未按下 */

	printk("[按键线程] 启动 (优先级=%d)\n", BUTTON_PRIORITY);

	while (1) {
		/*
		 * [读取按键] gpio_pin_get_dt() 返回当前电平
		 * 由于配置了 GPIO_ACTIVE_LOW:
		 *   返回 0: 按键按下 (低电平)
		 *   返回 1: 按键未按下 (高电平)
		 */
		state = gpio_pin_get_dt(&button_spec);

		/*
		 * [下降沿检测] 从高→低 = 按键刚按下
		 * 简单的软件消抖：两次检测间隔 200ms 已自然消抖
		 */
		if (state == 0 && last_state == 1) {
			printk("[按键] 按下事件 (时间: %u ms)\n",
			       k_uptime_get_32());
		}
		last_state = state;

		k_msleep(200);
	}
}

/*
 * [线程函数 3] 心跳打印线程
 * 每 1000ms (1秒) 打印系统运行时间
 */
void heartbeat_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	printk("[心跳线程] 启动 (优先级=%d)\n", HEARTBEAT_PRIORITY);

	while (1) {
		/*
		 * [打印心跳] 显示系统已运行时间
		 * k_uptime_get_32() 返回系统启动后的毫秒数 (32位)
		 * 最大值约 49.7 天，之后会回绕
		 */
		printk("[心跳] 系统运行中... (%u 秒)\n",
		       k_uptime_get_32() / 1000);
		k_msleep(1000);
	}
}

/* ==================== 线程定义 ==================== */

/*
 * K_THREAD_DEFINE: 静态定义并启动线程
 *
 * 参数说明:
 *   name:              线程变量名 (在代码中引用)
 *   stack_size:        栈大小 (字节)
 *   entry:             线程入口函数
 *   p1, p2, p3:        传递给入口函数的参数 (可为 NULL)
 *   prio:              线程优先级
 *   options:           线程选项 (0 = 默认)
 *   delay:             启动前延时 (ms, 0 = 立即启动)
 *
 * 使用 K_THREAD_DEFINE 而不是 k_thread_create() 的原因:
 *   - 编译时分配栈空间 (静态)，更可靠
 *   - 不需要手动管理内存
 *   - 线程在系统启动时自动创建
 */

K_THREAD_DEFINE(led_tid, LED_STACK_SIZE,
                led_blink_thread, NULL, NULL, NULL,
                LED_PRIORITY, 0, 0);

K_THREAD_DEFINE(button_tid, BUTTON_STACK_SIZE,
                button_check_thread, NULL, NULL, NULL,
                BUTTON_PRIORITY, 0, 0);

K_THREAD_DEFINE(heartbeat_tid, HEARTBEAT_STACK_SIZE,
                heartbeat_thread, NULL, NULL, NULL,
                HEARTBEAT_PRIORITY, 0, 0);

/* ==================== 主函数 ==================== */

int main(void)
{
	printk("\n========== 多线程示例 ==========\n");
	printk("硬件平台: ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("线程: LED闪烁 + 按键检测 + 心跳打印\n\n");

	/* [设备检查] 确保所需设备就绪 */
	if (!gpio_is_ready_dt(&led_spec)) {
		printk("!!! 错误: LED 设备未就绪\n");
		return -1;
	}
	if (!gpio_is_ready_dt(&button_spec)) {
		printk("!!! 错误: 按键设备未就绪\n");
		return -1;
	}

	/* [配置引脚] */
	gpio_pin_configure_dt(&led_spec, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&button_spec, GPIO_INPUT);

	printk("[主线程] 所有线程已启动，系统运行中...\n\n");

	/*
	 * [主线程] 进入空闲循环
	 * main() 函数本身运行在系统的主线程中
	 * 子线程在后台并发运行
	 */
	while (1) {
		k_msleep(10000); /* 主线程休眠 10 秒，期间子线程正常运行 */
	}

	return 0;
}
```

---

## 10. Wi-Fi — 无线网络连接

### 功能说明
ESP32-S3 连接 Wi-Fi 接入点并获取 IP 地址。

### overlay 文件

```dts
#include <espressif/esp32s3/esp32s3_wroom_n32r16.dtsi>

&wifi {
	status = "okay";
};
```

### prj.conf 额外配置

```
CONFIG_WIFI=y
CONFIG_WIFI_ESP32=y
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_DHCPV4=y
CONFIG_NET_MGMT_EVENT=y
```

`prj.conf` 还需要设置 Wi-Fi SSID 和密码 (通过 Kconfig 或运行时):

```
CONFIG_WIFI_ESP32_SSID="你的WiFi名称"
CONFIG_WIFI_ESP32_PASSWORD="你的WiFi密码"
```

---

## 11. BLE — 蓝牙低功耗

### overlay 文件

```dts
#include <espressif/esp32s3/esp32s3_wroom_n32r16.dtsi>

&esp32_bt_hci {
	status = "okay";
};
```

### prj.conf 额外配置

```
CONFIG_BT=y
CONFIG_BT_ESP32=y
CONFIG_BT_HCI=y
CONFIG_BT_PERIPHERAL=y
```

---

## 12. PSRAM — 外扩内存使用

### 功能说明
使用 ESP32-S3 外扩的 16MB PSRAM 进行大块内存分配。

### prj.conf 额外配置

```
CONFIG_ESP32_SPIRAM=y
CONFIG_SHARED_MULTI_HEAP=y
```

### main.c

```c
/*
 * [PSRAM 示例] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - 从 PSRAM 分配大块内存
 *   - 验证 PSRAM 读写正确性
 *   - 对比 PSRAM 与内部 SRAM 的分配
 *
 * N32R16V 的 PSRAM:
 *   - 容量: 16MB (Octal SPI @ 1.8V)
 *   - 速度: 比内部 SRAM 慢，但容量大得多
 *   - 用途: 图像缓冲、音频缓冲、大型数据结构
 *
 * 注意事项：
 *   - PSRAM 访问速度慢于内部 SRAM，不适合频繁访问的小数据
 *   - 中断上下文中不应访问 PSRAM (可能有延迟)
 *   - 使用 shared_multi_heap 管理 PSRAM 分配
 */

#include <zephyr/kernel.h>                  /* [内核] k_msleep(), k_uptime_get() */
#include <zephyr/multi_heap/shared_multi_heap.h> /* [共享堆] shared_multi_heap_* */
#include <zephyr/sys/printk.h>              /* [打印] printk() */
#include <soc/soc_memory_layout.h>          /* [内存布局] esp_ptr_external_ram() */

/* ==================== 宏定义 ==================== */

/*
 * [测试缓冲区大小] 从 PSRAM 分配 32KB
 * 这个大小在内部 SRAM 中可能无法分配 (内部 SRAM 仅约 512KB)
 */
#define PSRAM_BUF_SIZE 32768

/*
 * [填充模式] 测试中使用的固定数据模式
 * 写入已知数据后回读，验证 PSRAM 读写正确性
 */
#define TEST_PATTERN_BYTE  0xA5  /* [8位模式]  10100101 */
#define TEST_PATTERN_WORD  0xAA55 /* [16位模式] 1010101001010101 */

/* ==================== 主函数 ==================== */

int main(void)
{
	uint8_t *psram_buf;     /* [PSRAM 指针] 从 PSRAM 分配的缓冲区 */
	uint8_t *sram_buf;      /* [SRAM 指针] 从内部 SRAM 分配的缓冲区 */
	int err_count = 0;      /* [错误计数] 记录 PSRAM 读写错误数 */
	uint32_t start_time;    /* [计时] 记录操作开始时间 */
	uint32_t elapsed_ms;    /* [计时] 操作耗时 (毫秒) */

	printk("\n========== PSRAM 外扩内存示例 ==========\n");
	printk("硬件平台: ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("PSRAM: 16MB Octal SPI @ 1.8V\n\n");

	/* ==================== 从 PSRAM 分配内存 ==================== */

	printk("[分配] 从 PSRAM 申请 %d 字节...\n", PSRAM_BUF_SIZE);

	/*
	 * [PSRAM 分配] shared_multi_heap_aligned_alloc()
	 *
	 * 参数:
	 *   SMH_REG_ATTR_EXTERNAL: 指定从外部 PSRAM 分配
	 *   32: 字节对齐 (32 字节对齐，提高访问效率)
	 *   PSRAM_BUF_SIZE: 分配大小
	 *
	 * 返回值: 指向分配内存的指针，失败返回 NULL
	 *
	 * 为什么使用 shared_multi_heap 而不是 k_malloc():
	 *   k_malloc() 从内部 SRAM 分配，容量有限
	 *   shared_multi_heap 可以管理 PSRAM 和 SRAM 的统一内存池
	 */
	psram_buf = shared_multi_heap_aligned_alloc(
		SMH_REG_ATTR_EXTERNAL, 32, PSRAM_BUF_SIZE);

	if (psram_buf == NULL) {
		printk("!!! 错误: PSRAM 内存分配失败\n");
		printk("    请确认 prj.conf 中启用了 CONFIG_ESP32_SPIRAM=y\n");
		return -1;
	}

	/*
	 * [验证地址范围] 确认分配的内存确实在 PSRAM 区域
	 * esp_ptr_external_ram() 检查指针是否指向外部 PSRAM 地址空间
	 * ESP32-S3 PSRAM 映射到 0x3C000000 ~ 0x3F800000 地址范围
	 */
	if (esp_ptr_external_ram(psram_buf)) {
		printk("[分配] 成功! 缓冲区地址: %p (PSRAM 区域)\n",
		       (void *)psram_buf);
	} else {
		printk("!!! 警告: 分配的缓冲区不在 PSRAM 地址范围\n");
	}

	/* ==================== PSRAM 写入测试 ==================== */

	printk("\n[测试] 向 PSRAM 写入测试数据...\n");
	start_time = k_uptime_get_32();

	/*
	 * [写入 PSRAM] 逐字节写入测试模式
	 * 使用固定模式 0xA5，写入后回读验证
	 */
	for (int i = 0; i < PSRAM_BUF_SIZE; i = i + 1) {
		psram_buf[i] = TEST_PATTERN_BYTE;
	}

	elapsed_ms = k_uptime_get_32() - start_time;
	printk("[写入] 完成! %d 字节写入耗时 %u ms\n",
	       PSRAM_BUF_SIZE, elapsed_ms);

	/* ==================== PSRAM 回读验证 ==================== */

	printk("\n[验证] 回读并验证 PSRAM 数据...\n");
	start_time = k_uptime_get_32();

	/*
	 * [回读验证] 逐字节检查写入的数据
	 * 如果有不一致，记录错误数
	 */
	for (int i = 0; i < PSRAM_BUF_SIZE; i = i + 1) {
		if (psram_buf[i] != TEST_PATTERN_BYTE) {
			err_count = err_count + 1;
			/*
			 * [错误详情] 只打印前 10 个错误，避免刷屏
			 */
			if (err_count <= 10) {
				printk("  [错误] 地址 %d: 期望 0x%02X, 实际 0x%02X\n",
				       i, TEST_PATTERN_BYTE, psram_buf[i]);
			}
		}
	}

	elapsed_ms = k_uptime_get_32() - start_time;

	if (err_count == 0) {
		printk("[验证] ✓ PSRAM 读写测试通过! (%d 字节验证耗时 %u ms)\n",
		       PSRAM_BUF_SIZE, elapsed_ms);
	} else {
		printk("[验证] ✗ PSRAM 读写测试失败! 共 %d 个字节错误\n",
		       err_count);
	}

	/* ==================== 内部 SRAM vs PSRAM 对比 ==================== */

	printk("\n[对比] 内部 SRAM 分配测试...\n");

	/*
	 * [SRAM 分配] 使用 k_malloc() 从内部 SRAM 分配
	 * 内部 SRAM 速度更快但容量有限
	 * 注意: 分配大块内存 (>几KB) 可能失败
	 */
	sram_buf = k_malloc(4096); /* 分配 4KB 从内部 SRAM */
	if (sram_buf != NULL) {
		printk("[分配] 内部 SRAM 4KB 分配成功 (地址: %p)\n",
		       (void *)sram_buf);

		/*
		 * [验证 SRAM 地址] esp_ptr_internal() 检查指针是否指向内部 SRAM
		 * ESP32-S3 内部 SRAM 映射到 0x3FC80000 ~ 0x3FCE0000
		 */
		if (esp_ptr_internal(sram_buf)) {
			printk("[验证] 确认为内部 SRAM 地址范围\n");
		}

		k_free(sram_buf); /* [释放] 归还内部 SRAM */
	} else {
		printk("[分配] 内部 SRAM 4KB 分配失败 (可能内存碎片过多)\n");
	}

	/* ==================== PSRAM 字级和双字级测试 ==================== */

	printk("\n[高级测试] PSRAM 16位和32位读写测试...\n");

	/*
	 * [字级测试] 将 PSRAM 缓冲区视为 16 位数组进行测试
	 * 验证 PSRAM 的非对齐访问能力
	 */
	uint16_t *psram_w = (uint16_t *)psram_buf;
	int word_errors = 0;
	int word_count = PSRAM_BUF_SIZE / 2; /* 32KB / 2 = 16384 个 16 位字 */

	for (int i = 0; i < word_count; i = i + 1) {
		psram_w[i] = TEST_PATTERN_WORD;
	}

	for (int i = 0; i < word_count; i = i + 1) {
		if (psram_w[i] != TEST_PATTERN_WORD) {
			word_errors = word_errors + 1;
		}
	}

	if (word_errors == 0) {
		printk("[验证] ✓ PSRAM 16位读写测试通过! (%d 个字)\n", word_count);
	} else {
		printk("[验证] ✗ PSRAM 16位读写测试失败! (%d 个错误)\n", word_errors);
	}

	/* ==================== 释放 PSRAM 内存 ==================== */

	/*
	 * [释放] shared_multi_heap_free() 释放从 PSRAM 分配的内存
	 * 参数为 shared_multi_heap_aligned_alloc() 返回的指针
	 */
	shared_multi_heap_free(psram_buf);
	printk("\n[释放] PSRAM 缓冲区已归还\n");

	/* ==================== 完成 ==================== */

	printk("\n========== PSRAM 测试完成 ==========\n");
	printk("结论: N32R16V 的 16MB PSRAM 可用于大块数据存储\n");
	printk("注意事项:\n");
	printk("  1. PSRAM 速度慢于内部 SRAM，适合大块数据不适合频繁小访问\n");
	printk("  2. 不要在中断上下文中访问 PSRAM\n");
	printk("  3. 确保分配后的地址校验 (esp_ptr_external_ram)\n");

	while (1) {
		k_msleep(10000);
	}

	return 0;
}
```
