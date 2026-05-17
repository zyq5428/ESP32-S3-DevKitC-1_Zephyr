#include <zephyr/kernel.h>
#include <soc/soc_memory_layout.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(MEMTEST, LOG_LEVEL_INF);

/* * 定义测试的元素数量：4095 个 uint32_t 
 * 4095 * 4 字节 = 16380 字节 (约 16KB)，这对于 16MB 的 PSRAM 来说是小菜一碟
 */
#define TEST_ELEMENTS_COUNT 4095

/**
 * @brief Memory Test 线程入口函数
 * 
 * @param p1, p2, p3 线程参数（由 K_THREAD_DEFINE 传入，此处未使用）
 */
void memtest_thread_entry(void *p1, void *p2, void *p3)
{
    uint32_t *p_mem = NULL;
    uint32_t k;
    size_t total_bytes = TEST_ELEMENTS_COUNT * sizeof(uint32_t);

    LOG_INF("----- PSRAM 外置内存测试开始 -----");
    LOG_DBG("准备申请的空间大小: %zu 字节", total_bytes);

    /*
     * 关键修改点：
     * 1. 属性改用 SMH_REG_ATTR_CACHEABLE：ESP32-S3 的八线 PSRAM 是挂在 CPU 缓存（Cache）总线上的。
     * 在 Zephyr 驱动中，它被注册为“可缓存的外置堆内存”。
     * 2. 对齐大小设为 4 字节：因为我们存放的是 uint32_t（4字节数据），4字节对齐读写效率最高。
     */
    p_mem = (uint32_t *)shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, 
                                                        32, 
                                                        total_bytes);

    // 检查是否成功申请到内存
    if (p_mem == NULL) {
        LOG_ERR("❌ 错误：PSRAM 内存分配失败！");
        return;
    }

    // 打印申请成功的物理/虚拟内存起始地址（用 %p 打印指针）
    LOG_DBG("[成功] 成功在 PSRAM 申请到内存，起始地址为: %p", (void *)p_mem);

    // 阶段一：向 PSRAM 中写入已知数据
    LOG_DBG("正在写入数据...");
    for (k = 0; k < TEST_ELEMENTS_COUNT; k++) {
        p_mem[k] = k; // 将当前下标作为值写入内存
    }

    // 阶段二：从 PSRAM 中读出并校验数据
    LOG_DBG("正在校验数据...");
    for (k = 0; k < TEST_ELEMENTS_COUNT; k++) {
        if (p_mem[k] != k) {
            // 如果读出来的值和写进去的不一致，代表硬件读写有误或内存重叠
            LOG_ERR("❌ 校验失败！在下标 [%" PRIu32 "] 处: 读到的值 = %" PRIu32 " (期望的值 = %" PRIu32 ")", 
                    k, p_mem[k], k);
            break;
        }
    }

    /* * 阶段三：无论校验成功还是失败，作为优秀程序员，必须在退出前释放掉申请的内存！
     * 否则会导致外部堆内存发生“内存泄漏（Memory Leak）”。
     */
    shared_multi_heap_free(p_mem);
    LOG_DBG("已经成功释放分配的 PSRAM 内存指针。");

    // 最后的总成绩单单核对
    if (k < TEST_ELEMENTS_COUNT) {
        LOG_ERR("❌ 结论：PSRAM 内存内容完整性校验失败！");
        return;
    }

    LOG_INF("🎉 结论：PSRAM 16KB 空间连续读写测试成功！外置硬件极其稳定！");
}

/* 线程配置参数 */
#define MEMTEST_STACK_SIZE 2048  // 线程栈大小（单位：字节）
#define MEMTEST_PRIORITY 13      // 线程优先级（数字越大优先级越低）

/**
 * @brief 定义并自动启动线程
 * 
 * 参数依次为：线程 ID, 栈大小, 入口函数, 参数1, 参数2, 参数3, 优先级, 选项, 启动延迟
 */
K_THREAD_DEFINE(memtest_tid, MEMTEST_STACK_SIZE, 
                memtest_thread_entry, NULL, NULL, NULL,
                MEMTEST_PRIORITY, 0, 0);