#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* 引入 Zephyr 蓝牙核心栈头文件 */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

/* 从 Kconfig 配置中获取设备名称（通常在 prj.conf 中定义） */
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* 
 * 定义广播数据 (Advertising Data, AD)
 * 这里的数据格式遵循 Eddystone 规范
 */
static const struct bt_data ad[] = {
    /* 标志位：LE General Discoverable Mode & 不支持 BR/EDR (经典蓝牙) */
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    
    /* 16位服务 UUID：0xFEAA 代表 Eddystone */
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xaa, 0xfe),
    
    /* 服务数据 (Service Data)：包含实际的 URL 内容 */
	BT_DATA_BYTES(BT_DATA_SVC_DATA16,
		      0xaa, 0xfe, /* Eddystone UUID (重复以符合格式) */
		      0x10,       /* 帧类型：Eddystone-URL */
		      0x00,       /* 0米处的校准发射功率 (TX Power) */
		      0x00,       /* URL 前缀：http://www. */
		      'z', 'e', 'p', 'h', 'y', 'r',
		      'p', 'r', 'o', 'j', 'e', 'c', 't',
		      0x08)       /* 后缀映射：0x08 代表 .org */
};

/* 
 * 定义扫描响应数据 (Scan Response Data, SD)
 * 当手机等主设备主动扫描该 Beacon 时，会返回这段额外信息
 */
static const struct bt_data sd[] = {
    /* 返回完整的设备名称 */
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* 蓝牙初始化完成后的回调函数 */
static void bt_ready(int err)
{
	bt_addr_le_t addr = {0};
	size_t count = 1;

	if (err) {
		printk("蓝牙初始化失败 (错误码 %d)\n", err);
		return;
	}

	printk("蓝牙初始化成功\n");

	/* 
     * 开始广播
     * BT_LE_ADV_NCONN_IDENTITY: 非连接广播（不可连接），使用身份地址
     * ad: 广播包内容
     * sd: 扫描响应包内容
     */
	err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		printk("广播启动失败 (错误码 %d)\n", err);
		return;
	}

	/* 获取并打印当前设备正在使用的蓝牙 MAC 地址 */
	bt_id_get(&addr, &count);
	printk("Beacon 已启动，广播地址为 %s\n", bt_addr_le_str(&addr));
}

/**
 * @brief 蓝牙线程入口函数
 * 
 * @param p1, p2, p3 线程参数（由 K_THREAD_DEFINE 传入，此处未使用）
 */
void bt_thread_entry(void *p1, void *p2, void *p3)
{
	int err;

	printk("正在启动 Beacon 示例程序\n");

	/* 
     * 异步启动蓝牙子系统
     * 初始化完成后会自动调用 bt_ready 回调函数
     */
	err = bt_enable(bt_ready);
	if (err) {
		printk("蓝牙启动失败 (错误码 %d)\n", err);
	}
    
	return;
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