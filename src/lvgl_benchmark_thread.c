/*
 * [LVGL 性能基准测试线程] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能:
 *   运行 LVGL 官方 benchmark 性能测试, 自动测量:
 *     - FPS (每秒帧数)
 *     - CPU 占用率
 *     - 渲染耗时 (render time)
 *     - 刷屏耗时 (flush time)
 *
 * 测试内容 (自动依次运行):
 *    1. 空白场景     - 无控件的空界面
 *    2. 矩形 + 圆角  - 大量矩形和圆角矩形
 *    3. 文本标签     - 多语言文本渲染
 *    4. 图片         - 图片解码和显示
 *    5. 线条         - 大量线条绘制
 *    6. 弧线         - 弧线和圆环
 *    7. 图层         - 透明图层混合
 *    8. 混合场景     - 综合控件
 *
 * 测试完成后会显示汇总屏幕, 列出每项得分和总评。
 *
 * 线程架构:
 *   - 独立 K_THREAD_DEFINE, 不依赖 lcd_lvgl_thread
 *   - 等待 LVGL 就绪后自动运行测试
 *   - 测试完成后线程退出 (不会阻塞其他功能)
 *
 * 注意事项:
 *   - Benchmark 会接管整个屏幕, 覆盖当前 UI
 *   - 测试运行约 30~60 秒 (取决于硬件性能)
 *   - 完成后屏幕显示测试结果, 按 Reset 键可恢复
 */

/* ==================== Zephyr 头文件 ==================== */
#include <zephyr/kernel.h>          /* [内核] K_THREAD_DEFINE, k_msleep */
#include <zephyr/device.h>          /* [设备] device_is_ready */
#include <zephyr/devicetree.h>      /* [设备树] DT_CHOSEN */
#include <zephyr/sys/printk.h>      /* [打印] printk */
#include <zephyr/logging/log.h>     /* [日志] LOG_MODULE_REGISTER, LOG_INF */

/* ==================== LVGL 头文件 ==================== */
#include <lvgl.h>                   /* [LVGL] lv_* API */
/*
 * [LVGL Demo] 官方 Demo 伞形头文件
 * 路径解释: LVGL include path 指向 modules/lib/gui/lvgl/src/
 *           Demo 头文件在 modules/lib/gui/lvgl/demos/
 *           所以相对路径为 ../demos/lv_demos.h
 *          当 LV_USE_DEMO_BENCHMARK=y 时, 此文件自动包含 lv_demo_benchmark.h
 */
#include "../demos/lv_demos.h"

/* ==================== 项目头文件 ==================== */
#include "lcd_lvgl_thread.h"        /* [头文件] g_lvgl_ready 标志 */

/* ==================== 日志模块注册 ==================== */
LOG_MODULE_REGISTER(LVGL_BENCH, LOG_LEVEL_INF);

/* ==================== 线程入口函数 ==================== */

/**
 * @brief LVGL Benchmark 性能测试线程入口
 *
 * 工作流程:
 *   1. 等待 LVGL 和显示设备初始化完成 (检查 g_lvgl_ready)
 *   2. 等待显示屏彻底就绪 (确保 lcd_lvgl_thread 完成 UI 初始化)
 *   3. 调用 lv_demo_benchmark() 运行官方性能测试
 *   4. 测试完成后线程退出
 *
 * @param p1, p2, p3 线程参数 (未使用)
 */
void lvgl_benchmark_thread_entry(void *p1, void *p2, void *p3)
{
    /*
     * [步骤 1] 等待 LVGL 初始化完成
     *
     * g_lvgl_ready 由 lcd_lvgl_thread_entry() 在完成
     * 显示设备检测 + LVGL 状态确认 + UI 创建后设为 true。
     *
     * 轮询间隔 100ms, 最长等待 10 秒 (100 次 × 100ms)
     */
    int wait_count = 0;
    LOG_INF("Benchmark thread: waiting for LVGL to be ready...");
    while (!g_lvgl_ready && wait_count < 100) {
        k_msleep(100);
        wait_count++;
    }

    if (!g_lvgl_ready) {
        LOG_ERR("Benchmark thread: LVGL not ready after %d ms, aborting.",
                wait_count * 100);
        return;
    }

    LOG_INF("Benchmark thread: LVGL ready (waited %d ms).", wait_count * 100);

    /*
     * [步骤 2] 额外等待 500ms
     * 确保显示设备已完成 blanking off 和第一帧渲染
     * 避免 benchmark 的初始化命令与 ST7789 驱动冲突
     */
    k_msleep(500);

    /*
     * [步骤 3] 运行 LVGL 官方性能基准测试
     *
     * lv_demo_benchmark() 会:
     *   1. 接管当前活动屏幕
     *   2. 依次运行 8 个渲染场景
     *   3. 每个场景测试 5 秒 (默认), 测量 FPS/CPU/渲染/刷屏耗时
     *   4. 全部完成后显示汇总结果界面
     *
     * 测试期间 lcd_lvgl_thread 仍在后台休眠,
     * UI 刷新由 LVGL 工作队列 (优先级 5) 独立驱动, 互不干扰。
     *
     * 注意: lv_timer_handler() 由 CONFIG_LV_Z_RUN_LVGL_ON_WORKQUEUE=y
     *       自动在专用线程中调用, 本线程只负责启动测试。
     */
    LOG_INF("Benchmark thread: starting lv_demo_benchmark()...");
    LOG_INF("  The test will run ~30-60 seconds.");
    LOG_INF("  Watch the screen for FPS/results.");

    lv_demo_benchmark();

    /*
     * [步骤 4] 测试完成
     * lv_demo_benchmark() 返回后, 屏幕停留在汇总界面
     * 本线程退出 — 不再需要做任何事
     */
    LOG_INF("Benchmark thread: test completed. Thread exiting.");
    LOG_INF("  Press RESET to return to normal UI.");
}

/* ==================== 线程配置与定义 ==================== */

/*
 * [栈空间] 2048 字节
 * 只调用 lv_demo_benchmark() API, 不需要大量局部变量
 */
#define BENCHMARK_STACK_SIZE 2048

/*
 * [优先级] 10
 * 高于 lcd_lvgl_thread (14) 但低于 BLE (11)
 * 确保 benchmark 启动不被低优先级线程阻塞
 */
#define BENCHMARK_PRIORITY 10

/**
 * @brief 使用 K_THREAD_DEFINE 静态定义并自动启动线程
 */
K_THREAD_DEFINE(lvgl_benchmark_tid, BENCHMARK_STACK_SIZE,
                lvgl_benchmark_thread_entry, NULL, NULL, NULL,
                BENCHMARK_PRIORITY, 0,
                /*
                 * [启动延迟] 3000ms (3 秒)
                 * 给 lcd_lvgl_thread 充足的初始化时间
                 * (显示设备 init + backlight + blanking off + UI 创建)
                 */
                3000);
