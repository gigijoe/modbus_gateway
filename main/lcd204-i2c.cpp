#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp32/rom/ets_sys.h"

#include "lcd204-i2c.hpp"
#include "pcf8574.hpp"

static PCF8574 *pcf8574 = nullptr;

// commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// flags for display entry mode
#define LCD_ENTRYRIGHT 0x00
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00

// flags for display on/off control
#define LCD_DISPLAYON 0x04
#define LCD_DISPLAYOFF 0x00
#define LCD_CURSORON 0x02
#define LCD_CURSOROFF 0x00
#define LCD_BLINKON 0x01
#define LCD_BLINKOFF 0x00

// flags for display/cursor shift
#define LCD_DISPLAYMOVE 0x08
#define LCD_CURSORMOVE 0x00
#define LCD_MOVERIGHT 0x04
#define LCD_MOVELEFT 0x00

// flags for function set
#define LCD_8BITMODE 0x10
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_1LINE 0x00
#define LCD_5x10DOTS 0x04
#define LCD_5x8DOTS 0x00

#define LCD_BACKLIGHT 0x08
#define LCD_NOBACKLIGHT 0x00

//uint8_t _data_pins[4];
static uint8_t _displayfunction;
static uint8_t _displaycontrol;
static uint8_t _displaymode;
//static uint8_t _initialized;
static uint8_t _numlines,_currline;

#define  LOW 0
#define  HIGH  1
#if 0
// definite io           // lcd 16x2 pinout   R/W pin allways to GND
#define  RS GPIO_Pin_12  // GPIOB 12
#define  EP GPIO_Pin_13  // GPIOB 13
#define  D4 GPIO_Pin_6  // GPIOC 6
#define  D5 GPIO_Pin_7  // GPIOC 7   
#define  D6 GPIO_Pin_8  // GPIOC 8  
#define  D7 GPIO_Pin_9  // GPIOC 9  
#else
#define EN 0b00000100  // Enable bit
#define RW 0b00000010  // Read/Write bit
#define RS 0b00000001  // Register select bit
#endif
/*********** mid level commands, for sending data/cmds */

void LCD204_I2C_Send(uint8_t value, uint8_t mode);

void LCD204_I2C_Command(uint8_t value)
{
    LCD204_I2C_Send(value, LOW);
}

void LCD204_I2C_Write(uint8_t value)
{
    LCD204_I2C_Send(value, HIGH);
}

void LCD204_I2C_Puts(const char row_string [])
{
    uint8_t i;
    for(i=0;i<LINE_SIZE;i++) {
        if(row_string[i] < 0x20)
            break;
        LCD204_I2C_Send(row_string[i], HIGH);
    }
}

void LCD204_I2C_PrintRow(uint8_t row, const char *fmt, ...)
{
    char str[LINE_SIZE+1];
    va_list args;

    va_start(args, fmt);
    vsnprintf(str, LINE_SIZE+1, fmt, args);
    va_end(args);
    LCD204_I2C_SetPos(0, row);

    int i, end = 0;
    for(i=0;i<LINE_SIZE;i++) {
        if(str[i] < 0x20)
            end = 1;
        if(end)
            LCD204_I2C_Send(' ', HIGH);
        else
            LCD204_I2C_Send(str[i], HIGH);
    }

    vTaskDelay(1 / portTICK_RATE_MS); /* For i2c timing */
}

void LCD204_I2C_PutChar(uint8_t col, uint8_t row, const char ch)
{
    LCD204_I2C_SetPos(col, row);
    LCD204_I2C_Send(ch, HIGH);
}

/************ low level data pushing commands **********/

void LCD204_I2C_Write4bits(uint8_t value)
{
#if 0  
    GPIO_WriteBit(GPIOC, D4, (value & 0x01) != 0 ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(GPIOC, D5, (value & 0x02) != 0 ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(GPIOC, D6, (value & 0x04) != 0 ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(GPIOC, D7, (value & 0x08) != 0 ? Bit_SET : Bit_RESET);

    GPIO_ResetBits(GPIOB, EP);
    //delay_us(1);
    GPIO_SetBits(GPIOB, EP);
    delay_us(1);
    GPIO_ResetBits(GPIOB, EP);
    delay_us(38);
#else
    pcf8574->write(value);
    ets_delay_us(1);
    pcf8574->write(value | EN);
    ets_delay_us(1);
    pcf8574->write(value & ~EN);
    ets_delay_us(50);
#endif
}

void LCD204_I2C_Send(uint8_t value, uint8_t mode)
{
#if 0
    GPIO_WriteBit(GPIOB, RS, (mode == HIGH) ? Bit_SET : Bit_RESET);
    //palWritePad(GPIOB, RS, mode);
#else
#endif
    uint8_t v = value & 0xf0;
    if(mode == HIGH)
        v |= RS;
    LCD204_I2C_Write4bits(v | LCD_BACKLIGHT);
    v = (value << 4) & 0xf0;
    if(mode == HIGH)
        v |= RS;
    LCD204_I2C_Write4bits(v | LCD_BACKLIGHT);
}

/* When the display powers up, it is configured as follows:
1. Display clear
2. Function set:
    DL = 1; 8-bit interface data
    N = 0; 1-line display
    F = 0; 5x8 dot character font
3. Display on/off control:
    D = 0; Display off
    C = 0; Cursor off
    B = 0; Blinking off
4. Entry mode set:
    I/D = 1; Increment by 1
    S = 0; No shift
*/

void LCD204_I2C_Init(uint8_t addr)
{ 
    pcf8574 = new PCF8574;
    pcf8574->begin(addr);
    pcf8574->pinMode(0, OUTPUT);
    pcf8574->pinMode(1, OUTPUT);
    pcf8574->pinMode(2, OUTPUT);
    pcf8574->pinMode(3, OUTPUT);
    pcf8574->pinMode(4, OUTPUT);
    pcf8574->pinMode(5, OUTPUT);
    pcf8574->pinMode(6, OUTPUT);
    pcf8574->pinMode(7, OUTPUT);
    pcf8574->write(0x00);

#if 0
    GPIO_InitTypeDef  GPIO_InitStructure;

    /* GPIOC clock enable */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_ResetBits(GPIOC, GPIO_Pin_6);
    GPIO_ResetBits(GPIOC, GPIO_Pin_7);
    GPIO_ResetBits(GPIOC, GPIO_Pin_8);
    GPIO_ResetBits(GPIOC, GPIO_Pin_9);

    /* GPIOB clock enable */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_ResetBits(GPIOB, GPIO_Pin_12);
    GPIO_ResetBits(GPIOB, GPIO_Pin_13);
#endif
    // only 4 bit mode
    //_data_pins[0] = D4;
    //_data_pins[1] = D5;
    //_data_pins[2] = D6;
    //_data_pins[3] = D7;

    // allways 4 BIT MODE
    //palSetPadMode(GPIOB, RS, PAL_MODE_OUTPUT_PUSHPULL); //RS
    
    // we can save 1 pin by not using RW.
    //palSetPadMode(GPIOB, EP, PAL_MODE_OUTPUT_PUSHPULL); //EP

    // configures data_pin
    //palSetPadMode(GPIOC, _data_pins[0], PAL_MODE_OUTPUT_PUSHPULL);
    //palSetPadMode(GPIOC, _data_pins[1], PAL_MODE_OUTPUT_PUSHPULL);
    //palSetPadMode(GPIOC, _data_pins[2], PAL_MODE_OUTPUT_PUSHPULL);
    //palSetPadMode(GPIOC, _data_pins[3], PAL_MODE_OUTPUT_PUSHPULL);

    _displayfunction = LCD_2LINE;

    _numlines = 4;
    _currline = 0;

    // for some 1 line displays you can select a 10 pixel high font
    // if ((dotsize != 0) && (lines == 1)) {
    //   _displayfunction |= LCD_5x10DOTS;
    // }

    //for some 1 line displays you can select a 10 pixel high font
    //SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION!
    //according to datasheet, we need at least 40ms after power rises above 2.7V
    //before sending commands.
    //chThdSleepMilliseconds(50);
    vTaskDelay(50 / portTICK_RATE_MS);
    // Now we pull RS low to begin commands
    //palClearPad(GPIOB, RS);
    
    //palClearPad(GPIOB, EP);

    //put the LCD into 4 bit or 8 bit mode
    // this is according to the hitachi HD44780 datasheet
    // figure 24, pg 46
    // we start in 8bit mode, try to set 4 bit mode
    LCD204_I2C_Write4bits(0x03 << 4);
    //chThdSleepMicroseconds(4500);
    vTaskDelay(5 / portTICK_RATE_MS);
    // second try
    LCD204_I2C_Write4bits(0x03 << 4);
    //chThdSleepMicroseconds(4500);
    vTaskDelay(5 / portTICK_RATE_MS);
    // third go!
    LCD204_I2C_Write4bits(0x03 << 4);
    ets_delay_us(150);
    //chThdSleepMicroseconds(150);
    // finally, set to 4-bit interface
    LCD204_I2C_Write4bits(0x02 << 4);
    //delay_us(200);
    // finally, set # lines, font size, etc.
    _displayfunction = LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS;
    LCD204_I2C_Command(LCD_FUNCTIONSET | _displayfunction);
    // turn the display on with no cursor or blinking default
    _displaycontrol = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;
    //LCD204_I2C_Command(LCD_DISPLAYCONTROL | _displaycontrol);
    LCD204_I2C_Display();
    // clear it off
    LCD204_I2C_Clear();
    // Initialize to default text direction (for romance languages)
    _displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
    // set the entry mode
    LCD204_I2C_Command(LCD_ENTRYMODESET | _displaymode);
}

/********** high level commands, for the user! */

void LCD204_I2C_Clear(void)
{
    LCD204_I2C_Command(LCD_CLEARDISPLAY);  // clear display, set cursor position to zero
    //chThdSleepMicroseconds(1600);  // this command takes a long time!
    vTaskDelay(4 / portTICK_RATE_MS);
}

void LCD204_I2C_Home(void)
{
    LCD204_I2C_Command(LCD_RETURNHOME);    // set cursor position to zero
    //chThdSleepMicroseconds(1600);  // this command takes a long time!
    vTaskDelay(4 / portTICK_RATE_MS);
}

void LCD204_I2C_SetPos(uint8_t col, uint8_t row)
{
    int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
    //int row_offsets[] = { 0x00, 0x40 };
    //int row_offsets[] = { 0x80, 0xc0 };
    if(row >= _numlines ) {
      row = _numlines-1;    // we count rows starting w/0
    }
    LCD204_I2C_Command(LCD_SETDDRAMADDR | (col + row_offsets[row]));
}

// Turn the display on/off (quickly)
void LCD204_I2C_NoDisplay(void)
{
    _displaycontrol &= ~LCD_DISPLAYON;
    LCD204_I2C_Command(LCD_DISPLAYCONTROL | _displaycontrol);
}

void LCD204_I2C_Display(void)
{
    _displaycontrol |= LCD_DISPLAYON;
    LCD204_I2C_Command(LCD_DISPLAYCONTROL | _displaycontrol);
}

// Turns the underline cursor on/off
void LCD204_I2C_NoCursor(void)
{
    _displaycontrol &= ~LCD_CURSORON;
    LCD204_I2C_Command(LCD_DISPLAYCONTROL | _displaycontrol);
}

void LCD204_I2C_Cursor(void)
{
    _displaycontrol |= LCD_CURSORON;
    LCD204_I2C_Command(LCD_DISPLAYCONTROL | _displaycontrol);
}

// Turn on and off the blinking cursor
void LCD204_I2C_NoBlink(void)
{
    _displaycontrol &= ~LCD_BLINKON;
    LCD204_I2C_Command(LCD_DISPLAYCONTROL | _displaycontrol);
}

void LCD204_I2C_Blink(void)
{
    _displaycontrol |= LCD_BLINKON;
    LCD204_I2C_Command(LCD_DISPLAYCONTROL | _displaycontrol);
}

// These commands scroll the display without changing the RAM
void LCD204_I2C_ScrollDisplayLeft(void)
{
    LCD204_I2C_Command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
}

void LCD204_I2C_ScrollDisplayRight(void)
{
    LCD204_I2C_Command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT);
}

// This is for text that flows Left to Right
void LCD204_I2C_LeftToRight(void)
{
    _displaymode |= LCD_ENTRYLEFT;
    LCD204_I2C_Command(LCD_ENTRYMODESET | _displaymode);
}

// This is for text that flows Right to Left
void LCD204_I2C_RightToLeft(void)
{
    _displaymode &= ~LCD_ENTRYLEFT;
    LCD204_I2C_Command(LCD_ENTRYMODESET | _displaymode);
}

// This will 'right justify' text from the cursor
void LCD204_I2C_Autoscroll(void)
{
    _displaymode |= LCD_ENTRYSHIFTINCREMENT;
    LCD204_I2C_Command(LCD_ENTRYMODESET | _displaymode);
}

// This will 'left justify' text from the cursor
void LCD204_I2C_NoAutoscroll(void)
{
    _displaymode &= ~LCD_ENTRYSHIFTINCREMENT;
    LCD204_I2C_Command(LCD_ENTRYMODESET | _displaymode);
}

// Allows us to fill the first 8 CGRAM locations
// with custom characters
void LCD204_I2C_CreateChar(uint8_t location, uint8_t charmap[])
{ 
    int i;
    location &= 0x7; // we only have 8 locations 0-7
    LCD204_I2C_Command(LCD_SETCGRAMADDR | (location << 3));
    for (i=0; i<8; i++) {
        LCD204_I2C_Write(charmap[i]);
    }
}
