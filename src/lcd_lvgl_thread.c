/*
 * [LCD + LVGL 线程] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 * - 初始化 ST7789V (240x280) SPI LCD 显示屏
 * - 使用 Zephyr 内置 LVGL 9 图形库渲染自定义精美智能手表界面
 * - 采用“单屏幕复用重绘”机制，彻底杜绝多屏切换时的内存释放/野指针崩溃（0x4 / 乱码地址）。
 *
 * 硬件连接:
 * - GPIO12 (SCLK) → LCD SCL
 * - GPIO11 (MOSI) → LCD SDA
 * - GPIO10 (CS)   → LCD CS
 * - GPIO8  (DC)   → LCD DC
 * - GPIO9  (RST)  → LCD RST
 * - GPIO4  (BLK)  → LCD BLK (PWM 背光)
 */

/* ==================== Zephyr 头文件 ==================== */
#include <zephyr/kernel.h>          /* [内核] K_THREAD_DEFINE, k_sleep, k_msleep */
#include <zephyr/device.h>          /* [设备] device_is_ready, DEVICE_DT_GET */
#include <zephyr/devicetree.h>      /* [设备树] DT_CHOSEN, DT_NODELABEL */
#include <zephyr/drivers/display.h> /* [显示] display_blanking_off */
#include <zephyr/drivers/gpio.h>    /* [GPIO] 背光控制 */
#include <zephyr/drivers/pwm.h>     /* [PWM] 背光 PWM 调光 */
#include <zephyr/sys/printk.h>      /* [打印] printk */
#include <zephyr/logging/log.h>     /* [日志] LOG_MODULE_REGISTER, LOG_INF */

/* ==================== LVGL 头文件 ==================== */
#include <lvgl.h>                   /* [LVGL] lv_* 图形库 API */

/* ==================== 项目头文件 ==================== */
#include "lcd_lvgl_thread.h"        /* [头文件] 全局变量声明 */
#include "images/zephyr_200x116.h"  /* [头文件] Zephyr Logo 图像描述符 */

/* ==================== 日志模块注册 ==================== */
LOG_MODULE_REGISTER(LCD_LVGL, LOG_LEVEL_INF);

/* ==================== 全局变量定义 ==================== */
volatile int g_lcd_brightness = 30; /* [初始化] 默认背光亮度 30% */
volatile bool g_lvgl_ready = false; /* [初始化] LVGL 尚未就绪 */

/* ==================== LVGL 独享主显示屏幕对象 ==================== */
static lv_obj_t *app_screen = NULL;  /* [全局容器] 整个生命周期内唯一使用的屏幕画布 */

/* ==================== PWM 背光设备获取 ==================== */
static const struct pwm_dt_spec pwm_backlight =
    PWM_DT_SPEC_GET(DT_ALIAS(pwm_backlight));

/* ==================== 背光控制函数 ==================== */
static void lcd_backlight_set(uint8_t brightness)
{
    int ret;
    uint32_t pulse_ns;

    if (!pwm_is_ready_dt(&pwm_backlight)) {
        return;
    }

    if (brightness > 100) {
        brightness = 100;
    }

    pulse_ns = (uint32_t)(((uint64_t)brightness *
                           (uint64_t)pwm_backlight.period) / 100U);

    ret = pwm_set_pulse_dt(&pwm_backlight, pulse_ns);
    if (ret < 0) {
        LOG_ERR("Failed to set backlight PWM (err %d)", ret);
    }
}

/* ==================== UI 界面构建函数 ==================== */

/*
 * [函数] 阶段一：在 app_screen 上绘制开机欢迎画面
 */
static void lvgl_draw_boot_ui(void)
{
    lv_obj_t *logo_img;       
    lv_obj_t *title_label;    
    lv_obj_t *info_label;     
    lv_obj_t *status_label;   

    /* 【安全策略】直接使用活动主屏幕，不进行任何动态多屏创建 */
    app_screen = lv_screen_active();

    /* 填充深蓝色背景 */
    lv_obj_set_style_bg_color(app_screen, lv_color_hex(0x1a237e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(app_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 绘制 Zephyr 图像 */
    logo_img = lv_image_create(app_screen);
    lv_image_set_src(logo_img, &zephyr_200x116);
    lv_obj_align(logo_img, LV_ALIGN_TOP_MID, 0, 20);

    /* 绘制标题 */
    title_label = lv_label_create(app_screen);
    lv_label_set_text(title_label, "Zephyr RTOS + LVGL");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 150);

    /* 绘制硬件规格信息描述 */
    info_label = lv_label_create(app_screen);
    lv_label_set_text(info_label, "ESP32-S3 DevKitC\nST7789  240x280\nWatch OS Project");
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xb0bec5), 0);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(info_label, LV_ALIGN_BOTTOM_MID, 0, -65);

    /* 绘制启动状态文本 */
    status_label = lv_label_create(app_screen);
    lv_label_set_text(status_label, "Loading OS...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xffb300), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -15);

    LOG_INF("Boot interface drawn successfully.");
}

/*
 * [函数] 阶段二：清空同一块 app_screen 并直接刷出精美表盘 UI
 * * * 核心安全机制：
 * 使用 lv_obj_clean(app_screen) 清理自己手动生成的子控件，
 * 绝不使用动态删除屏幕本身的函数，彻底断绝底层刷新链表被粉碎的问题。
 */
static void lvgl_draw_main_watch_ui(void)
{
    LOG_INF("Clearing boot objects and re-drawing watchface...");
    
    /* 1. 清理开机画面留下的子图像和文本标签，还原干净画布 */
    lv_obj_clean(app_screen);

    /* 2. 更改画布背景色为极具深邃感的纯黑色（省电智能手表风） */
    lv_obj_set_style_bg_color(app_screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(app_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 3. 状态栏部件：蓝牙服务连接标志 */
    lv_obj_t *bt_label = lv_label_create(app_screen);
    lv_label_set_text(bt_label, "BLE");
    lv_obj_set_style_text_color(bt_label, lv_color_hex(0x2196f3), 0); /* 经典蓝牙蓝 */
    lv_obj_set_style_text_font(bt_label, &lv_font_montserrat_14, 0);
    lv_obj_align(bt_label, LV_ALIGN_TOP_LEFT, 15, 10);

    /* 4. 状态栏部件：电量满格指示 */
    lv_obj_t *bat_label = lv_label_create(app_screen);
    lv_label_set_text(bat_label, "100%");
    lv_obj_set_style_text_color(bat_label, lv_color_hex(0x4caf50), 0); /* 满电翠绿 */
    lv_obj_set_style_text_font(bat_label, &lv_font_montserrat_14, 0);
    lv_obj_align(bat_label, LV_ALIGN_TOP_RIGHT, -15, 10);

    /* 5. 数字大时钟显示 (放置在居中靠上方) */
    lv_obj_t *time_label = lv_label_create(app_screen);
    lv_label_set_text(time_label, "10:08");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, 0);
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 35);

    /* 6. 表盘正中央核心部件：步数运动圆环进度条 (Arc) */
    lv_obj_t *step_arc = lv_arc_create(app_screen);
    lv_obj_set_size(step_arc, 130, 130);            /* 手表圆环宽高度 */
    lv_arc_set_rotation(step_arc, 135);             /* 对称旋转下方缺口 */
    lv_arc_set_bg_angles(step_arc, 0, 270);         /* 圆环总可控行进角度 */
    lv_arc_set_value(step_arc, 72);                 /* 赋予默认今日运动百分比 */
    
    /* 圆环样式微调：运动覆盖轨迹设为动感橙色 */
    lv_obj_set_style_arc_color(step_arc, lv_color_hex(0xff5722), LV_PART_INDICATOR);
    /* 圆环未完成背景轨道设为极暗的碳黑色 */
    lv_obj_set_style_arc_color(step_arc, lv_color_hex(0x222222), LV_PART_MAIN);
    /* 完全隐藏圆环上原带的拖动圆球(Knob) */
    lv_obj_set_style_opa(step_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    
    lv_obj_align(step_arc, LV_ALIGN_CENTER, 0, 10);

    /* 7. 内嵌在圆环进度条中心的步数及单位说明 */
    lv_obj_t *step_data_label = lv_label_create(app_screen);
    lv_label_set_text(step_data_label, "8,431\nsteps");
    lv_obj_set_style_text_color(step_data_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(step_data_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(step_data_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(step_data_label, LV_ALIGN_CENTER, 0, 10);

    /* 8. 底部健康功能展示栏：实时心率模拟监测文本 */
    lv_obj_t *hr_label = lv_label_create(app_screen);
    lv_label_set_text(hr_label, "Heart Rate: 72 BPM");
    lv_obj_set_style_text_color(hr_label, lv_color_hex(0xe91e63), 0); /* 脉搏红 */
    lv_obj_set_style_text_font(hr_label, &lv_font_montserrat_14, 0);
    lv_obj_align(hr_label, LV_ALIGN_BOTTOM_MID, 0, -25);

    LOG_INF("Custom Watch Face drawn successfully onto reusable screen canvas!");
}

/* ==================== 显示设备初始化函数 ==================== */
static const struct device *display_init(void)
{
    const struct device *display_dev;
    int ret;

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device '%s' is not ready!",
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
    uint32_t loop_count = 0;          

    LOG_INF("LCD/LVGL Thread starting...");

    /* 基础稳压延时 */
    k_msleep(500);

    /* ---------- 步骤 1: 驱使 PWM 控制器点亮屏幕背光 ---------- */
    lcd_backlight_set(g_lcd_brightness);
    LOG_INF("Backlight set to %d%%", g_lcd_brightness);

    /* ---------- 步骤 2: 初始化硬件屏显设备驱动 ---------- */
    display_dev = display_init();
    if (display_dev == NULL) {
        LOG_ERR("Display init failed! LCD thread will exit.");
        return;
    }

    /* ---------- 步骤 3: 确保校验 LVGL 引擎内核状态 ---------- */
    if (!lv_is_initialized()) {
        LOG_ERR("LVGL Core not ready! Check prj.conf");
        return;
    }
    g_lvgl_ready = true; 
    LOG_INF("LVGL initialized and ready");

    /* ---------- 步骤 4: 在当前画布上渲染首屏开机信息 ---------- */
    lvgl_draw_boot_ui();

    /* ---------- 步骤 5: 保持上电状态展示 5 秒时间 ---------- */
    LOG_INF("Showing dev board info for 5 seconds...");
    k_sleep(K_SECONDS(5));

    /* ---------- 步骤 6: 拒绝释放容器，通过单级复用机制刷新至主表盘 ---------- */
    // LOG_INF("Switching watch views directly...");
    // lvgl_draw_main_watch_ui();

    /* ---------- 步骤 7: 进入低功耗长效状态心跳循环 ---------- */
    while (1) {
        k_sleep(K_SECONDS(30));
        loop_count++;
        LOG_DBG("Display thread alive (%u min), Watch UI running", loop_count / 2);
    }
}

/* ==================== 线程静态调度配置 ==================== */
#define LCD_LVGL_STACK_SIZE 4096
#define LCD_LVGL_PRIORITY 14

K_THREAD_DEFINE(lcd_lvgl_tid, LCD_LVGL_STACK_SIZE,
                lcd_lvgl_thread_entry, NULL, NULL, NULL,
                LCD_LVGL_PRIORITY, 0, 0);