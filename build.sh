#!/usr/bin/bash
gcc -fsanitize=address -Wall -g -o app main.c -lvulkan -lglfw
/home/roryc/vulkansdk/1.4.357.1/x86_64/bin/slangc shader.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o slang.spv
