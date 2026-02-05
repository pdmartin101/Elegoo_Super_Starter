#include "esp32-hal-gpio.h"
#include "Joystick.h"
#include <Arduino.h>
#include "snake.h"
// -------------------------- 摇杆初始化 --------------------------
void initJoystick() {
  pinMode(JOY_X_PIN, INPUT);  // 摇杆X轴设为输入
  pinMode(JOY_Y_PIN, INPUT);  // 摇杆Y轴设为输入
  pinMode(JOY_BUTTON_PIN, INPUT_PULLUP);
  Serial.println("摇杆初始化完成");
}

// -------------------------- 读取摇杆方向 --------------------------
void readJoystick() {
  int xVal = analogRead(JOY_X_PIN);
  int yVal = analogRead(JOY_Y_PIN);
  const int MAX_VAL = 4095;  // ADC最大取值（ESP32默认12位）

  // 仅限制反向移动，不限制边界（允许向边界移动）
  if (yVal < DEAD_ZONE && current_dir != 2) {        // 上（不反向向下）
    current_dir = 0;
  } else if (xVal > MAX_VAL - DEAD_ZONE && current_dir != 3) {  // 右（不反向向左）
    current_dir = 1;
  } else if (yVal > MAX_VAL - DEAD_ZONE && current_dir != 0) {  // 下（不反向向上）
    current_dir = 2;
  } else if (xVal < DEAD_ZONE && current_dir != 1) {  // 左（不反向向右）
    current_dir = 3;
  }
  // 无操作时保持原方向
}
bool isJoystickButtonPressed() {
  // 读取按键状态（下拉按键则检测LOW，上拉则检测LOW）

  return digitalRead(JOY_BUTTON_PIN) == LOW;
}


