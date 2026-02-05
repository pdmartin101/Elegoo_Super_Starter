#include <Keypad.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Wire.h>


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // 无复位引脚设为-1
extern Adafruit_SSD1306 oled;

// 函数声明
void initKeypadAndOled();  // 初始化键盘+OLED
void getkeypad();          // 读取键盘输入
void displayInputOnOled(); // 显示输入的数字

// 全局输入缓存（存储键盘输入的数字）
extern String inputBuffer;
void getkeypad();