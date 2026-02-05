#include <Arduino.h>

#include <Wire.h>
#include "snake.h"
#include "Joystick.h"
#include "score.h"

// -------------------------- Global Object Initialization --------------------------
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// -------------------------- Global Variable Initialization --------------------------
// Snake-related variables
int snake_x[MAX_LENGTH];
int snake_y[MAX_LENGTH];
int snake_length = 1;
bool isShow = true;
bool isGameOver=false;
int current_dir = -1;
bool start_game=true;

// Food-related variables
int food_x = 0;
int food_y = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED initialization failed!");
    while (1);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Initialize joystick
  initJoystick();
  initScore();

  // Initialize snake and food
  resetSnake();
  generateFood();

  // Initial drawing
  blinkSnakeAndFood();
  display.display();
}

void loop() {
  // 游戏结束状态处理
  if(start_game){
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    // 居中显示"GAME OVER"
    display.setCursor(OLED_WIDTH/2 - 36, OLED_HEIGHT/2 - 8);
    display.print("press joy button");
    display.display();
    if (isJoystickButtonPressed()) {
     start_game=false;
    }
    // isJoystickButtonPressed();
    return ;
  }
 

  if (isGameOver) {
    // 显示Game Over
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    // 居中显示"GAME OVER"
    display.setCursor(OLED_WIDTH/2 - 24, OLED_HEIGHT/2 - 8);
    display.print("GAME");
    display.setCursor(OLED_WIDTH/2 - 24, OLED_HEIGHT/2 + 8);
    display.print("OVER");
    display.display();
    
    // 检测按键重启游戏
    if (isJoystickButtonPressed()) {
      resetSnake();
      generateFood();
      resetScore();
      isGameOver = false;  // 恢复游戏运行状态
    }
    delay(100);  // 按键消抖
    return;
  }



  readJoystick();                  // Read joystick direction
  moveSnake();                     // Move snake
                                  
  if (checkSnakeOverBoundary()) {  // Check boundary: reset if out of bounds
    resetSnake();
    generateFood();
    resetScore();
  }

  if (checkFoodCollision()) {      // Check food collision: regenerate food if eaten
    generateFood();
    addScore(1);
  }

  blinkSnakeAndFood();             // Draw + blink effect
  delay(MOVE_SPEED);
}