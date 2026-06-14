# SPDX-License-Identifier: Apache-2.0

# 设定CMake最低版本门槛
cmake_minimum_required(VERSION 3.20.0)

# 告诉 Zephyr 在哪里找自定义的设备树绑定文件
# list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/esp32s3_devkitc_procpu.overlay")

# 设置自定义设备树绑定文件 (Bindings) 的搜索根目录。
# ${CMAKE_CURRENT_SOURCE_DIR} 指的是当前 CMakeLists.txt 所在的文件夹路径。
# 这行代码告诉 Zephyr 编译器：“除了去内核找硬件定义，也去我当前的项目目录下找找（通常是 dts/bindings 目录）”。
# 这样你写的 custom,sh1106.yaml 才能被系统识别。
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# 引入 Zephyr 编译框架的核心包。
# REQUIRED 表示这个包是必须的，找不到就直接报错停止。
# HINTS $ENV{ZEPHYR_BASE} 是给编译器一个“线索”，告诉它去环境变量 ZEPHYR_BASE 指向的路径找 Zephyr。
# 执行完这一行后，Zephyr 的所有编译规则、库文件、驱动框架都会被加载进你的项目中。
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# 定义当前项目的名称。
# 这里将项目命名为 "application"。这个名字会影响最终生成的二进制文件名（如 application.elf）。
# 它必须放在 find_package(Zephyr ...) 之后，因为 Zephyr 需要先初始化环境才能支持后续的项目定义。
project(application)

# 添加应用程序私有的头文件目录
# PRIVATE 表示这个 include 路径只对当前 app target 有效
target_include_directories(app PRIVATE
    include
    drivers/include
)

# 添加所有源文件到应用程序目标
target_sources(app PRIVATE
    src/main.c
    # src/memory_thread.c
    src/led_thread.c
    src/button_thread.c
    # src/bt_beacon_thread.c
    # src/bt_ibeacon_thread.c
    # src/bt_broadcaster_thread.c
    # src/bt_bthome_sensor_thread.c
    # src/bt_peripheral_thread.c
    src/bt_peripheral_nus_thread.c
    src/lcd_lvgl_thread.c
    src/wifi_weather_thread.c
    src/fonts/weather_chinese_font.c
    src/images/zephyr_200x116.c
    src/images/sbpk_240x280.c
    # src/lvgl_benchmark_thread.c        # [可选] LVGL 官方性能测试 (需同时启用 prj.conf 中的 CONFIG_LV_USE_DEMO_BENCHMARK)
)

# [LVGL Widgets Demo] 收集上游 LVGL 仓库中的 demo 源文件
# Zephyr 的 LVGL 模块只编译核心库, demo 的 .c 文件需要手动加入 target_sources
# lv_demos.c 是伞形文件, 根据 CONFIG_LV_USE_DEMO_WIDGETS 用 #if 条件包含各子文件
# file(GLOB_RECURSE LVGL_WIDGETS_SOURCES
#     # [伞形文件] 根据 Kconfig 决定编译哪些 demo (必须首先包含)
#     ${ZEPHYR_LVGL_MODULE_DIR}/demos/lv_demos.c
#     # [Widgets 主文件] 创建 TabView 容器 + 4 个 Tab 页面 + 自动幻灯片定时器
#     ${ZEPHYR_LVGL_MODULE_DIR}/demos/widgets/lv_demo_widgets.c
#     # [4 个子页面] 每个 Tab 对应一个独立的功能页面
#     ${ZEPHYR_LVGL_MODULE_DIR}/demos/widgets/lv_demo_widgets_shop.c
#     ${ZEPHYR_LVGL_MODULE_DIR}/demos/widgets/lv_demo_widgets_analytics.c
#     ${ZEPHYR_LVGL_MODULE_DIR}/demos/widgets/lv_demo_widgets_profile.c
#     ${ZEPHYR_LVGL_MODULE_DIR}/demos/widgets/lv_demo_widgets_components.c
#     # [图片资源] 预编译的 RGBA 像素数组, 链接到 Flash .rodata 段
#     ${ZEPHYR_LVGL_MODULE_DIR}/demos/widgets/assets/img_lvgl_logo.c
#     ${ZEPHYR_LVGL_MODULE_DIR}/demos/widgets/assets/img_clothes.c
#     ${ZEPHYR_LVGL_MODULE_DIR}/demos/widgets/assets/img_demo_widgets_avatar.c
#     ${ZEPHYR_LVGL_MODULE_DIR}/demos/widgets/assets/img_demo_widgets_needle.c
# )
# target_sources(app PRIVATE ${LVGL_WIDGETS_SOURCES})

# (可选) 链接所需的 Zephyr 库
# 正确的链接方式：只需要链接到 'zephyr' 目标
# target_link_libraries(app PRIVATE
#     zephyr
# )
