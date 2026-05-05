#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* 蓝牙核心头文件 */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>


/* 从 Kconfig 配置中获取设备名称（通常在 prj.conf 中定义） */
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* 
 * 自定义厂商数据 (Manufacturer Specific Data)
 * 前两个字节 0xff, 0xff 通常是厂商 ID（这里是测试用的伪 ID）
 * 第三个字节 0x00 是我们要动态改变的数据计数器
 */
static uint8_t mfg_data[] = { 0xff, 0xff, 0x00 };

/* 广播数据数组 */
// static const struct bt_data ad[] = {
//     /* 
//      * 使用 BT_DATA 宏引用上面的 mfg_data 数组
//      * 类型为 BT_DATA_MANUFACTURER_DATA（厂商自定义数据）
//      */
// 	BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, 3),
// };
static const struct bt_data ad[] = {
    /* 1. 标志位：必带，否则手机可能拒收*/
    // BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    /* 2. 你的自定义数据*/
    BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, 3),
    /* 3. 把名字也放进广播包，方便识别*/
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/**
 * @brief 蓝牙线程入口函数
 * 
 * @param p1, p2, p3 线程参数（由 K_THREAD_DEFINE 传入，此处未使用）
 */
void bt_thread_entry(void *p1, void *p2, void *p3)
{
	int err;

	printk("正在启动广播者 (Broadcaster)...\n");

	/* 
     * 初始化蓝牙子系统
     * 传入 NULL 表示【同步初始化】：程序会阻塞在这里，直到蓝牙硬件准备就绪
     */
	err = bt_enable(NULL);
	if (err) {
		printk("蓝牙初始化失败 (错误码 %d)\n", err);
		return;
	}

	printk("蓝牙初始化成功\n");

	/* 1. 先启动广播，不要在启动前睡觉 */
	err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Start failed %d\n", err);
		return;
	}

	/* 2. 然后再进入循环只更新数据，不要停掉广播 */
	while (1) {
		k_msleep(1000);
		mfg_data[2]++;
		// 使用专门的更新接口，比先停再开更稳定
		bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0); 
		printk("Updated data: 0x%02X\n", mfg_data[2]);
	}

	/* 进入应用主循环 */
	// do {
	// 	/* 等待 1000 毫秒 */
	// 	k_msleep(1000);

	// 	printk("正在发送广播数据: 0x%02X\n", mfg_data[2]);

	// 	/* 
    //      * 启动广播
    //      * BT_LE_ADV_NCONN: 不可连接广播
    //      * ad: 包含我们的 mfg_data
    //      * NULL, 0: 此处没有设置扫描响应数据
    //      */
	// 	err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad),
	// 			      NULL, 0);
	// 	if (err) {
	// 		printk("广播启动失败 (错误码 %d)\n", err);
	// 		return;
	// 	}

	// 	/* 广播持续 1000 毫秒 */
	// 	k_msleep(1000);

	// 	/* 停止广播：关闭射频发射，节省电量 */
	// 	err = bt_le_adv_stop();
	// 	if (err) {
	// 		printk("广播停止失败 (错误码 %d)\n", err);
	// 		return;
	// 	}

	// 	/* 
    //      * 核心逻辑：修改数据
    //      * 直接修改引用的 mfg_data 数组内容
    //      * 下次调用 bt_le_adv_start 时，发出的就是更新后的值
    //      */
	// 	mfg_data[2]++;

	// } while (1); // 死循环，不停地 开启-等待-关闭-修改-循环
    
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