/*
 * ili9341.c
 *
 *  Created on: Aug 20, 2024
 *      Author: Pablo Mazariegos
 */
#include <stdlib.h> // malloc()
#include <string.h> // memset()
#include "stm32f4xx_ll_spi.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "pgmspace.h"
#include "ili9341.h"
#include "main.h"

/*  RST 	- PC1
 *  RS/DC 	- PA4
 *  CS  	- PB0
 *  MOSI 	- PA7
 *  MISO 	- PA6
 *  SCK 	- PA5
 * */

#define LCD_DC_PORT    GPIOA
#define LCD_DC_PIN     GPIO_PIN_4
#define LCD_DC_L()     (LCD_DC_PORT->BSRR = (LCD_DC_PIN<<16))	//
#define LCD_DC_H()     (LCD_DC_PORT->BSRR =  LCD_DC_PIN)		//Poner el modo RS en high es para mandar dato

#define LCD_CS_PORT    GPIOB
#define LCD_CS_PIN     GPIO_PIN_0
#define LCD_CS_L()     (LCD_CS_PORT->BSRR = (LCD_CS_PIN<<16)) 	//Pone el chip select en activo para empezar la comunicación con la pantalla
#define LCD_CS_H()     (LCD_CS_PORT->BSRR =  LCD_CS_PIN)		//

extern const uint8_t smallFont[1140];
extern const uint16_t bigFont[1520];
extern SPI_HandleTypeDef hspi1;
//***************************************************************************************************************************************
// Función para inicializar LCD
//***************************************************************************************************************************************
void LCD_Init(void) {

	//****************************************
	// Secuencia de Inicialización
	//****************************************
	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
	/*Configure GPIO pin Output Level */
	//HAL_GPIO_WritePin(GPIOA, LCD_RD_Pin | LCD_WR_Pin | LCD_RS_Pin,GPIO_PIN_SET);
	HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
	HAL_Delay(5);
	HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_RESET);
	HAL_Delay(20);
	HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
	HAL_Delay(150);
	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);

	//****************************************
	LCD_CMD(0xE9);  // SETPANELRELATED
	LCD_DATA(0x20);
	//****************************************
	LCD_CMD(0x11); // Exit Sleep SLEEP OUT (SLPOUT)
	HAL_Delay(100);
	//****************************************
	LCD_CMD(0xD1);    // (SETVCOM)
	LCD_DATA(0x00);
	LCD_DATA(0x71);
	LCD_DATA(0x19);
	//****************************************
	LCD_CMD(0xD0);   // (SETPOWER)
	LCD_DATA(0x07);
	LCD_DATA(0x01);
	LCD_DATA(0x08);
	//****************************************
	LCD_CMD(0x36);  // (MEMORYACCESS)
	LCD_DATA(0x40 | 0x80 | 0x20 | 0x08); // LCD_DATA(0x19);
	//****************************************
	LCD_CMD(0x3A); // Set_pixel_format (PIXELFORMAT)
	LCD_DATA(0x05); // color setings, 05h - 16bit pixel, 11h - 3bit pixel
	//****************************************
	LCD_CMD(0xC1);    // (POWERCONTROL2)
	LCD_DATA(0x10);
	LCD_DATA(0x10);
	LCD_DATA(0x02);
	LCD_DATA(0x02);
	//****************************************
	LCD_CMD(0xC0); // Set Default Gamma (POWERCONTROL1)
	LCD_DATA(0x00);
	LCD_DATA(0x35);
	LCD_DATA(0x00);
	LCD_DATA(0x00);
	LCD_DATA(0x01);
	LCD_DATA(0x02);
	//****************************************
	LCD_CMD(0xC5); // Set Frame Rate (VCOMCONTROL1)
	LCD_DATA(0x04); // 72Hz
	//****************************************
	LCD_CMD(0xD2); // Power Settings  (SETPWRNORMAL)
	LCD_DATA(0x01);
	LCD_DATA(0x44);
	//****************************************
	LCD_CMD(0xC8); //Set Gamma  (GAMMASET)
	LCD_DATA(0x04);
	LCD_DATA(0x67);
	LCD_DATA(0x35);
	LCD_DATA(0x04);
	LCD_DATA(0x08);
	LCD_DATA(0x06);
	LCD_DATA(0x24);
	LCD_DATA(0x01);
	LCD_DATA(0x37);
	LCD_DATA(0x40);
	LCD_DATA(0x03);
	LCD_DATA(0x10);
	LCD_DATA(0x08);
	LCD_DATA(0x80);
	LCD_DATA(0x00);
	//****************************************
	LCD_CMD(0x2A); // Set_column_address 320px (CASET)
	LCD_DATA(0x00);
	LCD_DATA(0x00);
	LCD_DATA(0x01);
	LCD_DATA(0x3F);
	//****************************************
	LCD_CMD(0x2B); // Set_page_address 480px (PASET)
	LCD_DATA(0x00);
	LCD_DATA(0x00);
	LCD_DATA(0x01);
	LCD_DATA(0xE0);
	//  LCD_DATA(0x8F);
	LCD_CMD(0x29); //display on
	LCD_CMD(0x2C); //display on

	LCD_CMD(ILI9341_INVOFF); //Invert Off
	HAL_Delay(120);
	LCD_CMD(ILI9341_SLPOUT);    //Exit Sleep
	HAL_Delay(120);
	LCD_CMD(ILI9341_DISPON);    //Display on
	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
}
//***************************************************************************************************************************************
// Función para enviar comandos a la LCD - parámetro (comando)
//***************************************************************************************************************************************
void LCD_CMD(uint8_t cmd) {
	LCD_CS_L();
	LCD_DC_L();
	// ESPERAR que SPI no esté ocupado PRIMERO
	while (SPI1->SR & SPI_SR_BSY)
		;
	// Versión simple LL
	while (!(SPI1->SR & SPI_SR_TXE))
		;  // Esperar TX empty
	SPI1->DR = cmd;                    // Escribir dato
	while (SPI1->SR & SPI_SR_BSY)
		;     // Esperar fin

	LCD_CS_H();
}
//***************************************************************************************************************************************
// Función para enviar datos a la LCD - parámetro (dato)
//***************************************************************************************************************************************
void LCD_DATA(uint8_t data) {
	LCD_CS_L();
	LCD_DC_H();
	HAL_SPI_Transmit(&hspi1, &data, 1, 1);
	LCD_CS_H();
}
//***************************************************************************************************************************************
// Función para definir rango de direcciones de memoria con las cuales se trabajara (se define una ventana)
//***************************************************************************************************************************************
void SetWindows(unsigned int x1, unsigned int y1, unsigned int x2,
		unsigned int y2) {
	LCD_CMD(0x2a); // Set_column_address 4 parameters
	LCD_DATA(x1 >> 8);
	LCD_DATA(x1);
	LCD_DATA(x2 >> 8);
	LCD_DATA(x2);
	LCD_CMD(0x2b); // Set_page_address 4 parameters
	LCD_DATA(y1 >> 8);
	LCD_DATA(y1);
	LCD_DATA(y2 >> 8);
	LCD_DATA(y2);
	LCD_CMD(0x2c); // Write_memory_start
}
//***************************************************************************************************************************************
// Función para borrar la pantalla - parámetros (color)
//***************************************************************************************************************************************
void LCD_Clear(unsigned int c) {
	unsigned int x, y;
//	HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
//	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
	LCD_DC_H();
	LCD_CS_L();
	SetWindows(0, 0, 319, 239);

	for (x = 0; x < 320; x++)
		for (y = 0; y < 240; y++) {
			LCD_DATA(c >> 8);
			LCD_DATA(c);
		}
//	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
	LCD_CS_H();

}
//***************************************************************************************************************************************
// Función para dibujar una línea horizontal - parámetros ( coordenada x, cordenada y, longitud, color)
//***************************************************************************************************************************************
void H_line(unsigned int x, unsigned int y, unsigned int l, unsigned int c) {
	unsigned int i;
	LCD_CMD(0x02c); //write_memory_start
	//	HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
	//	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
	LCD_DC_H();
	LCD_CS_L();
	//l = l + x;
	SetWindows(x, y, l + x, y);
	//j = l; // * 2;
	for (i = 0; i < l; i++) {
		LCD_DATA(c >> 8);
		LCD_DATA(c);
	}
	//	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
	LCD_CS_H();
}
//***************************************************************************************************************************************
// Función para dibujar una línea vertical - parámetros ( coordenada x, cordenada y, longitud, color)
//***************************************************************************************************************************************
void V_line(unsigned int x, unsigned int y, unsigned int l, unsigned int c) {
	unsigned int i;
	LCD_CMD(0x02c); //write_memory_start
	//	HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
	//	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
	LCD_DC_H();
	LCD_CS_L();
	//l = l + y;
	SetWindows(x, y, x, y + l);
	//j = l; //* 2;
	for (i = 1; i <= l; i++) {
		LCD_DATA(c >> 8);
		LCD_DATA(c);
	}
	//	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
	LCD_CS_H();
}
//***************************************************************************************************************************************
// Función para dibujar un rectángulo - parámetros ( coordenada x, cordenada y, ancho, alto, color)
//***************************************************************************************************************************************
void Rect(unsigned int x, unsigned int y, unsigned int w, unsigned int h,
		unsigned int c) {
	H_line(x, y, w, c);
	H_line(x, y + h, w, c);
	V_line(x, y, h, c);
	V_line(x + w, y, h, c);
}
//***************************************************************************************************************************************
// Función para dibujar un rectángulo relleno - parámetros ( coordenada x, cordenada y, ancho, alto, color)
//***************************************************************************************************************************************
void FillRect(unsigned int x, unsigned int y, unsigned int w, unsigned int h,
		unsigned int c) {
	LCD_CMD(0x02c); // write_memory_start
	//	HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
	//	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
	LCD_DC_H();
	LCD_CS_L();

	SetWindows(x, y, x + w - 1, y + h - 1);
	unsigned int k = w * h * 2 - 1;
	for (int i = 0; i < w; i++) {
		for (int j = 0; j < h; j++) {
			LCD_DATA(c >> 8);
			LCD_DATA(c);

			//LCD_DATA(bitmap[k]);
			k = k - 2;
		}
	}
	//	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
	LCD_CS_H();
}
//***************************************************************************************************************************************
// Función para dibujar texto - parámetros ( texto, coordenada x, cordenada y, color, background)
//***************************************************************************************************************************************
void LCD_Print(char *text, int x, int y, int fontSize, int color,
		int background) {

	int fontXSize;
	int fontYSize;

	if (fontSize == 1) {
		fontXSize = fontXSizeSmal;
		fontYSize = fontYSizeSmal;
	}
	if (fontSize == 2) {
		fontXSize = fontXSizeBig;
		fontYSize = fontYSizeBig;
	}
	if (fontSize == 3) {
		fontXSize = fontXSizeNum;
		fontYSize = fontYSizeNum;
	}

	char charInput;
	int cLength = strlen(text);
	int charDec;
	int c;
	//int charHex;
	char char_array[cLength + 1];
	for (int i = 0; text[i] != '\0'; i++) {
		char_array[i] = text[i];
	}

	//text.toCharArray(char_array, cLength + 1);

	for (int i = 0; i < cLength; i++) {
		charInput = char_array[i];
		charDec = (int) charInput;
		//HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
		LCD_CS_L();
		SetWindows(x + (i * fontXSize), y, x + (i * fontXSize) + fontXSize - 1,
				y + fontYSize);
		long charHex1;
		for (int n = 0; n < fontYSize; n++) {
			if (fontSize == 1) {
				charHex1 = pgm_read_word_near(
						smallFont + ((charDec - 32) * fontYSize) + n);
			}
			if (fontSize == 2) {
				charHex1 = pgm_read_word_near(
						bigFont + ((charDec - 32) * fontYSize) + n);
			}
			for (int t = 1; t < fontXSize + 1; t++) {
				if ((charHex1 & (1 << (fontXSize - t))) > 0) {
					c = color;
				} else {
					c = background;
				}
				LCD_DATA(c >> 8);
				LCD_DATA(c);
			}
		}
		//HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
		LCD_CS_H();
	}
}
//***************************************************************************************************************************************
// Función para dibujar una imagen a partir de un arreglo de colores (Bitmap) Formato (Color 16bit R 5bits G 6bits B 5bits)
//***************************************************************************************************************************************
void LCD_Bitmap(unsigned int x, unsigned int y, unsigned int width,
		unsigned int height, const uint16_t *bitmap) {
	//LCD_CMD(0x02c); // write_memory_start
	//	HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
	//	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
	LCD_DC_H();
	LCD_CS_L();

	SetWindows(x, y, x + width - 1, y + height - 1);
	unsigned int total_pixels = width * height;

	// Enviar datos en bloques si es posible
	for (unsigned int i = 0; i < total_pixels; i++) {
		uint16_t pixel = bitmap[i];
		LCD_DATA(pixel >> 8);
		LCD_DATA(pixel & 0xFF);
	}
	//HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
	LCD_CS_H();
}
//***************************************************************************************************************************************
// Función para dibujar una imagen a partir de un arreglo de colores (Bitmap) Formato (Color 16bit R 5bits G 6bits B 5bits) con color transparente
//***************************************************************************************************************************************
void LCD_BitmapTransparent(uint16_t x, uint16_t y, uint16_t width,
		uint16_t height, const uint16_t *bitmap, uint16_t transparentColor) {
	for (unsigned int j = 0; j < height; j++) {
		for (unsigned int i = 0; i < width; i++) {
			unsigned int index = j * width + i;
			uint16_t pixel = bitmap[index];

			if (pixel != transparentColor) {
				// Comandos mínimos para un solo píxel
				LCD_CS_L();
				SetWindows(x + i, y + j, x + i, y + j);
				LCD_CMD(0x02c);
				LCD_DC_H();
				LCD_DATA(pixel >> 8);
				LCD_DATA(pixel & 0xFF);
				LCD_CS_H();
			}
		}
	}
}
//***************************************************************************************************************************************
// Función para dibujar una imagen sprite - los parámetros columns = número de imagenes en el sprite, index = cual desplegar, flip = darle vuelta
//***************************************************************************************************************************************
void LCD_Sprite(int x, int y, int width, int height, const uint16_t *bitmap,
		int columns, int index, char flip, char offset) {
	//LCD_CMD(0x02c); // write_memory_start
	//	HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
	//	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
	LCD_DC_H();
	LCD_CS_L();

	SetWindows(x, y, x + width - 1, y + height - 1);

	int ancho = width * columns;
	int k = 0;

	if (flip) {
		for (int j = 0; j < height; j++) {
			k = j * ancho + index * width - 1 - offset;
			k = k + width;
			for (int i = 0; i < width; i++) {
				uint16_t pixel = bitmap[k];
				LCD_DATA(pixel >> 8);
				LCD_DATA(pixel & 0xFF);
				k--;
			}
		}
	} else {
		for (int j = 0; j < height; j++) {
			k = j * ancho + index * width + 1 + offset;
			for (int i = 0; i < width; i++) {
				uint16_t pixel = bitmap[k];
				LCD_DATA(pixel >> 8);
				LCD_DATA(pixel & 0xFF);
				k++;
			}
		}
	}

	//HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
	LCD_CS_H();
}


//----------------------------PROPIAS---------------------


//***************************************************************************************************************************************
// Sprite con transparencia + fondo compositeado — single pass, bulk SPI
// Reemplaza RestoreBg + SpriteTransparent con una sola escritura por scanline.
// transpColor : color tratado como transparente (ej: 0xF81F)
// bg / bgWidth: bitmap de fondo completo y su ancho en píxeles
//***************************************************************************************************************************************

static uint8_t _slbuf[320 * 2];

static void _restore_bg_rect(int x, int y, int w, int h,
                              const uint16_t *bg, unsigned int bgWidth,
                              const uint8_t arena_map[320][240],
                              const uint16_t trail_colors[3]){

	if (w <= 0 || h <= 0) return;

	for (int j = 0; j < h; j++){
		for (int i = 0; i < w; i++){
			int sx = x + i;
			int sy = y + j;
			uint16_t px;
			uint8_t tm = 0;
			if (arena_map && sx >= 0 && sx < 320 && sy >= 0 && sy < 240)
				tm = arena_map[sx][sy];
			if (tm)
				px = trail_colors[tm];
			else
				px = bg[sy * bgWidth + sx];
			_slbuf[i * 2]     = px >> 8;
			_slbuf[i * 2 + 1] = px & 0xFF;
		}

		SetWindows(x, y+j, x+w-1, y+j);
		LCD_DC_H();
		LCD_CS_L();
		HAL_SPI_Transmit(&hspi1, _slbuf, w*2, HAL_MAX_DELAY);
		LCD_CS_H();

	}
}

void LCD_SpriteOverBg(int x, int y, int width, int height,
                      const uint16_t *bitmap, int columns, int index,
                      char flip, char offset, uint16_t transpColor,
                      const uint16_t *bg, unsigned int bgWidth,
                      const uint8_t arena_map[320][240],
                      const uint16_t trail_colors[3]) {
    int ancho = width * columns;

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            int sx = x + i;
            int sy = y + j;
            uint16_t px;
            uint8_t tm = 0;
            if (arena_map && sx >= 0 && sx < 320 && sy >= 0 && sy < 240)
                tm = arena_map[sx][sy];
            if (tm)
                px = trail_colors[tm];
            else
                px = bg[sy * bgWidth + sx];
            _slbuf[i * 2]     = px >> 8;
            _slbuf[i * 2 + 1] = px & 0xFF;
        }

        int k;
        if (flip) {
            k = j * ancho + index * width - 1 - offset + width;
            for (int i = 0; i < width; i++) {
                uint16_t px = bitmap[k--];
                if (px != transpColor) {
                    _slbuf[i * 2]     = px >> 8;
                    _slbuf[i * 2 + 1] = px & 0xFF;
                }
            }
        } else {
            k = j * ancho + index * width + offset;
            for (int i = 0; i < width; i++) {
                uint16_t px = bitmap[k++];
                if (px != transpColor) {
                    _slbuf[i * 2]     = px >> 8;
                    _slbuf[i * 2 + 1] = px & 0xFF;
                }
            }
        }

        SetWindows(x, y + j, x + width - 1, y + j);
        LCD_DC_H();
        LCD_CS_L();
        HAL_SPI_Transmit(&hspi1, _slbuf, width * 2, HAL_MAX_DELAY);
        LCD_CS_H();
    }
}

void LCD_RestoreBgDelta(int old_x, int old_y, int new_x, int new_y,
                        int w, int h,
                        const uint16_t *bg, unsigned int bgWidth,
                        const uint8_t arena_map[320][240],
                        const uint16_t trail_colors[3]) {
    int dx = new_x - old_x;
    int dy = new_y - old_y;

    if (dx == 0 && dy == 0) return;

    if (dx >= w || dx <= -w || dy >= h || dy <= -h){
    	_restore_bg_rect(old_x, old_y, w, h, bg, bgWidth, arena_map, trail_colors);
    	return;
    }

    if (dy > 0){
    	_restore_bg_rect(old_x, old_y, w, dy, bg, bgWidth, arena_map, trail_colors);
    }else if (dy < 0){
    	_restore_bg_rect(old_x, old_y + h + dy, w, -dy, bg, bgWidth, arena_map, trail_colors);
    }

    int v_y = old_y;
    int v_h = h;

    if (dy > 0){
    	v_y = old_y + dy;
    	v_h = h - dy;
    }else if (dy < 0){
    	v_y = old_y;
    	v_h = h + dy;
    }

    if (dx > 0){
    	_restore_bg_rect(old_x, v_y, dx, v_h, bg, bgWidth, arena_map, trail_colors);
    }else if (dx < 0){
    	_restore_bg_rect(old_x + w + dx, v_y, -dx, v_h, bg, bgWidth, arena_map, trail_colors);
    }
}
