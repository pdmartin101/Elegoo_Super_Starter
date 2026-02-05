#include "score.h"
#include <Arduino.h>
#include "snake.h"
// 全局分数变量初始化
int current_score = 0;

// -------------------------- 初始化分数（清零） --------------------------
void initScore() {
  current_score = 0;
  Serial.println("分数初始化完成：0分");
}

// -------------------------- 增加分数 --------------------------
void addScore(int val) {
  current_score += val;
  Serial.printf("分数+%d，当前总分：%d\n", val, current_score);
}

// -------------------------- 重置分数（清零） --------------------------
void resetScore() {
  current_score = 0;
  Serial.println("分数重置：0分");
}

// -------------------------- 绘制分数到OLED（左上角） --------------------------
void drawScore() {
  display.setTextSize(1);        // 字体大小（1=8x8像素）
  display.setTextColor(SSD1306_WHITE);  // 白色字体
  display.setCursor(2, 2);       // 坐标(2,2)（左上角留边距）
  display.print("Score: ");      // 文字前缀
  display.print(current_score);  // 显示当前分数
}