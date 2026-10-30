# 独立诊断工程：ReinkCase 四色墨水屏接线电平观察
# 构建：idf.py -C tools/gpio_probe build
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gpio_probe)
