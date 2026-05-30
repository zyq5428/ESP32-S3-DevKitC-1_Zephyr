/*
 * [LCD + LVGL 线程] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - 初始化 ST7789V (240x280) SPI LCD 显示屏
 *   - 使用 Zephyr 内置 LVGL 图形库渲染界面
 *   - 显示文本标签和嵌入图像
 *   - 通过全局变量 g_lcd_brightness 支持亮度控制
 *
 * 硬件连接:
 *   - GPIO12 (SCLK) → LCD SCL
 *   - GPIO11 (MOSI) → LCD SDA
 *   - GPIO10 (CS)   → LCD CS
 *   - GPIO8  (DC)   → LCD DC
 *   - GPIO9  (RST)  → LCD RST
 *   - GPIO4  (BLK)  → LCD BLK (PWM 背光)
 *   - 3V3 / 5V       → LCD VCC
 *   - GND             → LCD GND
 *
 * 工作流程:
 *   1. 系统启动时 K_THREAD_DEFINE 自动创建线程
 *   2. 通过 DT_CHOSEN(zephyr_display) 获取设备树中的显示设备
 *   3. 关闭 blanking (打开屏幕)
 *   4. 创建 LVGL 界面元素 (背景、标签、图像)
 *   5. 主循环: 每 10ms 调用 lv_timer_handler() 刷新界面
 *
 * 注意事项:
 *   - LVGL 由 CONFIG_LV_Z_AUTO_INIT=y 自动初始化，无需手动调用 lv_init()
 *   - lv_timer_handler() 必须被周期性调用 (建议 5~15ms)
 *   - MIPI DBI SPI 使用 write-only 模式，驱动自动处理 CS/DC
 *   - 图像数据较大时建议放在 PSRAM 中 (通过 shared_multi_heap 分配)
 */

/* ==================== Zephyr 头文件 ==================== */
#include <zephyr/kernel.h>          /* [内核] K_THREAD_DEFINE, k_sleep, k_msleep */
#include <zephyr/device.h>          /* [设备] device_is_ready, DEVICE_DT_GET */
#include <zephyr/devicetree.h>      /* [设备树] DT_CHOSEN, DT_NODELABEL */
#include <zephyr/drivers/display.h> /* [显示] display_blanking_off, display_write */
#include <zephyr/drivers/gpio.h>    /* [GPIO] 背光控制 */
#include <zephyr/drivers/pwm.h>     /* [PWM] 背光 PWM 调光 */
#include <zephyr/sys/printk.h>      /* [打印] printk */
#include <zephyr/logging/log.h>     /* [日志] LOG_MODULE_REGISTER, LOG_INF */

/* ==================== LVGL 头文件 ==================== */
#include <lvgl.h>                   /* [LVGL] lv_* 图形库 API */

/* ==================== 项目头文件 ==================== */
#include "lcd_lvgl_thread.h"        /* [头文件] 全局变量声明 */

/* ==================== 日志模块注册 ==================== */
LOG_MODULE_REGISTER(LCD_LVGL, LOG_LEVEL_INF);

/* ==================== 全局变量定义 ==================== */
volatile int g_lcd_brightness = 80; /* [初始化] 默认背光亮度 80% */
volatile bool g_lvgl_ready = false; /* [初始化] LVGL 尚未就绪 */

/* ==================== 背光控制函数 ==================== */

/*
 * [函数] 设置 LCD 背光亮度
 *
 * 通过 LEDC PWM 控制 GPIO4 输出占空比，实现无级调光。
 * 如果不需要调光，也可以直接用 GPIO 高低电平开关背光。
 *
 * 参数 brightness: 0~100，表示亮度百分比
 *   - 0: 关闭背光 (屏幕全黑)
 *   - 100: 最大亮度
 */
static void lcd_backlight_set(uint8_t brightness)
{
    /*
     * [简化方案] 直接用 GPIO 开/关背光
     * 如果需要 PWM 无级调光，可以用 overlay 配置 LEDC 通道，
     * 然后在这里调用 pwm_set_pulse_dt() 调节占空比。
     *
     * 硬件: GPIO4 → LCD BLK 引脚 (高电平点亮)
     */
    const struct device *const gpio0_dev =
        DEVICE_DT_GET(DT_NODELABEL(gpio0));
    static bool backlight_inited = false;

    if (!device_is_ready(gpio0_dev)) {
        return;
    }

    if (!backlight_inited) {
        /*
         * [初始化] 首次调用时配置 GPIO4 为输出
         * 低电平初始状态 (先灭后亮，避免闪烁)
         */
        gpio_pin_configure(gpio0_dev, 4, GPIO_OUTPUT_INACTIVE);
        backlight_inited = true;
    }

    /*
     * [设置] 根据亮度值控制 GPIO
     * brightness > 0 则点亮，= 0 则熄灭
     * 简单二值控制，如果需要 PWM 无级调光可以替换为 LEDC
     */
    if (brightness > 0) {
        gpio_pin_set_dt(&(struct gpio_dt_spec){
            .port = gpio0_dev, .pin = 4}, 1);
    } else {
        gpio_pin_set_dt(&(struct gpio_dt_spec){
            .port = gpio0_dev, .pin = 4}, 0);
    }
}

/* ==================== LVGL 界面构建函数 ==================== */

/*
 * [函数] 创建 LVGL 主界面
 *
 * 界面布局 (从外到内):
 *   1. 屏幕背景 — 深蓝色 (体现 Zephyr 品牌色)
 *   2. 标题标签 — "Zephyr LVGL" (顶部居中)
 *   3. 信息标签 — "ESP32-S3 + ST7789" (中部)
 *   4. 状态标签 — 显示系统运行时间 (底部)
 */
static void lvgl_create_ui(void)
{
    lv_obj_t *title_label;    /* [LVGL 对象] 标题标签 */
    lv_obj_t *info_label;     /* [LVGL 对象] 信息标签 */
    lv_obj_t *status_label;   /* [LVGL 对象] 状态标签 */

    /*
     * [获取] 获取当前活动屏幕对象
     * lv_screen_active() 返回 LVGL 默认创建的屏幕
     * 所有界面元素都是这个屏幕的子对象
     */
    lv_obj_t *screen = lv_screen_active();

    /* ---------- 1. 设置屏幕背景色 ---------- */
    /*
     * [背景色] 设置为深蓝色 (#1a237e)
     * lv_color_hex() 将 HTML 颜色码转换为 LVGL 颜色结构体
     * lv_obj_set_style_bg_color 设置背景色
     * lv_obj_set_style_bg_opa 设置背景不透明度 (LV_OPA_COVER = 完全不透明)
     */
    lv_obj_set_style_bg_color(screen,
                              lv_color_hex(0x1a237e),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen,
                            LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ---------- 2. 创建标题标签 ---------- */
    /*
     * [创建] lv_label_create() 在屏幕对象上创建文本标签
     * lv_label_set_text() 设置标签显示的文本内容
     * lv_obj_set_style_text_color() 设置文字颜色为白色
     * lv_obj_align() 将标签对齐到屏幕顶部中间
     */
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "Zephyr LVGL");
    lv_obj_set_style_text_color(title_label,
                                lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    /*
     * [字体大小] lv_obj_set_style_text_font 设置字号
     * &lv_font_montserrat_14 是 14 号 Montserrat 字体
     * 这是在 prj.conf 中通过 CONFIG_LV_FONT_MONTSERRAT_14=y 启用的
     */
    lv_obj_set_style_text_font(title_label,
                               &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 15);

    /* ---------- 3. 创建信息标签 ---------- */
    info_label = lv_label_create(screen);
    lv_label_set_text(info_label,
                      "ESP32-S3 DevKitC\n"
                      "ST7789 240x280\n"
                      "Zephyr RTOS + LVGL");
    lv_obj_set_style_text_color(info_label,
                                lv_color_hex(0xb0bec5),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(info_label,
                               &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    /*
     * [文本对齐] LV_TEXT_ALIGN_CENTER 让多行文本每行居中显示
     */
    lv_obj_set_style_text_align(info_label,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(info_label, LV_ALIGN_CENTER, 0, -10);

    /* ---------- 4. 创建状态标签 ---------- */
    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "System Ready");
    lv_obj_set_style_text_color(status_label,
                                lv_color_hex(0x4caf50),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(status_label,
                               &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -15);

    LOG_INF("LVGL UI created successfully");
}

/* ==================== 显示设备初始化函数 ==================== */

/*
 * [函数] 初始化显示设备
 *
 * 流程:
 *   1. 从设备树 chosen 节点获取 zephyr,display 指定的设备
 *   2. 检查设备驱动是否就绪
 *   3. 关闭显示 blanking (即点亮屏幕)
 *
 * 返回值: 显示设备指针，失败返回 NULL
 */
static const struct device *display_init(void)
{
    const struct device *display_dev;
    int ret;

    /*
     * [获取设备] DT_CHOSEN(zephyr_display) 返回我们在 overlay 中
     * chosen { zephyr,display = &st7789v; } 指定的设备节点
     * DEVICE_DT_GET 根据设备树节点获取设备指针
     */
    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    /* [就绪检查] 验证 ST7789V 驱动是否初始化成功 */
    if (!device_is_ready(display_dev)) {
        /*
         * [错误] 如果设备未就绪，说明:
         *   1. overlay 中的 st7789v 节点未正确设置
         *   2. SPI 总线或 MIPI DBI 驱动未正确启用
         *   3. 硬件接线有误 (驱动初始化时会复位显示)
         */
        LOG_ERR("Display device '%s' is not ready! "
                "Check overlay, wiring, and power.",
                display_dev ? display_dev->name : "NULL");
        return NULL;
    }
    LOG_INF("Display device '%s' is ready", display_dev->name);

    /*
     * [打开显示] display_blanking_off() 解除显示睡眠状态
     *
     * ST7789V 驱动在初始化后会进入 blanking-on (灭屏) 状态
     * 调用此函数会发送命令序列:
     *   1. SLEEP_OUT (0x11) — 退出睡眠模式 (约 120ms)
     *   2. DISP_ON   (0x29) — 打开显示输出
     *
     * 返回值: 0 = 成功, -ENOSYS = 驱动不支持 (不算错误)
     */
    ret = display_blanking_off(display_dev);
    if (ret < 0 && ret != -ENOSYS) {
        LOG_ERR("Failed to turn off blanking (error %d)", ret);
        return NULL;
    }
    LOG_INF("Display blanking turned off, screen should be lit");

    return display_dev;
}

/* ==================== LVGL 线程入口函数 ==================== */

/**
 * @brief LCD/LVGL 线程入口函数
 *
 * 此线程由 K_THREAD_DEFINE 在系统启动时自动创建并执行。
 * 负责: 显示初始化 → 背光点亮 → UI 构建 → 周期性 LVGL 刷新
 *
 * @param p1, p2, p3 线程参数 (未使用)
 */
void lcd_lvgl_thread_entry(void *p1, void *p2, void *p3)
{
    const struct device *display_dev;
    uint32_t loop_count = 0;          /* [计数] 主循环迭代次数 */

    LOG_INF("LCD/LVGL Thread starting...");

    /*
     * [延时] 等待 500ms，让 SPI、MIPI DBI 驱动等其他组件先完成初始化
     * 这个延时很关键: 如果太早访问显示设备，SPI 总线可能还没就绪
     */
    k_msleep(500);

    /* ---------- 步骤 1: 点亮背光 ---------- */
    /*
     * [背光] 先给背光上电，这样后续初始化过程中就能看到画面
     * 有些 LCD 模块背光接在板载 LDO 上，不需要额外控制
     */
    lcd_backlight_set(80);
    LOG_INF("Backlight set to 80%%");

    /* ---------- 步骤 2: 初始化显示设备 ---------- */
    display_dev = display_init();
    if (display_dev == NULL) {
        /*
         * [严重错误] 显示初始化失败
         * 如果到这里失败了，检查:
         *   1. LCD 的 VCC 和 GND 是否正确连接?
         *   2. SPI 线 (SCLK/MOSI/CS/DC/RST) 是否接对?
         *   3. overlay 中的引脚配置是否与实物一致?
         */
        LOG_ERR("Display init failed! LCD thread will exit.");
        return;
    }

    /* ---------- 步骤 3: 等待 LVGL 自动初始化 ---------- */
    /*
     * [等待] CONFIG_LV_Z_AUTO_INIT=y 会在 POST_KERNEL 阶段自动调用 lv_init()
     * 但我们还是做一个安全检查: 验证 LVGL 核心是否已初始化
     * lv_is_initialized() 返回 true 表示 LVGL 已准备就绪
     */
    if (!lv_is_initialized()) {
        LOG_ERR("LVGL not initialized! Check CONFIG_LV_Z_AUTO_INIT=y");
        return;
    }
    g_lvgl_ready = true; /* [标志] 通知其他线程 LVGL 已就绪 */
    LOG_INF("LVGL initialized and ready");

    /* ---------- 步骤 4: 构建用户界面 ---------- */
    lvgl_create_ui();
    LOG_INF("UI created, entering main loop...");

    /* ---------- 步骤 5: 主循环 — 周期性刷新 LVGL ---------- */
    /*
     * [主循环] 每隔 10ms 调用一次 lv_timer_handler()
     *
     * lv_timer_handler() 是 LVGL 的核心引擎，它:
     *   1. 处理 LVGL 内部定时器 (动画、倒计时等)
     *   2. 检查脏区 (dirty area)，对需要更新的区域重新渲染
     *   3. 调用 Zephyr 的 flush 回调函数，将像素数据发送到 LCD
     *
     * 调用间隔:
     *   - 10ms (100Hz): 流畅 UI，适合有动画的场景
     *   - 15ms (~66Hz): 一般 UI，可节省 CPU
     *   - 30ms (~33Hz): 静态 UI，CPU 占用最低
     *
     * 注意: lv_timer_handler() 只刷新有变化的部分 (partial refresh)
     *       不必担心它消耗过多 CPU
     */
    while (1) {
        /*
         * [刷新] 调用 LVGL 渲染引擎
         * 这个函数的耗时取决于 UI 变化量:
         *   - 静态界面: <1ms
         *   - 有动画: 几 ms
         *
         * 返回值是 LVGL 处理完到下次需要刷新之间的建议时延 (ms)
         * 我们暂不依赖返回值，固定 10ms 周期更简单可靠
         */
        lv_timer_handler();

        /*
         * [休眠] 让出 CPU 10 毫秒
         * k_msleep() 是阻塞调用，会触发上下文切换
         * 这保证了其他线程 (BLE、LED 等) 有充足的运行时间
         */
        k_msleep(10);

        /*
         * [心跳] 每 6000 次循环 (约 60 秒) 打印一次调试信息
         * 这可以帮助判断 LVGL 线程是否正常运行
         */
        loop_count++;
        if ((loop_count % 6000) == 0) {
            LOG_DBG("LVGL loop alive (%u iterations)", loop_count);
        }
    }
}

/* ==================== 线程配置与定义 ==================== */

/*
 * [栈空间] LVGL 线程栈大小: 4096 字节
 *
 * LVGL 的渲染工作在 lv_timer_handler() 内部完成，
 * 主要的显示缓冲区和内存池从系统堆分配 (不在栈上)，
 * 所以 4096 字节足以满足 LVGL 调用和控件创建的需求。
 * 如果使用深层递归的 LVGL 功能，可能需要增加到 6144。
 */
#define LCD_LVGL_STACK_SIZE 4096

/*
 * [优先级] LVGL 线程优先级: 14
 *
 * 优先级设计考量:
 *   11 - BLE NUS 线程 (蓝牙通信需要低延迟)
 *   12 - 按键线程 (用户输入优先)
 *   13 - 内存测试 (一次性运行即可)
 *   14 - LVGL 显示 ★ (UI 刷新，低于通信和输入)
 *   15 - LED 呼吸灯 (视觉效果，最低优先)
 *
 * 数字越小优先级越高，14 是较低的优先级，
 * 保证 UI 渲染不会影响蓝牙通信和按键响应。
 */
#define LCD_LVGL_PRIORITY 14

/**
 * @brief 使用 K_THREAD_DEFINE 静态定义并自动启动线程
 *
 * 参数依次为:
 *   lcd_lvgl_tid:              线程标识符 (可在 Shell 中查看)
 *   LCD_LVGL_STACK_SIZE:       栈大小 (字节)
 *   lcd_lvgl_thread_entry:     线程入口函数
 *   NULL, NULL, NULL:          传递给入口函数的三个参数 (未使用)
 *   LCD_LVGL_PRIORITY:         线程优先级
 *   0:                         线程选项 (0 = 默认抢占式)
 *   0:                         启动延迟 (0 = 立即启动)
 */
K_THREAD_DEFINE(lcd_lvgl_tid, LCD_LVGL_STACK_SIZE,
                lcd_lvgl_thread_entry, NULL, NULL, NULL,
                LCD_LVGL_PRIORITY, 0, 0);
