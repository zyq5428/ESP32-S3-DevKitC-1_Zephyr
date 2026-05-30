# 项目模板指南

> 创建新的 Zephyr 项目时，生成以下完整文件。所有文件必须完整，不使用省略号。

---

## 文件结构总览

```
├── CMakeLists.txt          # CMake 构建文件
├── prj.conf                # Kconfig 应用配置
├── Kconfig                 # (可选) 应用级 Kconfig 选项
├── sysbuild.conf           # (可选) 多映像/多固件构建（Sysbuild）的配置文件
├── README.md               # 项目的说明文档，通常包含项目的开发板型号、项目目标，以及如何编译、接线和运行。
├── boards/
│   └── esp32s3_devkitc_esp32s3_procpu.overlay  # 设备树覆盖文件 ★关键
├── include/
│   └── xxx_thread.h        # (可选) 应用程序头文件
├── src/
│   └── main.c              # 主程序 (含详尽中文注释)
└── sysbuild/               # (可选) 二级引导程序配置
    └── mcuboot.conf        # 为MCUboot (Bootloader) 准备的配置文件
    └── mcuboot.overlay     # 为MCUboot (Bootloader) 准备的设备树覆盖文件
```

---

## CMakeLists.txt — 构建入口

每个项目都需要此文件，用于告诉 CMake 如何构建 Zephyr 应用。

```cmake
# SPDX-License-Identifier: Apache-2.0
# [构建配置] Zephyr 项目的 CMake 构建入口文件
# 每个 Zephyr 应用必须包含此文件，它定义了项目名称和源文件

# [CMake 版本要求] 指定构建所需的最低 CMake 版本
# Zephyr 3.x 要求至少 CMake 3.20.0
cmake_minimum_required(VERSION 3.20.0)

# [查找 Zephyr 包] 引入 Zephyr 构建系统的所有功能
# REQUIRED: 如果找不到 Zephyr SDK 则构建失败
# HINTS: 通过环境变量 ZEPHYR_BASE 定位 Zephyr 安装位置
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# [定义项目] 设置项目名称，这个名字会出现在编译信息中
# 建议使用英文小写 + 下划线命名
project(my_esp32s3_app)

# [添加源文件] 指定要编译的源文件
# app 是 Zephyr 预定义的应用目标，PRIVATE 表示仅本项目使用
# 如果有多个源文件，用空格分隔：src/main.c src/helper.c
target_sources(app PRIVATE src/main.c)
```

---

## prj.conf — Kconfig 应用配置

此文件用于启用 Zephyr 的子系统、驱动和功能选项。
根据项目需求启用相应的配置项。

### 基础配置 (所有项目通用)

```
# ==================== Zephyr 基础配置 ====================

# [控制台] 启用串口控制台输出，printk/printf 通过串口输出
CONFIG_CONSOLE=y

# [串口驱动] 启用串口 (UART) 驱动子系统
CONFIG_SERIAL=y

# [UART 控制台] 将控制台绑定到 UART 外设(而非 USB 或 SEMIHOST)
CONFIG_UART_CONSOLE=y

# [GPIO 驱动] 启用 GPIO 通用输入输出驱动
CONFIG_GPIO=y

# [日志系统] 启用 Zephyr 日志子系统，支持 LOG_INF/LOG_ERR 等宏
CONFIG_LOG=y

# [断言] 启用内核断言检查，开发阶段建议开启
CONFIG_ASSERT=y
```

### ESP32-S3-N32R16V 专用配置

```
# ==================== ESP32-S3-N32R16V 专用配置 ====================

# [时钟控制] 启用时钟控制驱动，ESP32-S3 外设工作需要
CONFIG_CLOCK_CONTROL=y

# [PSRAM 支持] ★关键★ 启用 ESP32-S3 的 PSRAM 支持
# N32R16V 模组有 16MB PSRAM，必须启用此选项才能使用
CONFIG_ESP32_SPIRAM=y

# [Wi-Fi 支持] (可选) 如需 Wi-Fi 功能，取消以下注释
# CONFIG_WIFI=y
# CONFIG_WIFI_ESP32=y
# CONFIG_NETWORKING=y

# [蓝牙支持] (可选) 如需 BLE 功能，取消以下注释
# CONFIG_BT=y
# CONFIG_BT_ESP32=y
```

### 外设专用配置按需添加

```
# ==================== 按需启用的外设配置 ====================

# [I2C 驱动] 如需 I2C 总线通信
# CONFIG_I2C=y

# [SPI 驱动] 如需 SPI 总线通信
# CONFIG_SPI=y

# [PWM / LEDC 驱动] 如需 PWM 输出(ESP32-S3 使用 LEDC 实现 PWM)
# CONFIG_PWM=y
# CONFIG_LEDC=y

# [ADC 驱动] 如需模数转换器读取电压
# CONFIG_ADC=y

# [看门狗] 如需看门狗定时器
# CONFIG_WATCHDOG=y

# [传感器子系统] 如需使用各类传感器驱动
# CONFIG_SENSOR=y

# [Shell 子系统] 如需命令行交互
# CONFIG_SHELL=y
```

---

## boards/esp32s3_devkitc_esp32s3_procpu.overlay — 设备树覆盖

**这是整个项目中最关键的文件之一。** 它用于覆盖 Zephyr 板级默认的设备树配置，
以适应 N32R16V 模组和你的外设需求。

### 基础覆盖 (N32R16V 配置 + 常用外设)

```dts
/*
 * [设备树覆盖文件] ESP32-S3-DevKitC-1 v1.1 (N32R16V) 专用设备树覆盖
 *
 * 文件名必须是：esp32s3_devkitc_esp32s3_procpu.overlay
 * 存放位置：项目根目录下的 boards/ 子目录
 *
 * 为什么需要这个文件：
 *   Zephyr 自带的 esp32s3_devkitc 板级配置默认使用 esp32s3_wroom_n8.dtsi
 *   (8MB Flash, 无 PSRAM)，而 N32R16V 模组有 32MB Flash + 16MB PSRAM，
 *   必须通过此 overlay 覆盖默认的 Flash/PSRAM 配置。
 */

/*
 * [包含 N32R16 模组定义] 覆盖默认的 N8 模组配置
 * 这个头文件定义了 32MB Flash + 16MB PSRAM 的内存布局
 * 必须放在 /dts-v1/ 声明之后、根节点之前
 */
#include <espressif/esp32s3/esp32s3_wroom_n32r16.dtsi>

/ {
	/*
	 * [设备别名] 为常用外设定义简短别名
	 * 别名的好处：代码中使用 DT_ALIAS(led0) 而非冗长的路径
	 * 如果更换引脚只需修改这里，代码不用改
	 */

	aliases {
		/*
		 * [LED 别名] 将板上 RGB LED 映射为 led0
		 * 注意：RGB LED 连接在 GPIO38 (v1.1)，使用 WS2812 驱动
		 */
		led0 = &rgb_led;
	};

	/*
	 * [自定义设备节点] 在根节点下定义项目所需的硬件设备
	 */
	leds {
		compatible = "gpio-leds";

		/*
		 * [RGB LED 节点] 使用 WS2812 兼容的 LED Strip 驱动
		 * 标签: rgb_led, 可通过 &rgb_led 引用
		 */
		rgb_led: led_0 {
			/*
			 * [引脚配置] GPIO38 连接至 WS2812 数据输入
			 * GPIO_ACTIVE_HIGH: WS2812 数据信号为高电平有效
			 */
			gpios = <&gpio0 38 GPIO_ACTIVE_HIGH>;
			label = "RGB LED";
		};
	};
};

/*
 * [UART0 配置] 覆盖默认的串口配置
 * UART0 通过 USB 转 UART 桥接器输出
 * TX = GPIO43, RX = GPIO44
 */
&uart0 {
	status = "okay";
	current-speed = <115200>;
	/* pinctrl 配置继承自板级默认值，若不满意可在此覆盖 */
};

/*
 * [GPIO 启用] 确保 gpio0 和 gpio1 控制器已启用
 * gpio0: GPIO0 ~ GPIO31
 * gpio1: GPIO32 ~ GPIO48
 */
&gpio0 {
	status = "okay";
};

&gpio1 {
	status = "okay";
};

/*
 * [I2C0 配置] 启用 I2C0 总线
 * SDA = GPIO1, SCL = GPIO2
 */
&i2c0 {
	status = "okay";
	clock-frequency = <I2C_BITRATE_STANDARD>;
};

/*
 * [SPI2 配置] 启用 SPI2 总线 (FSPI 控制器)
 * MOSI = GPIO11, MISO = GPIO13, SCLK = GPIO12, CS = GPIO10
 * 注意：GPIO35/36/37 不可用，不要将 SPI 引脚分配到这些位置
 * 注意：GPIO38 被 RGB LED 占用，如果还需要 SPI 的 CS 信号，请改用其他引脚
 */
&spi2 {
	status = "okay";
	#address-cells = <1>;
	#size-cells = <0>;
};

/* [Wi-Fi] 启用 WiFi 子系统 */
&wifi {
	status = "okay";
};

/* [USB OTG] 启用原生 USB OTG 接口 */
&usb_otg {
	status = "okay";
};
```

### pinctrl 自定义示例

如果需要自定义引脚复用配置，在 overlay 中添加 pinctrl 节点：

```dts
/*
 * [自定义 pinctrl] 覆盖默认的引脚复用配置
 * 这里以自定义一个额外的 UART1 引脚配置为例
 */
&pinctrl {

	/*
	 * [UART1 自定义引脚配置]
	 * 将 UART1 的 TX 和 RX 映射到自定义引脚
	 * 示例使用 GPIO17(TX) 和 GPIO18(RX)
	 *
	 * 为什么用 pinctrl：pinctrl 子系统集中管理所有引脚的复用功能，
	 * 比手动调用 gpio_pin_configure() 更清晰、更易维护
	 */
	my_uart1_custom: my_uart1_custom {
		group1 {
			/*
			 * [TX 引脚组] 配置 UART1_TX 映射到 GPIO17
			 * UART1_TX_GPIO17 是 Zephyr 预定义的引脚复用宏
			 * output-high: TX 空闲时为高电平 (UART 协议要求)
			 */
			pinmux = <UART1_TX_GPIO17>;
			output-high;
		};

		group2 {
			/*
			 * [RX 引脚组] 配置 UART1_RX 映射到 GPIO18
			 * bias-pull-up: 上拉电阻，防止 RX 悬空时收到噪声数据
			 */
			pinmux = <UART1_RX_GPIO18>;
			bias-pull-up;
		};
	};
};

/*
 * [UART1 节点] 引用自定义 pinctrl 配置
 */
&uart1 {
	status = "okay";
	current-speed = <9600>;
	/*
	 * pinctrl-0: 引用上面定义的 my_uart1_custom 引脚配置
	 * pinctrl-names = "default": 使用默认状态(非休眠)配置
	 */
	pinctrl-0 = <&my_uart1_custom>;
	pinctrl-names = "default";
};
```

---

## src/main.c — 主程序文件

新建项目时，使用以下作为起始模板。根据用户需求填充实际业务逻辑。

```c
/*
 * [项目名称] 你的项目名称
 *
 * 硬件平台：ESP32-S3-DevKitC-1 v1.1 (ESP32-S3-WROOM-2-N32R16V)
 * RTOS:     Zephyr RTOS
 * 功能描述：在此填写项目功能
 *
 * 硬件连接：
 *   - Boot 按钮:  GPIO0  (低电平触发)
 *   - RGB LED:     GPIO38 (WS2812, 板载)
 *   - 串口:        GPIO43 (TX) / GPIO44 (RX)
 *
 * 注意事项：
 *   - GPIO35/36/37 不可外部使用 (Octal SPI Flash/PSRAM 占用)
 *   - GPIO38 被板载 RGB LED 占用，若外部使用需先禁用 LED 节点
 *   - 使用 N32R16V 模组，需确保 overlay 包含 esp32s3_wroom_n32r16.dtsi
 */

/* ==================== 头文件包含 ==================== */

/* [内核头文件] Zephyr 内核 API：k_sleep(), k_msleep(), 线程创建等 */
#include <zephyr/kernel.h>

/* [设备驱动头文件] GPIO 驱动 API：gpio_pin_configure(), gpio_pin_set() 等 */
#include <zephyr/drivers/gpio.h>

/* [设备树头文件] 设备树宏定义：DT_NODELABEL, GPIO_DT_SPEC_GET 等 */
#include <zephyr/devicetree.h>

/* [系统打印] 提供 printk() 函数，用于串口输出调试信息 */
#include <zephyr/sys/printk.h>

/* ==================== 宏定义 ==================== */

/*
 * [休眠时间] 定义主循环的休眠间隔，单位毫秒
 * 1000ms = 1秒，可根据实际需求调整
 */
#define SLEEP_TIME_MS 1000

/* ==================== 设备获取 ==================== */

/*
 * [GPIO0 设备获取] 通过设备树节点标签获取 gpio0 控制器设备指针
 *
 * DEVICE_DT_GET: 编译时获取设备指针，比运行时查找更高效
 * DT_NODELABEL(gpio0): 获取设备树中标签为 "gpio0" 的节点
 * gpio0 控制器管理 GPIO0 ~ GPIO31 共 32 个引脚
 */
static const struct device *const gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

/* ==================== 主函数 ==================== */

/*
 * [入口函数] Zephyr 应用的入口点
 * Zephyr 内核初始化完成后自动调用 main() 函数
 * 返回值: 0 表示正常结束，非 0 表示异常 (通常不需要 return)
 */
int main(void)
{
	/*
	 * [启动信息] 打印项目标识和板子信息
	 * CONFIG_BOARD_TARGET 是编译时自动定义的宏，值为 "esp32s3_devkitc"
	 */
	printk("\n");
	printk("========================================\n");
	printk("  ESP32-S3-DevKitC-1 v1.1 N32R16V\n");
	printk("  Zephyr RTOS 项目启动\n");
	printk("  板子: %s\n", CONFIG_BOARD_TARGET);
	printk("========================================\n");
	printk("\n");

	/*
	 * [设备就绪检查] 验证 gpio0 控制器是否已初始化
	 * device_is_ready() 返回 true 表示设备可用
	 * 每个外设使用前都必须检查，未就绪就继续运行会导致崩溃
	 */
	if (!device_is_ready(gpio0_dev)) {
		/* [错误处理] 设备未就绪时打印错误信息并终止 */
		printk("!!! 错误: gpio0 控制器初始化失败，程序终止\n");
		return -1;
	}
	printk("[初始化] gpio0 控制器就绪\n");

	/* ==================== 主循环 ==================== */

	/*
	 * [无限循环] Zephyr RTOS 的标准运行模式
	 * 在 RTOS 中，主循环通常负责低优先级任务
	 * 高优先级任务应使用独立线程处理
	 */
	while (1) {
		printk("[运行] 主循环运行中... (系统运行时间: %u ms)\n",
		       k_uptime_get_32());
		/*
		 * [休眠] 让出 CPU 给其他线程
		 * k_msleep() 是阻塞调用，调用期间 CPU 可运行其他任务
		 * 不使用 busy-wait (忙等)，以节省功耗
		 */
		k_msleep(SLEEP_TIME_MS);
	}

	/* 程序正常情况下不会执行到此处 */
	return 0;
}
```
