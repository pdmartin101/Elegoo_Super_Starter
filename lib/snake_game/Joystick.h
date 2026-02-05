#ifndef JOYSTICK_H
#define JOYSTICK_H

#include "snake.h"

// 该文件仅用于函数声明（实际逻辑在Joystick.cpp），已在Snake.h中统一声明，此处可留空或重复声明
// 建议直接使用Snake.h中的声明，避免重复定义
#define JOY_X_PIN   35    // 摇杆X轴引脚
#define JOY_Y_PIN   34    // 摇杆Y轴引脚
// 在snake.h的宏定义部分添加
#define JOY_BUTTON_PIN 32  // 摇杆按键引脚（根据实际硬件调整）

// 在全局变量部分添加
extern bool isGameOver;    // 游戏结束状态标识
bool isJoystickButtonPressed(); // 添加按键检测函数
#endif