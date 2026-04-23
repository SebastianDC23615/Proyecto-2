/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ili9341.h"
#include "bitmaps.h"
#include "fatfs_sd.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef enum {
    DIR_UP    = 0,
    DIR_LEFT  = 1,
    DIR_DOWN  = 2,
    DIR_RIGHT = 3
} BikeDir;

typedef struct {
    int             x, y;
    int             prev_x, prev_y;
    BikeDir         dir;
    int             speed;
    const uint16_t *sheet;
    uint16_t        transp;
    uint16_t 		trail_color;
    int				lives;
    const uint16_t *icon;
    uint8_t         char_index;
} Bike;

typedef struct {
    int off_x, off_y;
    int w, h;
} HitboxRect;

typedef struct {
    uint8_t j1_yes;   /* P1 A button, "slow" */
    uint8_t j1_no;    /* P1 B button, "fast" */
    uint8_t j2_yes;   /* P2 A */
    uint8_t j2_no;    /* P2 B */
    uint8_t j1_x;     /* 0=left 127=center 255=right */
    uint8_t j1_y;     /* 0=up   127=center 255=down  */
    uint8_t j2_x;
    uint8_t j2_y;
} struct_mensaje;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define ARENA_X0      2
#define ARENA_Y0      36
#define ARENA_X1      316
#define ARENA_Y1      236

#define ARENA_W_TILES 160
#define ARENA_H_TILES 120

#define BIKE_W        16
#define BIKE_H        16
#define BIKE_TRANSP   0x0263 // #004D19

/* Collision hitbox: narrow rectangle centered in the 32x32 sprite box.
 * The LONG axis (32) always runs with the direction of travel, the SHORT
 * axis (14) is perpendicular and inset 9 px from each sprite edge.
 * Used by trail laying and collision — NOT by rendering (the sprite is
 * still drawn as a full 32x32 composite). */
#define HITBOX_LONG   BIKE_W                          /* 32, along heading */
#define HITBOX_SHORT  6                              /* perp. to heading  */
#define HITBOX_INSET  ((BIKE_W - HITBOX_SHORT) / 2)   /* = 9               */

/* Bounds for the bike's sprite top-left so its body stays in the arena.
 * Because the hitbox's long axis (32) equals BIKE_W, these numbers are
 * identical whether you reason about the sprite box or the hitbox's
 * leading edge along the direction of travel. */
#define BIKE_X_MIN    ARENA_X0
#define BIKE_Y_MIN    ARENA_Y0
#define BIKE_X_MAX    (ARENA_X1 - BIKE_W)
#define BIKE_Y_MAX    (ARENA_Y1 - BIKE_H)


#define JOY_CENTER       127
#define JOY_DEADZONE      80

#define BIKE_SPEED_NORMAL  2
#define BIKE_SPEED_FAST    4

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
extern const uint16_t arena_bg[];
extern const uint16_t cinematic[];
//extern const uint16_t main_menu[];

extern const uint16_t car_sel[];

extern const uint16_t bike_blue[];
extern const uint16_t bike_samflyn[];
extern const uint16_t bike_kevinflyn[];
extern const uint16_t bike_orange[];
extern const uint16_t bike_clu[];
extern const uint16_t bike_ares[];

extern const uint16_t icon_blue[];
extern const uint16_t icon_orange[];
extern const uint16_t icon_sam[];
extern const uint16_t icon_flyn[];
extern const uint16_t icon_clu[];
extern const uint16_t icon_ares[];

uint8_t arena_map[ARENA_H_TILES][ARENA_W_TILES];

uint16_t p1_trace_color;
uint16_t p2_trace_color;

/* UART receive state machine buffers, consumed by HAL_UART_RxCpltCallback. */
uint8_t rx_byte;
uint8_t rx_payload[8];
volatile uint8_t  sincronizado = 0;
volatile struct_mensaje datosJugadores;
uint8_t val = 0;
char msg[3];

volatile uint8_t dbg_pending = 0;
uint8_t dbg_copy[8];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */
void DrawTraceAndMarkMap(int cx, int cy, uint16_t color, uint8_t player_id);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// SD Card functions
FATFS fs;
FRESULT fres;

// BG Loading, directly to screen

void Draw_BG_From_SD(const char* filename) {
    FIL file;
    UINT bytes_read;
    uint8_t line_buffer[640];

    if (f_open(&file, filename, FA_READ) == FR_OK) {
        // SetWindows sends commands via LCD_CMD, which manages CS itself.
        // It ends with CS_H and DC_L after the 0x2C (RAMWR) command.
        SetWindows(0, 0, 319, 239);

        // Now manually enter data mode for the bulk pixel transfer.
        LCD_DC_H();
        LCD_CS_L();

        for (int y = 0; y < 240; y++) {
            f_read(&file, line_buffer, 640, &bytes_read);
            HAL_SPI_Transmit(&hspi1, line_buffer, 640, HAL_MAX_DELAY);
        }

        LCD_CS_H();
        f_close(&file);
    } else {
        LCD_Clear(0xF800);
    }
}

//Caracter scores

uint16_t char_wins[6] = {0, 0, 0, 0, 0, 0}; // 0=Blue, 1=Sam, 2=Kevin, 3=Orange, 4=Clu, 5=Ares

void Save_Scores() {
    FIL file;
    UINT bytes_written;

    // Open the file (FA_CREATE_ALWAYS ensures it creates a new file or overwrites the old one)
    if (f_open(&file, "scores.bin", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        // Write the exact 12 bytes (6 uint16_t) of data to the SD Card
        f_write(&file, char_wins, sizeof(char_wins), &bytes_written);
        f_close(&file);
    }
}

void Load_Scores() {
    FIL file;
    UINT bytes_read;

    // Try to open the file to read it
    if (f_open(&file, "scores.bin", FA_READ) == FR_OK) {
        f_read(&file, char_wins, sizeof(char_wins), &bytes_read);
        f_close(&file);
    } else {
        // FILE DOES NOT EXIST!
        // This is a fresh SD card. Call Save_Scores() to create 'scores.bin'
        // right now using the default zeros in our char_wins array.
        Save_Scores();
    }
}

// Lives icon logic

void Draw_Dashboard(Bike *b1, Bike *b2) {
    for (int i = 0; i < 4; i++) {
        int x1 = 2 + i * 32;
        int x2 = 286 - i * 32;
        if (i < b1->lives)
            LCD_SpriteOverBg(x1, 2, 32, 32, b1->icon, 5, 0, 0, 0, BIKE_TRANSP, arena_bg, 320);
        if (i < b2->lives)
            LCD_SpriteOverBg(x2, 2, 32, 32, b2->icon, 5, 0, 0, 0, BIKE_TRANSP, arena_bg, 320);
    }
}

void Explode_Life_Icon(int x, int y, const uint16_t *icon) {
    for (int frame = 1; frame <= 4; frame++) {
        LCD_SpriteOverBg(x, y, 32, 32, icon, 5, frame, 0, 0, BIKE_TRANSP, arena_bg, 320);
        HAL_Delay(150);
    }
}


// Trail logic

typedef struct { int8_t dx0, dy0, dx1, dy1; } TrailOffsets;

static const HitboxRect hitbox_for_dir[4] = {
    /* DIR_UP    */ { HITBOX_INSET, 0,            HITBOX_SHORT, HITBOX_LONG  },
    /* DIR_LEFT  */ { 0,            HITBOX_INSET, HITBOX_LONG,  HITBOX_SHORT },
    /* DIR_DOWN  */ { HITBOX_INSET, 0,            HITBOX_SHORT, HITBOX_LONG  },
    /* DIR_RIGHT */ { 0,            HITBOX_INSET, HITBOX_LONG,  HITBOX_SHORT },
};

/* Fill hx/hy/hw/hh with the bike's current hitbox in screen coordinates.
 * This is the ONLY place trail/collision code should read the collision
 * rect from — never use BIKE_W/BIKE_H directly for those. */
static inline void bike_get_hitbox(const Bike *b,
                                   int *hx, int *hy, int *hw, int *hh) {
    const HitboxRect *r = &hitbox_for_dir[b->dir];
    *hx = b->x + r->off_x;
    *hy = b->y + r->off_y;
    *hw = r->w;
    *hh = r->h;
}

/* 1. Check if the head of the bike hit a trace */
int check_trace_collision(Bike *b) {
    int head_x = b->x + 7; // Center X
    int head_y = b->y + 7; // Center Y

    // Push the check coordinate to the absolute front edge of the bike based on direction
    if (b->dir == DIR_UP) head_y = b->y;
    else if (b->dir == DIR_DOWN) head_y = b->y + 15;
    else if (b->dir == DIR_LEFT) head_x = b->x;
    else if (b->dir == DIR_RIGHT) head_x = b->x + 15;

    int mx = (head_x - ARENA_X0) / 2;
    int my = (head_y - ARENA_Y0) / 2;

    // If it's inside the array and the array is not 0, it's a crash!
    if (mx >= 0 && mx < ARENA_W_TILES && my >= 0 && my < ARENA_H_TILES) {
        if (arena_map[my][mx] != 0) return 1;
    }
    return 0;
}

/* 2. Check if the bikes hit each other head-on */
int check_head_on(Bike *b1, Bike *b2) {
    int dx = b1->x - b2->x;
    int dy = b1->y - b2->y;
    // If the centers of both bikes are within 10 pixels of each other, they crashed
    if (dx > -10 && dx < 10 && dy > -10 && dy < 10) return 1;
    return 0;
}

/* 3. Handle the animation, lives, and map reset. Returns 1 if the game is over. */
int handle_crash(Bike *b1, Bike *b2, int p1_crashed, int p2_crashed) {

    val = snprintf(msg, sizeof(msg), "%u", 6);
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);

    // Play on-field explosion (frames 4-7 of the bike sheet)
    for (int frame = 4; frame <= 7; frame++) {
        if (p1_crashed)
            LCD_SpriteOverBg(b1->x, b1->y, BIKE_W, BIKE_H, b1->sheet, 8, frame, 0, 0, b1->transp, arena_bg, 320);
        if (p2_crashed)
            LCD_SpriteOverBg(b2->x, b2->y, BIKE_W, BIKE_H, b2->sheet, 8, frame, 0, 0, b2->transp, arena_bg, 320);
        HAL_Delay(150);
    }

    // Deduct lives, then play the dashboard explosion on the slot that just died.
    // After decrement, b->lives is the new count; that value is also the 0-based
    // index of the slot that was lost (e.g. 3→2 loses slot 2).
    if (p1_crashed) {
        b1->lives--;
        Explode_Life_Icon(2 + b1->lives * 32, 2, b1->icon);
    }
    if (p2_crashed) {
        b2->lives--;
        Explode_Life_Icon(286 - b2->lives * 32, 2, b2->icon);
    }

    // Game over if either player is out of lives
    if (b1->lives <= 0 || b2->lives <= 0)
        return 1;

    // Still playing — reset arena, positions, and redraw
    memset(arena_map, 0, sizeof(arena_map));

    b1->x = BIKE_X_MIN; b1->y = BIKE_Y_MIN;
    b1->prev_x = BIKE_X_MIN; b1->prev_y = BIKE_Y_MIN;
    b1->dir = DIR_RIGHT;

    b2->x = BIKE_X_MAX; b2->y = BIKE_Y_MAX;
    b2->prev_x = BIKE_X_MAX; b2->prev_y = BIKE_Y_MAX;
    b2->dir = DIR_LEFT;

    LCD_Bitmap(0, 0, 320, 240, arena_bg);
    Draw_Dashboard(b1, b2);

    LCD_SpriteOverBg(b1->x, b1->y, BIKE_W, BIKE_H, b1->sheet, 8, (int)b1->dir, 0, 0, b1->transp, arena_bg, 320);
    LCD_SpriteOverBg(b2->x, b2->y, BIKE_W, BIKE_H, b2->sheet, 8, (int)b2->dir, 0, 0, b2->transp, arena_bg, 320);

    HAL_Delay(2000);
    val = snprintf(msg, sizeof(msg), "%u", 4);
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);
    return 0;
}

static inline void input_to_bike(uint8_t jx, uint8_t jy,
                                 uint8_t btn_slow, uint8_t btn_fast,
                                 BikeDir *io_dir, int *out_speed) {
    int dx = (int)jx - JOY_CENTER;
    int dy = (int)jy - JOY_CENTER;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;

    BikeDir requested = *io_dir;
    if (ax >= JOY_DEADZONE || ay >= JOY_DEADZONE) {
        if (ax > ay)      requested = (dx > 0) ? DIR_RIGHT : DIR_LEFT;
        else if (ay > ax) requested = (dy > 0) ? DIR_UP  : DIR_DOWN;
    }

    // Reject 180° reversal
    BikeDir cur = *io_dir;
    int is_reverse =
        (cur == DIR_UP    && requested == DIR_DOWN)  ||
        (cur == DIR_DOWN  && requested == DIR_UP)    ||
        (cur == DIR_LEFT  && requested == DIR_RIGHT) ||
        (cur == DIR_RIGHT && requested == DIR_LEFT);
    if (!is_reverse) *io_dir = requested;

    if (btn_fast) *out_speed = BIKE_SPEED_FAST;
    else *out_speed = BIKE_SPEED_NORMAL;
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_SPI2_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */

  	  val = snprintf(msg, sizeof(msg), "%u", 0);
  	  HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);

	  LCD_Init();
	  LCD_Clear(0x0000);

	  f_mount(&fs, "", 1);
	  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
	  HAL_SPI_Init(&hspi2);
	  Load_Scores();

	  // Start Animation

	  HAL_Delay(2000);
	  LCD_FadeInPartial(189, 79, 5, 3, cinematic, 26, 41, 63, 2);
	  HAL_Delay(2000);

	  val = snprintf(msg, sizeof(msg), "%u", 1);
	  HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);

	  LCD_FadeInTransparent(163, 38, 63, 108, cinematic, BIKE_TRANSP, 4);
	  HAL_Delay(1000);
  while (1) { // outer restart loop — returns here after each game ends

	  Draw_BG_From_SD("menu.bin");
	  HAL_Delay(2000);

	  val = snprintf(msg, sizeof(msg), "%u", 2);
	  HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);

	  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);

	  // Main Menu (Gamemode select)
		uint8_t Play = 0;
		int selected_mode = 0;
		uint8_t prev_j1_y = 127;
		uint8_t prev_j1_no = 0;

		int txt1_x = 20, txt1_y = 170;
		int txt2_x = 20, txt2_y = 200;

	  while (Play == 0) {
			struct_mensaje snap = datosJugadores;

			if (snap.j1_y > 170 && prev_j1_y <= 170) {
				selected_mode = 0;
			} else if (snap.j1_y < 80 && prev_j1_y >= 80) {
				selected_mode = 1;
			}
			prev_j1_y = snap.j1_y;

			// 2. Render Menu Text (with background color 0x0000 / Black)
			if (selected_mode == 0) {
				LCD_Print("> LIGHTCYCLE DUEL", txt1_x, txt1_y, 1, 0x04FF, 0x0000);
				LCD_Print("     CACHE LEAK  ", txt2_x, txt2_y, 1, 0xFFFF, 0x0000);
			} else {
				LCD_Print("  LIGHTCYCLE DUEL", txt1_x, txt1_y, 1, 0xFFFF, 0x0000);
				LCD_Print(">    CACHE LEAK  ", txt2_x, txt2_y, 1, 0x04FF, 0x0000);
			}

			// 3. Handle Selection Confirm (j1_no pressed)
			if (snap.j1_no != 0 && prev_j1_no == 0) {
				if (selected_mode == 0) {
					Play = 1;
				} else if (selected_mode == 1) {
					// Nuevo Modo
				}
			}
			prev_j1_no = snap.j1_no;

			HAL_Delay(50);



	  }

	  LCD_Clear(0x0000);
	  HAL_Delay(500);


	  // CHARACTER SELECTION MENU

	  const uint16_t *p1_selected_sheet = bike_blue;
	  const uint16_t *p2_selected_sheet = bike_orange;

	  const uint16_t *p1_icon = icon_blue;
	  const uint16_t *p2_icon = icon_orange;

	  uint8_t p1_char = 0; // 0=Blue, 1=Sam, 2=Kevin
	  uint8_t p2_char = 0; // 0=Orange, 1=Clu, 2=Ares

		if (selected_mode == 0) {
			Draw_BG_From_SD("charsel.bin");

			uint8_t p1_ready = 0;
			uint8_t p2_ready = 0;

			uint8_t prev_j1_x = 127;
			uint8_t prev_j2_x = 127;
			uint8_t prev_j1_no = 0;
			uint8_t prev_j2_no = 0;

			int p1_draw_x = 16, p1_draw_y = 88;
			int p2_draw_x = 176, p2_draw_y = 88;

			LCD_Print("  PROGRAM 1  ", 30, 60, 1, 0x07FF, 0x0000);
			LCD_Print("  PROGRAM 2  ", 190, 60, 1, 0xFD20, 0x0000);

			uint8_t force_render = 1;

			while (!p1_ready || !p2_ready) {
				struct_mensaje snap = datosJugadores;
				uint8_t update_p1 = force_render;
				uint8_t update_p2 = force_render;
				force_render = 0;

				// --- Player 1 Navigation ---
				if (!p1_ready) {
					if (snap.j1_x < 80 && prev_j1_x >= 80) {
						p1_char = (p1_char == 0) ? 2 : p1_char - 1;
						update_p1 = 1;
					} else if (snap.j1_x > 170 && prev_j1_x <= 170) {
						p1_char = (p1_char == 2) ? 0 : p1_char + 1;
						update_p1 = 1;
					}
				}
				prev_j1_x = snap.j1_x;

				if (snap.j1_no != 0 && prev_j1_no == 0) {
					p1_ready = !p1_ready; // Lock/Unlock choice
					update_p1 = 1;
				}

				// --- Player 2 Navigation ---
				if (!p2_ready) {
					if (snap.j2_x < 80 && prev_j2_x >= 80) {
						p2_char = (p2_char == 0) ? 2 : p2_char - 1;
						update_p2 = 1;
					} else if (snap.j2_x > 170 && prev_j2_x <= 170) {
						p2_char = (p2_char == 2) ? 0 : p2_char + 1;
						update_p2 = 1;
					}
				}
				prev_j2_x = snap.j2_x;

				if (snap.j2_no != 0 && prev_j2_no == 0) {
					p2_ready = !p2_ready; // Lock/Unlock choice
					update_p2 = 1;
				}

				prev_j1_no = snap.j1_no;
				prev_j2_no = snap.j2_no;

				// --- Render Updates ---
				if (update_p1) {
					FillRect(p1_draw_x, p1_draw_y, 128, 64, 0x0000);
					LCD_BitmapPartialTransparent(p1_draw_x, p1_draw_y, 128, 64, car_sel, p1_char * 128, 0, 768, BIKE_TRANSP);

					if (p1_ready) LCD_Print("  READY  ", 40, 160, 1, 0x07E0, 0x0000);
					else LCD_Print("         ", 40, 160, 1, 0x0000, 0x0000);
				}

				if (update_p2) {
					FillRect(p2_draw_x, p2_draw_y, 128, 64, 0x0000);
					LCD_BitmapPartialTransparent(p2_draw_x, p2_draw_y, 128, 64, car_sel, (p2_char + 3) * 128, 0, 768, BIKE_TRANSP);

					if (p2_ready) LCD_Print("  READY  ", 200, 160, 1, 0x07E0, 0x0000);
					else LCD_Print("         ", 200, 160, 1, 0x0000, 0x0000);
				}

				HAL_Delay(50);
			}

			const uint16_t* p1_choices[3] = {bike_blue, bike_samflyn, bike_kevinflyn};
			const uint16_t* p2_choices[3] = {bike_orange, bike_clu, bike_ares};

			const uint16_t* p1_icon_choices[3] = {icon_blue, icon_sam, icon_flyn};
			const uint16_t* p2_icon_choices[3] = {icon_orange, icon_clu, icon_ares};
			p1_icon = p1_icon_choices[p1_char];
			p2_icon = p2_icon_choices[p2_char];

			p1_selected_sheet = p1_choices[p1_char];
			p2_selected_sheet = p2_choices[p2_char];

			HAL_Delay(1000);
		}

	  val = snprintf(msg, sizeof(msg), "%u", 0);
	  HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);

	  LCD_Bitmap(0, 0, 320, 240, arena_bg);

	  struct_mensaje snap = datosJugadores;
	  uint8_t p_j1 = snap.j1_no;
	  uint8_t p_j2 = snap.j2_no;

	  memset(arena_map, 0, sizeof(arena_map));

	  val = snprintf(msg, sizeof(msg), "%u", 4);
	  HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);

	  Bike bike = {
		  .x           = BIKE_X_MIN,
		  .y           = BIKE_Y_MIN,
		  .prev_x      = BIKE_X_MIN,
		  .prev_y      = BIKE_Y_MIN,
		  .dir         = DIR_RIGHT,
		  .speed       = BIKE_SPEED_NORMAL,
		  .sheet       = p1_selected_sheet,
		  .transp      = BIKE_TRANSP,
		  .lives	   = 3,
		  .icon = p1_icon,
		  .char_index = p1_char,
	  };

	  Bike bike2 = {
		  .x           = BIKE_X_MAX,
		  .y           = BIKE_Y_MAX,
		  .prev_x      = BIKE_X_MAX,
		  .prev_y      = BIKE_Y_MAX,
		  .dir         = DIR_LEFT,
		  .speed       = BIKE_SPEED_NORMAL,
		  .sheet       = p2_selected_sheet,
		  .transp      = BIKE_TRANSP,
		  .lives	   = 3,
		  .icon = p2_icon,
		  .char_index = p2_char + 3,
	  };

	  p1_trace_color = bike.sheet[1928];
	  p2_trace_color = bike2.sheet[1928];
	  bike.trail_color = p1_trace_color;
	  bike2.trail_color = p2_trace_color;


	  LCD_SpriteOverBg(bike.x, bike.y, BIKE_W, BIKE_H,
					   bike.sheet, 8, (int)bike.dir, 0, 0,
					   bike.transp, arena_bg, 320);

	  LCD_SpriteOverBg(bike2.x, bike2.y, BIKE_W, BIKE_H,
					   bike2.sheet, 8, (int)bike2.dir, 0, 0,
					   bike2.transp, arena_bg, 320);

	  Draw_Dashboard(&bike, &bike2);

	  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

	  val = snprintf(msg, sizeof(msg), "%u", 3);
	  HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);
	  HAL_Delay(2000);

	while (1) {

		struct_mensaje snap = datosJugadores;

		input_to_bike(snap.j1_x, snap.j1_y, snap.j1_yes, snap.j1_no, &bike.dir,
				&bike.speed);
		input_to_bike(snap.j2_x, snap.j2_y, snap.j2_yes, snap.j2_no, &bike2.dir,
				&bike2.speed);

		bike.prev_x = bike.x;
		bike2.prev_x = bike2.x;
		bike.prev_y = bike.y;
		bike2.prev_y = bike2.y;

		// PLAYER 1
		int steps1 = bike.speed / 2;
		for (int s = 0; s < steps1; s++) {
		    int screen_cx = bike.x + 7;
		    int screen_cy = bike.y + 7;

		    int mx1 = (screen_cx - ARENA_X0) / 2;
		    int my1 = (screen_cy - ARENA_Y0) / 2;

		    if (mx1 >= 0 && mx1 < ARENA_W_TILES && my1 >= 0 && my1 < ARENA_H_TILES) arena_map[my1][mx1] = 1;

		    switch (bike.dir) {
		        case DIR_RIGHT: bike.x += 2; break;
		        case DIR_LEFT:  bike.x -= 2; break;
		        case DIR_DOWN:  bike.y += 2; break;
		        case DIR_UP:    bike.y -= 2; break;
		    }
		}

		// PLAYER 2
		int steps2 = bike2.speed / 2;
		for (int s = 0; s < steps2; s++) {
		    int screen_cx2 = bike2.x + 7;
		    int screen_cy2 = bike2.y + 7;

		    int mx2 = (screen_cx2 - ARENA_X0) / 2;
		    int my2 = (screen_cy2 - ARENA_Y0) / 2;

		    if (mx2 >= 0 && mx2 < ARENA_W_TILES && my2 >= 0 && my2 < ARENA_H_TILES) arena_map[my2][mx2] = 2;

		    switch (bike2.dir) {
		        case DIR_RIGHT: bike2.x += 2; break;
		        case DIR_LEFT:  bike2.x -= 2; break;
		        case DIR_DOWN:  bike2.y += 2; break;
		        case DIR_UP:    bike2.y -= 2; break;
		    }
		}

		// --- COLLISION DETECTION ---
		int p1_crashed = 0;
		int p2_crashed = 0;

		// 1. Check if they hit the arena walls (Bounds Check)
		if (bike.x < BIKE_X_MIN || bike.x > BIKE_X_MAX || bike.y < BIKE_Y_MIN || bike.y > BIKE_Y_MAX) p1_crashed = 1;
		if (bike2.x < BIKE_X_MIN || bike2.x > BIKE_X_MAX || bike2.y < BIKE_Y_MIN || bike2.y > BIKE_Y_MAX) p2_crashed = 1;

		// 2. Check if they hit a trace
		if (check_trace_collision(&bike)) p1_crashed = 1;
		if (check_trace_collision(&bike2)) p2_crashed = 1;

		// 3. Check for head-on collisions
		if (check_head_on(&bike, &bike2)) {
			p1_crashed = 1;
			p2_crashed = 1;
		}

		if (p1_crashed || p2_crashed) {
			if (handle_crash(&bike, &bike2, p1_crashed, p2_crashed)) {
				int winner = 0; // 0 = Tie, 1 = Player 1, 2 = Player 2

				if (bike.lives > 0 && bike2.lives <= 0) winner = 1;
				else if (bike2.lives > 0 && bike.lives <= 0) winner = 2;

			    char win_msg[24];

			    if (winner == 1){
			    	snprintf(win_msg, sizeof(win_msg), "    PROGRAM %d QUALIFIED", winner);
			    	FillRect(30, 95, 260, 50, 0x0000);
			    	LCD_Print(win_msg, 45, 112, 1, 0x07FF, 0x0000);
			    	char_wins[bike.char_index]++;
			    }else if (winner == 2){
			    	snprintf(win_msg, sizeof(win_msg), "    PROGRAM %d QUALIFIED", winner);
			    	FillRect(30, 95, 260, 50, 0x0000);
			    	LCD_Print(win_msg, 45, 112, 1, 0xFD20, 0x0000);
			    	char_wins[bike2.char_index]++;
			    }else if (winner == 0){
			    	snprintf(win_msg, sizeof(win_msg), "     ERR: NO QUALIFIER");
					FillRect(30, 95, 260, 50, 0x0000);
					LCD_Print(win_msg, 45, 112, 1, 0xF800, 0x0000);
			    }

				Save_Scores();

			    val = snprintf(msg, sizeof(msg), "%u", 0);
			    HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);
			    val = snprintf(msg, sizeof(msg), "%u", 7);
			    HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);
			    while (!datosJugadores.j1_yes && !datosJugadores.j1_no)
			        HAL_Delay(50);
			    break;
			}
			continue;
		}

		// Restaurar fondo y volver a mostrar motos
		LCD_RestoreBgDelta(bike.prev_x, bike.prev_y, bike.x, bike.y,
		BIKE_W, BIKE_H, arena_bg, 320, 0, 0, 0, 0, bike.trail_color);

		LCD_SpriteOverBg(bike.x, bike.y, BIKE_W, BIKE_H, bike.sheet, 8,
				(int) bike.dir, 0, 0, bike.transp, arena_bg, 320);

		LCD_RestoreBgDelta(bike2.prev_x, bike2.prev_y, bike2.x, bike2.y,
		BIKE_W, BIKE_H, arena_bg, 320, 0, 0, 0, 0, bike2.trail_color);

		LCD_SpriteOverBg(bike2.x, bike2.y, BIKE_W, BIKE_H, bike2.sheet, 8,
				(int) bike2.dir, 0, 0, bike2.transp, arena_bg, 320);


		if ((p_j1 != snap.j1_no) || (p_j2 != snap.j2_no)) {

			if ((snap.j1_no == 0) && (snap.j2_no == 0)) {
				val = snprintf(msg, sizeof(msg), "%u", 4);
				HAL_UART_Transmit(&huart3, (uint8_t*) msg, val, HAL_MAX_DELAY);
				//HAL_UART_Transmit(&huart2, (uint8_t*) msg, val, HAL_MAX_DELAY);
			} else if ((p_j1 != 1) || (p_j2 != 1)) {
				val = snprintf(msg, sizeof(msg), "%u", 5);
				HAL_UART_Transmit(&huart3, (uint8_t*) msg, val, HAL_MAX_DELAY);
				//HAL_UART_Transmit(&huart2, (uint8_t*) msg, val, HAL_MAX_DELAY);
			}

			p_j1 = snap.j1_no;
			p_j2 = snap.j2_no;
		}

		HAL_Delay(15);
	}  // end inner game loop

  } // end outer restart loop
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LCD_RESET_Pin */
  GPIO_InitStruct.Pin = LCD_RESET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(LCD_RESET_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LCD_DC_Pin */
  GPIO_InitStruct.Pin = LCD_DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(LCD_DC_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LCD_CS_Pin SD_CS_Pin */
  GPIO_InitStruct.Pin = LCD_CS_Pin|SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        if (!sincronizado) {
            if (rx_byte == 0xAA) {
                sincronizado = 1;
                HAL_UART_Receive_IT(&huart1, rx_payload, 8);
            } else {
                HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
            }
        } else {
            /* Full 8-byte payload in hand. Commit and go back to hunting. */
            memcpy((void *)&datosJugadores, rx_payload, sizeof(datosJugadores));
            sincronizado = 0;

            memcpy(dbg_copy, rx_payload, 8);
            dbg_pending = 1;

            HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
        }
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1) {
	}
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
