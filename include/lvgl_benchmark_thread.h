/*
 * [LVGL Benchmark 测试线程] 头文件
 *
 * 此线程由 K_THREAD_DEFINE 自动创建并延迟 3 秒启动。
 * 无需手动调用任何启动函数。
 *
 * 测试完成后线程自动退出, 屏幕保持测试结果界面。
 * 按下开发板的 RST 键可重启回到正常 UI。
 */

#ifndef LVGL_BENCHMARK_THREAD_H
#define LVGL_BENCHMARK_THREAD_H

#include <zephyr/types.h>

/**
 * @brief Benchmark 线程将在系统启动 3 秒后自动运行
 *
 * 通过 K_THREAD_DEFINE 中的启动延迟参数 (3000ms) 实现。
 * 本头文件仅作文档用途 — 不需要额外的初始化调用。
 */

#endif /* LVGL_BENCHMARK_THREAD_H */
