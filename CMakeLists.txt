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
    src/led_thread.c
    src/button_thread.c
    # src/bt_beacon_thread.c
    # src/bt_ibeacon_thread.c
    # src/bt_broadcaster_thread.c
    # src/bt_bthome_sensor_thread.c
    # src/bt_peripheral_thread.c
    src/bt_peripheral_nus_thread.c
)

# (可选) 链接所需的 Zephyr 库
# 正确的链接方式：只需要链接到 'zephyr' 目标
# target_link_libraries(app PRIVATE
#     zephyr
# )
