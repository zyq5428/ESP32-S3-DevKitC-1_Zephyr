/*
 * [LCD + LVGL 线程] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 * - 初始化 ST7789V (240x280) SPI LCD 显示屏
 * - 使用 Zephyr 内置 LVGL 图形库渲染界面 (兼容 LVGL 9)
 * - 显示文本标签和嵌入图像
 * - 通过全局变量 g_lcd_brightness 支持亮度控制
 * - 5秒后安全切换至官方 Widgets 演示程序
 *
 * 硬件连接:
 * - GPIO12 (SCLK) → LCD SCL
 * - GPIO11 (MOSI) → LCD SDA
 * - GPIO10 (CS)   → LCD CS
 * - GPIO8  (DC)   → LCD DC
 * - GPIO9  (RST)  → LCD RST
 * - GPIO4  (BLK)  → LCD BLK (PWM 背光)
 *
 * 注意事项:
 * - 在 LVGL 9 中，严禁对活跃屏幕直接调用 lv_obj_clean()，否则会破坏内核定时器导致崩溃。
 * - 本标准代码采用“创建新屏 → 载入新屏 → 销毁旧屏”的规范安全切屏。
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
#include "../demos/lv_demos.h"      /* [LVGL Demo] lv_demo_widgets() 等官方 Demo 入口 */

/* ==================== 项目头文件 ==================== */
#include "lcd_lvgl_thread.h"        /* [头文件] 全局变量声明 */
#include "images/zephyr_200x116.h"  /* [头文件] Zephyr Logo 图像描述符 */

/* ==================== 日志模块注册 ==================== */
LOG_MODULE_REGISTER(LCD_LVGL, LOG_LEVEL_INF);

/* ==================== 全局变量定义 ==================== */
volatile int g_lcd_brightness = 30; /* [初始化] 默认背光亮度 30% */
volatile bool g_lvgl_ready = false; /* [初始化] LVGL 尚未就绪 */

/* ==================== LVGL 9 多屏幕管理指针 ==================== */
static lv_obj_t *info_screen = NULL; /* [指针] 指向开机信息屏幕对象，方便后续安全释放 */

/* ==================== PWM 背光设备获取 ==================== */
static const struct pwm_dt_spec pwm_backlight =
    PWM_DT_SPEC_GET(DT_ALIAS(pwm_backlight));

/* ==================== 背光控制函数 ==================== */
static void lcd_backlight_set(uint8_t brightness)
{
    int ret;
    uint32_t pulse_ns;    /* [变量] PWM 高电平持续时间 (纳秒) */

    if (!pwm_is_ready_dt(&pwm_backlight)) {
        return;
    }

    if (brightness > 100) {
        brightness = 100;
    }

    /* 使用 64 位中间计算防止乘法溢出 */
    pulse_ns = (uint32_t)(((uint64_t)brightness *
                           (uint64_t)pwm_backlight.period) / 100U);

    ret = pwm_set_pulse_dt(&pwm_backlight, pulse_ns);
    if (ret < 0) {
        LOG_ERR("Failed to set backlight PWM (err %d)", ret);
    }
}

/* ==================== LVGL 界面构建函数 ==================== */

/*
 * [函数] 创建开发板信息界面 (升级为 LVGL 9 规范独立屏幕模式)
 */
static void lvgl_create_info_screen(void)
{
    lv_obj_t *logo_img;       /* [LVGL 对象] Zephyr Logo 图像 */
    lv_obj_t *title_label;    /* [LVGL 对象] 标题标签 */
    lv_obj_t *info_label;     /* [LVGL 对象] 信息标签 */
    lv_obj_t *status_label;   /* [LVGL 对象] 状态标签 */

    /* * 【LVGL 9 核心修改】
     * 不再直接使用 lv_screen_active()，而是使用 lv_obj_create(NULL) 创建一个全新的空屏幕。
     * 这样做可以形成一个完全独立的 UI 图层，后续切屏时可以直接整体销毁，绝不污染系统内核。
     */
    info_screen = lv_obj_create(NULL);

    /* ---------- 1. 屏幕背景 (全部挂载到新的 info_screen 上) ---------- */
    lv_obj_set_style_bg_color(info_screen,
                              lv_color_hex(0x1a237e),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(info_screen,
                            LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ---------- 2. Zephyr Logo ---------- */
    logo_img = lv_image_create(info_screen);
    lv_image_set_src(logo_img, &zephyr_200x116);
    lv_obj_align(logo_img, LV_ALIGN_TOP_MID, 0, 20);

    /* ---------- 3. 标题 ---------- */
    title_label = lv_label_create(info_screen);
    lv_label_set_text(title_label, "Zephyr RTOS + LVGL");
    lv_obj_set_style_text_color(title_label,
                                lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label,
                               &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 150);

    /* ---------- 4. 信息标签 (底部) ---------- */
    info_label = lv_label_create(info_screen);
    lv_label_set_text(info_label,
                      "ESP32-S3 DevKitC\n"
                      "ST7789  240x280\n"
                      "LCD PWM Backlight");
    lv_obj_set_style_text_color(info_label,
                                lv_color_hex(0xb0bec5),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(info_label,
                               &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(info_label,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(info_label, LV_ALIGN_BOTTOM_MID, 0, -65);

    /* ---------- 5. 状态标签 ---------- */
    status_label = lv_label_create(info_screen);
    lv_label_set_text(status_label, "System Ready");
    lv_obj_set_style_text_color(status_label,
                                lv_color_hex(0x4caf50),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(status_label,
                               &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -15);

    /* 【核心操作】让显示屏正式加载并显示这个刚刚做好的信息屏幕 */
    lv_screen_load(info_screen);

    LOG_INF("LVGL UI with Zephyr logo created successfully");
}

/* ==================== LVGL 界面清理与切屏函数 ==================== */

/*
 * [函数] 安全切换屏幕并销毁旧界面 (彻底解决 LVGL 9 闪退 Bug)
 * * 核心逻辑：
 * 1. 创建一个供 Widgets 演示程序运行的全新空白屏幕。
 * 2. 执行屏幕载入，让硬件刷新新屏幕。
 * 3. 此时旧屏幕(info_screen)脱离活跃状态，调用 lv_obj_delete() 安全释放它及其所有子控件占用的全部内存。
 */
static void lvgl_clear_screen(void)
{
    /* 1. 创建一个全新的空白屏幕作为 Demo 的舞台 */
    lv_obj_t *new_demo_screen = lv_obj_create(NULL);
    
    /* 2. 将这个新屏幕的背景设置为全黑 */
    lv_obj_set_style_bg_color(new_demo_screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(new_demo_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    /* 3. 命令 LVGL 引擎加载并切换到这个全新黑屏 */
    lv_screen_load(new_demo_screen);

    /* * 4. 关键安全步骤：由于新屏幕已经上任，旧的 info_screen 已经安全卸载。
     * 我们调用 lv_obj_delete 彻底将其从内存中抹除，回收图片和文字占用的空间。
     * 这种做法百分之百不会误伤 LVGL 9 系统底层的任何全局定时器和样式动画。
     */
    if (info_screen != NULL) {
        lv_obj_delete(info_screen);
        info_screen = NULL; /* 指针清空防止野指针访问 */
    }

    LOG_INF("Screen cleared safely, ready for Widgets Demo");
}

/* ==================== 显示设备初始化函数 ==================== */
static const struct device *display_init(void)
{
    const struct device *display_dev;
    int ret;

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device '%s' is not ready! ",
                display_dev ? display_dev->name : "NULL");
        return NULL;
    }
    LOG_INF("Display device '%s' is ready", display_dev->name);

    ret = display_blanking_off(display_dev);
    if (ret < 0 && ret != -ENOSYS) {
        LOG_ERR("Failed to turn off blanking (error %d)", ret);
        return NULL;
    }
    LOG_INF("Display blanking turned off, screen should be lit");

    return display_dev;
}

/* ==================== LVGL 线程入口函数 ==================== */
void lcd_lvgl_thread_entry(void *p1, void *p2, void *p3)
{
    const struct device *display_dev;
    uint32_t loop_count = 0;          /* [计数] 主循环迭代次数 */

    LOG_INF("LCD/LVGL Thread starting...");

    /* 延时 500ms 确保底层总线硬件完全就绪 */
    k_msleep(500);

    /* ---------- 步骤 1: 点亮背光 ---------- */
    lcd_backlight_set(g_lcd_brightness);
    LOG_INF("Backlight set to %d%%", g_lcd_brightness);

    /* ---------- 步骤 2: 初始化显示设备 ---------- */
    display_dev = display_init();
    if (display_dev == NULL) {
        LOG_ERR("Display init failed! LCD thread will exit.");
        return;
    }

    /* ---------- 步骤 3: 等待 LVGL 自动初始化 ---------- */
    if (!lv_is_initialized()) {
        LOG_ERR("LVGL not initialized! Check CONFIG_LV_Z_AUTO_INIT=y");
        return;
    }
    g_lvgl_ready = true; 
    LOG_INF("LVGL initialized and ready");

    /* ---------- 步骤 4: 构建并载入开发板信息界面 ---------- */
    lvgl_create_info_screen();
    LOG_INF("Dev board info screen created");

    /* ---------- 步骤 5: 展示信息界面 (5 秒) ---------- */
    LOG_INF("Showing dev board info for 5 seconds...");
    k_sleep(K_SECONDS(5));

    /* ---------- 步骤 6: 安全切屏并启动 LVGL Widgets Demo ---------- */
    LOG_INF("Launching LVGL Widgets Demo...");
    
    /* 采用符合 LVGL 9 标准的多屏迭代法清除旧界面并回收内存 */
    lvgl_clear_screen();
    
    /* 此时系统内核极其纯净，Demo 会直接在我们刚才创建的干净黑屏上启动，绝不闪退 */
    lv_demo_widgets();
    
    LOG_INF("Widgets Demo launched! Slideshow auto-advances every 8 seconds.");

    /* ---------- 步骤 7: 空闲心跳循环 ---------- */
    while (1) {
        k_sleep(K_SECONDS(30));
        loop_count++;
        LOG_DBG("Display thread alive (%u min), demo running", loop_count / 2);
    }
}

/* ==================== 线程配置与静态定义 ==================== */
#define LCD_LVGL_STACK_SIZE 4096
#define LCD_LVGL_PRIORITY 14

K_THREAD_DEFINE(lcd_lvgl_tid, LCD_LVGL_STACK_SIZE,
                lcd_lvgl_thread_entry, NULL, NULL, NULL,
                LCD_LVGL_PRIORITY, 0, 0);