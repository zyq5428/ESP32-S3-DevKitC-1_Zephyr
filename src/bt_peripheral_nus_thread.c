/*
 * 这里的 NUS (Nordic UART Service) 是一个自定义服务，
 * 用于在蓝牙连接上模拟串口通信（RX/TX）。
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/services/nus.h> // 核心：引入 NUS 服务头文件
#include "led_control.h"

#define DEVICE_NAME		CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN		(sizeof(DEVICE_NAME) - 1)

/* 定义并初始化全局 LED 模式，默认开机为绿色呼吸灯 */
volatile led_mode_t g_led_mode = LED_MODE_BREATHE_GREEN;

/* 广播数据：设备启动后，手机扫描到时看到的信息 */
static const struct bt_data ad[] = {
	/* 设置为通用可发现模式，不支持经典蓝牙 */
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	/* 设置广播中的设备名称 */
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* 扫描响应数据：当手机请求更多信息时，设备回复的内容 */
static const struct bt_data sd[] = {
	/* 告知手机：本设备支持 NUS 服务（UUID 为 128 位） */
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

/* 回调函数：当手机端开启或关闭“通知（Notification）”时触发 */
static void notif_enabled(bool enabled, void *ctx)
{
	ARG_UNUSED(ctx);
	// 如果 enabled 为 true，说明手机现在可以接收来自板子的数据了
	printk("%s() - %s\n", __func__, (enabled ? "Enabled" : "Disabled"));
}

/* 回调函数：当手机通过蓝牙发送数据给板子时，会调用此函数（相当于串口 RX） */
static void received(struct bt_conn *conn, const void *data, uint16_t len, void *ctx)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(ctx);

	/* 打印接收到的数据长度和内容 */
	printk("%s() - Len: %d, Message: %.*s\n", __func__, len, len, (char *)data);

	/* 【新逻辑】解析手机发送过来的控制指令 */
	if (len > 0) {
		// 取出接收到的第一个字节（字符）
		char cmd = ((char *)data)[0]; 

		// 根据不同的字符，修改全局的 LED 模式变量
		switch (cmd) {
			case 'R':
			case 'r':
				g_led_mode = LED_MODE_BREATHE_RED;
				printk("蓝牙指令：切换为 [红色] 呼吸灯\n");
				break;
			case 'G':
			case 'g':
				g_led_mode = LED_MODE_BREATHE_GREEN;
				printk("蓝牙指令：切换为 [绿色] 呼吸灯\n");
				break;
			case 'B':
			case 'b':
				g_led_mode = LED_MODE_BREATHE_BLUE;
				printk("蓝牙指令：切换为 [蓝色] 呼吸灯\n");
				break;
			case '0':
				g_led_mode = LED_MODE_OFF;
				printk("蓝牙指令：[关闭] LED 灯\n");
				break;
			default:
				printk("未知的蓝牙指令: %c\n", cmd);
				break;
		}
	}
}

/* 将上面定义的两个回调函数注册到 NUS 结构体中 */
struct bt_nus_cb nus_listener = {
	.notif_enabled = notif_enabled,
	.received = received,
};

/**
 * @brief 蓝牙线程入口函数
 * 
 * @param p1, p2, p3 线程参数（由 K_THREAD_DEFINE 传入，此处未使用）
 */
void bt_thread_entry(void *p1, void *p2, void *p3)
{
	int err;

	printk("Sample - Bluetooth Peripheral NUS\n");

	/* 1. 注册 NUS 回调：告诉系统，如果收到数据或连接状态改变，找谁处理 */
	err = bt_nus_cb_register(&nus_listener, NULL);
	if (err) {
		printk("Failed to register NUS callback: %d\n", err);
		return ;
	}

	/* 2. 初始化蓝牙控制器和协议栈 */
	err = bt_enable(NULL);
	if (err) {
		printk("Failed to enable bluetooth: %d\n", err);
		return ;
	}

	/* 3. 开始广播
	 * BT_LE_ADV_CONN_FAST_1：使用较快的广播频率，方便手机快速发现
	 * ad: 广播数据；sd: 扫描响应数据
	 */
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Failed to start advertising: %d\n", err);
		return ;
	}

	printk("Initialization complete\n");

	/* 4. 主循环：每隔 3 秒向手机发送一次 "Hello World!" (相当于串口 TX) */
	while (true) {
		const char *hello_world = "Hello World!\n";

		k_sleep(K_SECONDS(3));

		/* 发送数据到手机
		 * 第一个参数为 NULL 表示发送给所有已连接的客户端
		 */
		err = bt_nus_send(NULL, hello_world, strlen(hello_world));

		/* 错误处理：如果是缓冲区满(-EAGAIN)或未连接(-ENOTCONN)，通常忽略 */
		if (err < 0 && (err != -EAGAIN) && (err != -ENOTCONN)) {
			return ;
		} else if (err == 0) {
            printk("Data send - Result: %d\n", err);
        }
	}

	return ;
}

/* 线程配置参数 */
#define BT_STACK_SIZE 4096  // 线程栈大小（单位：字节）
#define BT_PRIORITY 11      // 线程优先级（数字越大优先级越低）

/**
 * @brief 定义并自动启动线程
 * 
 * 参数依次为：线程 ID, 栈大小, 入口函数, 参数1, 参数2, 参数3, 优先级, 选项, 启动延迟
 */
K_THREAD_DEFINE(bluetooth_tid, BT_STACK_SIZE, 
                bt_thread_entry, NULL, NULL, NULL,
                BT_PRIORITY, 0, 0);