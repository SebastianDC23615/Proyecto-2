/*
 * ili9341.h
 *
 *  Created on: Aug 20, 2024
 *      Author: Pablo Mazariegos
 */

#ifndef INC_ILI9341_H_
#define INC_ILI9341_H_

#include "lcd_registers.h"
#include "font.h"
#include <stdint.h>
#include "main.h"

#define LCD_DC_PORT    GPIOA
#define LCD_DC_PIN     GPIO_PIN_4
#define LCD_DC_L()     (LCD_DC_PORT->BSRR = (LCD_DC_PIN<<16))	//
#define LCD_DC_H()     (LCD_DC_PORT->BSRR =  LCD_DC_PIN)		//Poner el modo RS en high es para mandar dato

#define LCD_CS_PORT    GPIOB
#define LCD_CS_PIN     GPIO_PIN_0
#define LCD_CS_L()     (LCD_CS_PORT->BSRR = (LCD_CS_PIN<<16)) 	//Pone el chip select en activo para empezar la comunicación con la pantalla
#define LCD_CS_H()     (LCD_CS_PORT->BSRR =  LCD_CS_PIN)		//

void LCD_Init(void);
void LCD_CMD(uint8_t cmd);
void LCD_DATA(uint8_t data);
void SetWindows(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2);
void LCD_Clear(unsigned int c);
void H_line(unsigned int x, unsigned int y, unsigned int l, unsigned int c);
void V_line(unsigned int x, unsigned int y, unsigned int l, unsigned int c);
void Rect(unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned int c);
void FillRect(unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned int c);
void LCD_Print(char* text, int x, int y, int fontSize, int color, int background);

void LCD_Bitmap(unsigned int x, unsigned int y, unsigned int width, unsigned int height, const uint16_t *bitmap);
void LCD_BitmapTransparent(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t *bitmap, uint16_t transparentColor);
void LCD_BitmapFast(unsigned int x, unsigned int y, unsigned int width, unsigned int height, const uint8_t *bitmap);
void LCD_BitmapPartial(int x, int y, int draw_w, int draw_h, const uint16_t *bitmap, int src_x, int src_y, int src_total_w);
void LCD_BitmapPartialTransparent(int x, int y, int draw_w, int draw_h, const uint16_t *bitmap, int src_x, int src_y, int src_total_w, uint16_t transpColor);
void LCD_Sprite(int x, int y, int width, int height, const uint16_t *bitmap,
                int columns, int index, char flip, char offset);
void LCD_FadeInPartial(int x, int y, int draw_w, int draw_h, const uint16_t *bitmap, int src_x, int src_y, int src_total_w, int speed);
void LCD_FadeInTransparent(int x, int y, int draw_w, int draw_h, const uint16_t *bitmap, uint16_t transpColor, int speed);

//------------------------------PROPIAS------------------------------//
void LCD_SpriteOverBg(int x, int y, int width, int height,
                      const uint16_t *bitmap, int columns, int index,
                      char flip, char offset, uint16_t transpColor,
                      const uint16_t *bg, unsigned int bgWidth);

void LCD_RestoreBgDelta(int old_x, int old_y, int new_x, int new_y,
                        int w, int h,
                        const uint16_t *bg, unsigned int bgWidth,
                        int trail_sx0, int trail_sy0,
                        int trail_sx1, int trail_sy1,
                        uint16_t trail_color);
#endif /* INC_ILI9341_H_ */
