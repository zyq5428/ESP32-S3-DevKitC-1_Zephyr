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
 *   - 3V3 / 5V      → LCD VCC
 *   - GND           → LCD GND
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
#include "../demos/lv_demos.h"     /* [LVGL Demo] lv_demo_widgets() 等官方 Demo 入口 */

/* ==================== 项目头文件 ==================== */
#include "lcd_lvgl_thread.h"        /* [头文件] 全局变量声明 */
#include "images/zephyr_200x116.h"  /* [头文件] Zephyr Logo 图像描述符 */

/* ==================== 日志模块注册 ==================== */
LOG_MODULE_REGISTER(LCD_LVGL, LOG_LEVEL_INF);

/* ==================== 全局变量定义 ==================== */
volatile int g_lcd_brightness = 30; /* [初始化] 默认背光亮度 30% */
volatile bool g_lvgl_ready = false; /* [初始化] LVGL 尚未就绪 */

/* ==================== PWM 背光设备获取 ==================== */

/*
 * [设备树获取] 通过 pwm-backlight 别名获取 LEDC PWM 背光设备
 *
 * DT_ALIAS(pwm_backlight) 对应 overlay 中:
 *   aliases { pwm-backlight = &pwm_bl; };
 *   pwm_bl: pwm_bl { pwms = <&ledc0 0 PWM_HZ(250) PWM_POLARITY_NORMAL>; };
 *
 * PWM_DT_SPEC_GET 返回一个 pwm_dt_spec 结构体，包含:
 *   .dev     = &ledc0 设备指针
 *   .channel = 0         (LEDC 通道 0)
 *   .period  = 4000000   (250Hz → 周期 4,000,000 纳秒)
 *   .flags   = PWM_POLARITY_NORMAL
 */
static const struct pwm_dt_spec pwm_backlight =
    PWM_DT_SPEC_GET(DT_ALIAS(pwm_backlight));

/* ==================== 背光控制函数 ==================== */

/*
 * [函数] 设置 LCD 背光亮度 (PWM 无级调光)
 *
 * 通过 ESP32-S3 的 LEDC 控制器在 GPIO4 输出 PWM 信号，
 * 调节占空比实现从 0% (全黑) 到 100% (最亮) 的平滑无级调光。
 *
 * 工作原理:
 *   PWM 频率 = 250Hz (周期 = 4,000,000 纳秒)
 *   占空比 = brightness% × 周期
 *   例如: brightness=80 → pulse=3,200,000ns (80% 占空比)
 *
 * 参数 brightness: 0~100，表示亮度百分比
 *   - 0:   关闭背光 (占空比 0%，屏幕全黑)
 *   - 100: 最大亮度 (占空比 100%，屏幕最亮)
 *
 * 注意:
 *   - pwm_set_pulse_dt() 首次调用会自动初始化 LEDC 硬件
 *   - 250Hz 的频率远超人眼闪烁阈值 (~50Hz)，画面无屏闪感
 */
static void lcd_backlight_set(uint8_t brightness)
{
    int ret;
    uint32_t pulse_ns;    /* [变量] PWM 高电平持续时间 (纳秒) */

    /*
     * [就绪检查] 验证 LEDC PWM 设备是否可用
     * pwm_is_ready_dt() 同时检查设备驱动和 DT 配置
     */
    if (!pwm_is_ready_dt(&pwm_backlight)) {
        /*
         * [降级处理] 如果 PWM 不可用，保持静默
         * 可能是 overlay 中未正确配置 LEDC 节点
         */
        return;
    }

    /*
     * [钳位] 将亮度值限制在 0~100 范围内
     * 防止异常输入导致占空比溢出
     */
    if (brightness > 100) {
        brightness = 100;
    }

    /*
     * [计算占空比] 将百分比转换为纳秒脉冲宽度
     *
     * 公式: pulse_ns = (brightness / 100) × period_ns
     *
     * 例如:
     *   period = 4,000,000 ns (250Hz)
     *   brightness = 80 → pulse = (80 × 4,000,000) / 100 = 3,200,000 ns
     *   brightness = 25 → pulse = (25 × 4,000,000) / 100 = 1,000,000 ns
     *   brightness = 0  → pulse = 0 (完全关闭)
     *
     * 使用 64 位中间计算防止乘法溢出:
     *   brightness × period 最大 = 100 × 4,000,000 = 400,000,000
     *   这个结果在 32 位范围内 (最大约 4.29×10^9)，所以 uint32_t 够用
     */
    pulse_ns = (uint32_t)(((uint64_t)brightness *
                           (uint64_t)pwm_backlight.period) / 100U);

    /*
     * [核心操作] 设置 PWM 脉冲宽度 (即占空比)
     *
     * pwm_set_pulse_dt():
     *   参数1: &pwm_backlight — PWM 设备规格 (包含 dev + channel)
     *   参数2: pulse_ns       — 高电平持续时间 (纳秒)
     *
     * 返回值: 0 = 成功, 负数 = 错误码
     *
     * 注意:
     *   - 首次调用会触发 LEDC 硬件初始化 (时钟、定时器、GPIO 引脚)
     *   - 后续调用只需更新占空比寄存器，开销极小 (~数微秒)
     *   - 在 ISR 中调用也是安全的 (LEDC 寄存器操作是原子性的)
     */
    ret = pwm_set_pulse_dt(&pwm_backlight, pulse_ns);
    if (ret < 0) {
        LOG_ERR("Failed to set backlight PWM (err %d)", ret);
    }
}

/* ==================== LVGL 界面构建函数 ==================== */

/*
 * [函数] 创建开发板信息界面 (启动时展示 5 秒)
 *
 * 界面布局 (从外到内):
 *   1. 屏幕背景 — 深蓝色 (体现 Zephyr 品牌色)
 *   2. Zephyr Logo 图像 — 200x116 像素
 *   3. 标题标签 — "Zephyr RTOS + LVGL" (顶部居中)
 *   4. 信息标签 — "ESP32-S3 DevKitC / ST7789 / LCD PWM Backlight" (中部)
 *   5. 状态标签 — "System Ready" (底部, 绿色)
 */
static void lvgl_create_info_screen(void)
{
    lv_obj_t *logo_img;       /* [LVGL 对象] Zephyr Logo 图像 */
    lv_obj_t *title_label;    /* [LVGL 对象] 标题标签 */
    lv_obj_t *info_label;     /* [LVGL 对象] 信息标签 */
    lv_obj_t *status_label;   /* [LVGL 对象] 状态标签 */

    /*
     * [获取] 获取当前活动屏幕对象
     */
    lv_obj_t *screen = lv_screen_active();

    /*
     * [布局规划] 屏幕 240×280, 圆角屏需要上边距避开圆角遮挡
     *
     *  y=0    ┌──────────────────────┐
     *  y=20   │  ┌──────────────┐    │  ← Logo (200×116) 顶部留 20px 避开圆角
     *         │  │  Zephyr Logo │    │
     *  y=136  │  └──────────────┘    │  ← Logo 结束于 y=20+116=136
     *  y=150  │   Zephyr + LVGL      │  ← 标题 (Logo下方 14px 间距)
     *  y=172  │   ESP32-S3 DevKitC   │
     *         │   ST7789  240×280    │  ← 信息 3 行, 各约 17px
     *         │   LCD PWM 背光       │
     *  y=256  │     System Ready     │  ← 状态 (底部上方)
     *  y=280  └──────────────────────┘
     */

    /* ---------- 1. 屏幕背景 ---------- */
    lv_obj_set_style_bg_color(screen,
                              lv_color_hex(0x1a237e),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen,
                            LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ---------- 2. Zephyr Logo ---------- */
    /*
     * [位置] LV_ALIGN_TOP_MID, y=20
     * 顶部留 20px 上边距，避免被圆角遮挡
     */
    logo_img = lv_image_create(screen);
    lv_image_set_src(logo_img, &zephyr_200x116);
    lv_obj_align(logo_img, LV_ALIGN_TOP_MID, 0, 20);

    /* ---------- 3. 标题 ---------- */
    /*
     * [位置] LV_ALIGN_TOP_MID, y=150
     * Logo 底部在 y=136，留 14px 间距 → 标题起始 y=150
     */
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "Zephyr RTOS + LVGL");
    lv_obj_set_style_text_color(title_label,
                                lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label,
                               &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 150);

    /* ---------- 4. 信息标签 (底部) ---------- */
    /*
     * [位置] LV_ALIGN_BOTTOM_MID, y=-65
     * 从底部往上 65 像素，为状态标签留空间
     */
    info_label = lv_label_create(screen);
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
    /*
     * [位置] LV_ALIGN_BOTTOM_MID, y=-15
     * 底部上方 15px
     */
    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "System Ready");
    lv_obj_set_style_text_color(status_label,
                                lv_color_hex(0x4caf50),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(status_label,
                               &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -15);

    LOG_INF("LVGL UI with Zephyr logo created successfully");
}

/* ==================== LVGL 界面清理函数 ==================== */

/*
 * [函数] 清除当前屏幕上的所有 LVGL 对象
 *
 * 在切换到 Widgets Demo 之前调用，释放之前的信息界面占用的内存。
 * lv_obj_clean() 递归删除屏幕上的所有子对象 (标签、图像、背景等)，
 * 但保留屏幕对象本身 (方便 Demo 直接在上面绘制)。
 *
 * 为什么要清除:
 *   1. 旧 Logo 占 ~46KB 像素数据仍驻留 PSRAM → 内存浪费
 *   2. 新旧 UI 叠加 → 视觉混乱不可控
 *   3. lv_demo_widgets() 需要干净的屏幕起跑线
 */
static void lvgl_clear_screen(void)
{
    lv_obj_t *screen = lv_screen_active();  /* [获取] 当前活动屏幕对象 */

    lv_obj_clean(screen);                    /* [删除] 递归删除所有子对象 */

    /* [背景重置] 子对象删除后背景可能残留, 重置为纯黑色 */
    lv_obj_set_style_bg_color(screen,
                              lv_color_hex(0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen,
                            LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    LOG_INF("Screen cleared, ready for Widgets Demo");
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
    lcd_backlight_set(g_lcd_brightness);
    LOG_INF("Backlight set to %d%%", g_lcd_brightness);

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

    /* ---------- 步骤 4: 构建开发板信息界面 ---------- */
    lvgl_create_info_screen();
    LOG_INF("Dev board info screen created");

    /* ---------- 步骤 5: 展示信息界面 (5 秒) ---------- */
    /*
     * [产品展示] 上电后首先看到的是开发板信息:
     *   - Zephyr Logo (200x116 像素)
     *   - "Zephyr RTOS + LVGL" 标题
     *   - "ESP32-S3 DevKitC / ST7789 240x280" 规格
     *   - "System Ready" 绿色状态指示
     * 给用户 5 秒时间看清系统信息, 然后自动切换 Demo。
     */
    LOG_INF("Showing dev board info for 5 seconds...");
    k_sleep(K_SECONDS(5));

    /* ---------- 步骤 6: 启动 LVGL Widgets Demo ---------- */
    /*
     * [Demo 切换] 清除信息界面 → 启动官方 Widgets Demo
     *
     * lv_demo_widgets() 工作原理:
     *   1. 标记为 LV_DEMO_TYPE_FULL (全屏独占 Demo)
     *   2. 创建 4 个 Tab 页面: Shop(商店), Analytics(分析),
     *      Profile(个人资料), Components(控件展示)
     *   3. 启动 8 秒间隔的自动幻灯片切换定时器
     *   4. 每 2 秒随机改变主题颜色
     *   5. 返回后由 LVGL 工作队列周期性调用 lv_timer_handler() 驱动渲染
     *
     * 关于 240x280 小屏幕:
     *   - LVGL 9 内置 DPI 感知, 控件和文字会自动缩放
     *   - Tab 标签栏在窄屏上可能较挤, 但 4 个 Tab 均可点击
     *   - Widgets Demo 原始设计为 480x272, 在 240x280 上会等比缩小
     *
     * DMA 加速效果:
     *   SPI2 GDMA 通道 1(TX) 自动搬运 LVGL VDB(PSRAM) 到 ST7789V
     *   CPU 在每帧 134KB 传输期间可处理 BLE/按键等其他任务
     */
    LOG_INF("Launching LVGL Widgets Demo...");
    lvgl_clear_screen();
    lv_demo_widgets();
    LOG_INF("Widgets Demo launched! Slideshow auto-advances every 8 seconds.");
    LOG_INF("Tabs: Shop | Analytics | Profile | Components");

    /* ---------- 步骤 7: 空闲循环 — UI 由 LVGL 工作队列驱动 ---------- */
    /*
     * [架构说明] lv_demo_widgets() 返回后:
     *   - 屏幕已填充完整的 Widgets Demo 控件树
     *   - 自动幻灯片定时器已创建 (每 8 秒切换到下一个 Tab)
     *   - 屏幕刷新由 CONFIG_LV_Z_FLUSH_THREAD 独立线程驱动
     *   - lv_timer_handler() 由 LVGL 工作队列 (优先级 5) 周期性调用
     *   - SPI DMA (GDMA) 自动搬运帧缓冲到 ST7789V, CPU 空闲
     *
     * 本线程进入低功耗监控模式:
     *   每 30 秒输出心跳日志确认存活,
     *   未来可在此添加 Shell 命令切换 Demo、亮度调节等交互。
     */
    while (1) {
        k_sleep(K_SECONDS(30));
        loop_count++;
        LOG_DBG("Display thread alive (%u min), demo running", loop_count / 2);
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
