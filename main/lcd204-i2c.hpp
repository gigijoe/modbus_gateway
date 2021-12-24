#ifndef _LCD204_I2C_HPP
#define _LCD204_I2C_HPP

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define LINE_SIZE 20

void LCD204_I2C_Init(uint8_t addr);

void LCD204_I2C_Clear(void);
void LCD204_I2C_Home(void);
void LCD204_I2C_SetPos(uint8_t col, uint8_t row);
void LCD204_I2C_NoDisplay(void);
void LCD204_I2C_Display(void);
void LCD204_I2C_NoCursor(void);
void LCD204_I2C_Cursor(void);
void LCD204_I2C_NoBlink(void);
void LCD204_I2C_Blink(void);
void LCD204_I2C_ScrollDisplayLeft(void);
void LCD204_I2C_ScrollDisplayRight(void);
void LCD204_I2C_LeftToRight(void);
void LCD204_I2C_RightToLeft(void);
void LCD204_I2C_Autoscroll(void);
void LCD204_I2C_NoAutoscroll(void);
void LCD204_I2C_CreateChar(uint8_t location, uint8_t charmap[]);

void LCD204_I2C_Puts(const char str []);
void LCD204_I2C_PutChar(uint8_t col, uint8_t row, const char ch);
void LCD204_I2C_PrintRow(uint8_t row, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif

