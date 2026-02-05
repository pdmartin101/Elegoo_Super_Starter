#include "snake.h"
#include <Arduino.h>

// -------------------------- 生成不与蛇身重叠的食物 --------------------------
void generateFood() {
  bool overlap;  // 是否与蛇身重叠
  do {
    overlap = false;
    // 食物坐标：合法范围内，且与蛇身尺寸对齐（4的倍数）
    food_x = (random(0, MAX_SNAKE_X / SNAKE_SIZE - 2)) * SNAKE_SIZE;
    food_y = (random(4, MAX_SNAKE_Y / SNAKE_SIZE )) * SNAKE_SIZE;

    // 检查是否与蛇身任何一节重叠
    for (int i = 0; i < snake_length; i++) {
      if (abs(snake_x[i] - food_x) < SNAKE_SIZE && abs(snake_y[i] - food_y) < SNAKE_SIZE) {
        overlap = true;
        break;
      }
    }
  } while (overlap);  // 重叠则重新生成

  Serial.printf("生成食物：(%d,%d)\n", food_x, food_y);
}

// -------------------------- 绘制食物（圆形） --------------------------
void drawFood() {
  int center_x = food_x + FOOD_SIZE / 2;
  int center_y = food_y + FOOD_SIZE / 2;
  display.fillCircle(center_x, center_y, FOOD_SIZE / 2, FOOD_COLOR);
}

// -------------------------- 检查是否吃到食物 --------------------------
bool checkFoodCollision() {
  // 碰撞判断：蛇头与食物重叠超过一半
  bool xOverlap = (snake_x[0] < food_x + FOOD_SIZE/2) && (snake_x[0] + SNAKE_SIZE > food_x + FOOD_SIZE/2);
  bool yOverlap = (snake_y[0] < food_y + FOOD_SIZE/2) && (snake_y[0] + SNAKE_SIZE > food_y + FOOD_SIZE/2);

  if (xOverlap && yOverlap) {
    // 蛇长度加1（不超过最大长度）
    if (snake_length < MAX_LENGTH) {
      int tail_idx = snake_length - 1;  // 当前蛇尾索引
      int new_tail_x = snake_x[tail_idx];
      int new_tail_y = snake_y[tail_idx];

      // 新增节在蛇尾后方（反向延伸）
      switch (current_dir) {
        case 0:  // 上移 → 新增节在蛇尾下方
          new_tail_y += SNAKE_SIZE;
          break;
        case 1:  // 右移 → 新增节在蛇尾左方
          new_tail_x -= SNAKE_SIZE;
          break;
        case 2:  // 下移 → 新增节在蛇尾上方
          new_tail_y -= SNAKE_SIZE;
          break;
        case 3:  // 左移 → 新增节在蛇尾右方
          new_tail_x += SNAKE_SIZE;
          break;
        default:
          new_tail_y += SNAKE_SIZE;  // 无方向时默认下方
          break;
      }

      // 新增节坐标赋值 
      snake_x[snake_length] = new_tail_x;
      snake_y[snake_length] = new_tail_y;
      snake_length++;

      Serial.printf("吃到食物！长度：%d，新增节：(%d,%d)\n", snake_length, new_tail_x, new_tail_y);
    }
    return true;  // 吃到食物，返回true
  }
  return false;  // 未吃到
}