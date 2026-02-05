
#include "snake.h"
#include <Arduino.h>
#include "score.h"
#include "Joystick.h"

// -------------------------- 重置蛇为初始状态 --------------------------
void resetSnake() {
  // 蛇头回到屏幕中心
  snake_x[0] = (OLED_WIDTH - SNAKE_SIZE) / 2;  // (128-4)/2=62
  snake_y[0] = (OLED_HEIGHT - SNAKE_SIZE) / 2; // (64-4)/2=30

  // 重置长度为1（仅保留蛇头）
  snake_length = 1;

  // 清空移动方向（静止）
  current_dir = -1;

  // 重置闪烁状态
  isShow = true;
  if(!start_game){
    isGameOver=true;
  }
  
  
  Serial.printf("蛇重置完成：中心(%d,%d)，长度=%d\n", snake_x[0], snake_y[0], snake_length);
}

// -------------------------- 蛇移动（含蛇身跟随） --------------------------
void moveSnake() {
  if (current_dir == -1) return;  // 无方向时不移动

  // 蛇身跟随：后一节 = 前一节坐标（从尾到头更新）
  for (int i = snake_length - 1; i > 0; i--) {
    snake_x[i] = snake_x[i - 1];
    snake_y[i] = snake_y[i - 1];
  }

  // 蛇头移动（自由移动，不限制边界）
  switch (current_dir) {
    case 0:  // 上（Y减小）
      snake_y[0] -= MOVE_STEP+current_score/5;
      break;
    case 1:  // 右（X增大）
      snake_x[0] += MOVE_STEP+current_score/5;
      break;
    case 2:  // 下（Y增大）
      snake_y[0] += MOVE_STEP+current_score/5;
      break;
    case 3:  // 左（X减小）
      snake_x[0] -= MOVE_STEP+current_score/5;
      break;
  }

  // 调试：打印蛇头坐标
  // Serial.printf("蛇头：(%d,%d) | 方向：%d\n", snake_x[0], snake_y[0], current_dir);
}

// -------------------------- 检查蛇头是否越界 --------------------------
bool checkSnakeOverBoundary() {
  // 越界条件：蛇头任何部分超出屏幕
  bool xOver = (snake_x[0] < 0) || (snake_x[0] > MAX_SNAKE_X);
  bool yOver = (snake_y[0] < 8) || (snake_y[0] > MAX_SNAKE_Y+2);

  if (xOver || yOver) {
    Serial.println("蛇头越界！游戏重置");
    return true;
  }
  return false;
}

// -------------------------- 绘制蛇（蛇头+蛇身） --------------------------
void drawSnake() {
  // 绘制蛇身（不闪烁）
  for (int i = 1; i < snake_length; i++) {
    display.fillRect(snake_x[i], snake_y[i], SNAKE_SIZE, SNAKE_SIZE, SNAKE_BODY_COLOR);
  }

  // 绘制蛇头（根据闪烁状态）
  if (isShow) {
    display.fillRect(snake_x[0], snake_y[0], SNAKE_SIZE, SNAKE_SIZE, SNAKE_HEAD_COLOR);
  } else {
    display.fillRect(snake_x[0], snake_y[0], SNAKE_SIZE, SNAKE_SIZE, SSD1306_BLACK);
  }
}

// -------------------------- 绘制蛇+食物+蛇头闪烁 --------------------------
void blinkSnakeAndFood() {
  display.clearDisplay();
  drawScore();
  drawSnake();   // 绘制蛇（含蛇头闪烁）
  drawFood();    // 绘制食物
  display.display();
  isShow = !isShow;  // 切换闪烁状态
}