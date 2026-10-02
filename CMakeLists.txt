cmake_minimum_required(VERSION 3.21)
project(Lens VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Q_OBJECT 元对象编译（lens_llm / lens_app）
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network Gui Quick Widgets)

if(MSVC)
  # 源文件含 UTF-8 中文注释；MSVC 默认按系统代码页 936 读会损坏 token，需显式 /utf-8。
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

# 开发期：任何发往大模型的请求先弹窗征得同意。正式发布设为 OFF。
option(DEV_SEND_CONFIRM "Dev: force consent before sending to LLM" OFF)

add_subdirectory(src/core)
add_subdirectory(src/llm)
add_subdirectory(src/app)
# 自检目标：链接 lens_core + lens_llm，只在本机构建、不进发布包
add_subdirectory(test)
