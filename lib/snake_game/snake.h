#ifndef SNAKE_H
#define SNAKE_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// -------------------------- 硬件配置宏定义 --------------------------
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define OLED_ADDR   0x3C  // OLED地址（根据实际调整）



// -------------------------- 游戏参数宏定义 --------------------------
#define DEAD_ZONE   600   // 摇杆死区
#define MOVE_STEP   1     // 移动步长
#define MOVE_SPEED  50    // 移动速度（ms）

#define SNAKE_SIZE  4     // 蛇头/蛇身/食物尺寸（4x4像素）
#define FOOD_SIZE   4
#define MAX_LENGTH  30    // 蛇最大长度
#define MAX_SNAKE_X (OLED_WIDTH - SNAKE_SIZE)  // 蛇头X最大合法值（124）
#define MAX_SNAKE_Y (OLED_HEIGHT - SNAKE_SIZE) // 蛇头Y最大合法值（60）

// -------------------------- 颜色定义 --------------------------
#define FOOD_COLOR        SSD1306_WHITE
#define SNAKE_BODY_COLOR  SSD1306_WHITE
#define SNAKE_HEAD_COLOR  SSD1306_WHITE

// -------------------------- 全局对象声明 --------------------------
extern Adafruit_SSD1306 display;  // OLED显示对象

// -------------------------- 全局变量声明 --------------------------
// 蛇相关变量
extern int snake_x[MAX_LENGTH];    // 蛇每节X坐标（0=蛇头）
extern int snake_y[MAX_LENGTH];    // 蛇每节Y坐标
extern int snake_length;           // 蛇当前长度
extern bool isShow;                // 蛇头闪烁状态
extern int current_dir;            // 移动方向（-1=无，0=上，1=右，2=下，3=左）

// 食物相关变量
extern int food_x;
extern int food_y;

//启动相关
extern bool start_game;

// -------------------------- 函数声明 --------------------------
// 蛇核心函数（Snake.cpp）
void resetSnake();                 // 重置蛇为初始状态
void moveSnake();                  // 蛇移动（含蛇身跟随）
bool checkSnakeOverBoundary();     // 检查蛇头是否越界（越界返回true）

// 摇杆函数（Joystick.cpp）
void initJoystick();               // 初始化摇杆（引脚模式）
void readJoystick();               // 读取摇杆方向并更新current_dir

// 食物函数（Food.cpp）
void generateFood();               // 生成不与蛇身重叠的食物
void drawFood();                   // 绘制食物

// 绘制函数（Snake.cpp）
void drawSnake();                  // 绘制蛇（蛇头+蛇身）
void blinkSnakeAndFood();          // 绘制蛇+食物+蛇头闪烁

// 吃食物函数（Food.cpp）
bool checkFoodCollision();         // 检查是否吃到食物（吃到返回true）

#endif