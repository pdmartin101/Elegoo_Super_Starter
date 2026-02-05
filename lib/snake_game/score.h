#ifndef SCORE_H
#define SCORE_H



// 全局分数变量声明（在 Score.cpp 中初始化）
extern int current_score;

// 分数相关函数声明
void initScore();         // 初始化分数（清零）
void addScore(int val);   // 增加分数（val为增加的分值）
void resetScore();        // 重置分数（清零）
void drawScore();         // 绘制分数到OLED

#endif