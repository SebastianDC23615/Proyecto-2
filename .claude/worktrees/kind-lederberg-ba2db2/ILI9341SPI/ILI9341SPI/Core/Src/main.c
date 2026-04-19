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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ili9341.h"
#include "bitmaps.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Direction values are also the sprite-sheet column index for that facing,
 * so casting (int)BikeDir straight into LCD_SpriteOverBg(..., index, ...)
 * picks the correct frame. Frame layout: 0=UP 1=LEFT 2=DOWN 3=RIGHT. */
typedef enum {
    DIR_UP    = 0,
    DIR_LEFT  = 1,
    DIR_DOWN  = 2,
    DIR_RIGHT = 3
} BikeDir;

/* Head-based bike state. The "head" is the leading pixel at the front
 * face of the 16x16 sprite; everything (pivot on turn, hitbox, trace
 * offset) is expressed relative to the head. The sprite's top-left is
 * computed from the head each frame via head_to_topleft(). */
typedef struct {
    int             head_x, head_y;      /* head pixel (leading edge) */
    int             prev_tl_x, prev_tl_y;/* previous frame sprite top-left */
    BikeDir         dir;
    int             speed;               /* pixels per frame */
    const uint16_t *sheet;
    uint16_t        transp;
    uint8_t         player_id;           /* 1 = P1, 2 = P2 — index into trail_colors */
    uint8_t         state;               /* 0 = alive, 1 = dead */
} Bike;

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
/* Playfield: inclusive pixel bounds the bike's body must stay inside. */
#define ARENA_X0      3
#define ARENA_Y0      41
#define ARENA_X1      316
#define ARENA_Y1      236

#define BIKE_W        16
#define BIKE_H        16
#define BIKE_TRANSP   0x0263 // #004D19

/* Head-based geometry (see helpers in USER CODE 0). */
#define HEAD_HALF      7    /* (BIKE_W/2 - 1) — perp distance head→sprite edge */
#define TRACE_OFFSET  14    /* pixels BEHIND head where trace is stamped       */

#define JOY_CENTER       127
#define JOY_DEADZONE      80

#define BIKE_SPEED_NORMAL  1
#define BIKE_SPEED_FAST    3

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
extern const uint16_t arena_bg[];
const uint64_t traces[];

/* UART receive state machine buffers, consumed by HAL_UART_RxCpltCallback. */
uint8_t           rx_byte;            /* 1-byte slot for sync hunt     */
uint8_t           rx_payload[8];      /* 8-byte slot for payload read  */
volatile uint8_t  sincronizado = 0;   /* 0 = waiting for 0xAA, 1 = reading payload */
volatile struct_mensaje datosJugadores;  /* latest decoded snapshot    */

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
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Full-screen collision map: 1 byte per pixel.
 *   0 = empty, 1 = P1 trail, 2 = P2 trail.
 * Indexed as arena_map[x][y] in screen coords. 320*240 = 76800 bytes —
 * MUST be static global, do not place on stack. */
static uint8_t arena_map[320][240];

/* Trail color lookup. Index 0 is unused sentinel.
 *   1 = P1 cyan (Flynn)
 *   2 = P2 orange (Ares/Clu) */
static const uint16_t trail_colors[3] = { 0x0000, 0x07FF, 0xFD20 };

/* Head pixel convention:
 *   DIR_RIGHT → rightmost center px    DIR_LEFT → leftmost center px
 *   DIR_UP    → topmost center px      DIR_DOWN → bottommost center px
 * head_to_topleft computes the sprite's top-left so that the 16×16 box
 * has the head at its front face and HEAD_HALF (=7) offset on the
 * perpendicular axis. */
static inline void head_to_topleft(int hx, int hy, BikeDir dir,
                                   int *tl_x, int *tl_y) {
    switch (dir) {
        case DIR_RIGHT: *tl_x = hx - (BIKE_W - 1); *tl_y = hy - HEAD_HALF;    break;
        case DIR_LEFT:  *tl_x = hx;                *tl_y = hy - HEAD_HALF;    break;
        case DIR_UP:    *tl_x = hx - HEAD_HALF;    *tl_y = hy;                break;
        case DIR_DOWN:  *tl_x = hx - HEAD_HALF;    *tl_y = hy - (BIKE_H - 1); break;
        default:        *tl_x = hx;                *tl_y = hy;                break;
    }
}

/* Screen coord where the trace should be stamped, TRACE_OFFSET (=14) px
 * BEHIND the head, on the perpendicular=head_y (or head_x) line. */
static inline void trace_pos(int hx, int hy, BikeDir dir,
                             int *tx, int *ty) {
    switch (dir) {
        case DIR_RIGHT: *tx = hx - TRACE_OFFSET; *ty = hy;                break;
        case DIR_LEFT:  *tx = hx + TRACE_OFFSET; *ty = hy;                break;
        case DIR_UP:    *tx = hx;                *ty = hy + TRACE_OFFSET; break;
        case DIR_DOWN:  *tx = hx;                *ty = hy - TRACE_OFFSET; break;
        default:        *tx = hx;                *ty = hy;                break;
    }
}

/* Stamp a 2×2 trace block at (x,y) into arena_map AND display.
 * player_id: 1 = P1, 2 = P2 (indexes trail_colors[]). */
static void Leave_Trace(int x, int y, uint8_t player_id) {
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            int sx = x + dx;
            int sy = y + dy;
            if (sx >= ARENA_X0 && sx <= ARENA_X1 &&
                sy >= ARENA_Y0 && sy <= ARENA_Y1)
                arena_map[sx][sy] = player_id;
        }
    }
    int sx = x, sy = y, sw = 2, sh = 2;
    if (sx < ARENA_X0) { sw -= (ARENA_X0 - sx); sx = ARENA_X0; }
    if (sy < ARENA_Y0) { sh -= (ARENA_Y0 - sy); sy = ARENA_Y0; }
    if (sx + sw - 1 > ARENA_X1) sw = ARENA_X1 - sx + 1;
    if (sy + sh - 1 > ARENA_Y1) sh = ARENA_Y1 - sy + 1;
    if (sw > 0 && sh > 0)
        FillRect(sx, sy, sw, sh, trail_colors[player_id]);
}

/* 4×4 head hitbox check. Returns 1 if the box hits an arena wall or any
 * trace cell not owned by own_id. own_id lets a bike pass through its
 * own fresh trail (safety net — the head leads TRACE_OFFSET pixels
 * ahead of its own stamps, so in practice this rarely triggers). */
static int check_head_collision(int hx, int hy, uint8_t own_id) {
    for (int dy = -1; dy <= 2; dy++) {
        for (int dx = -1; dx <= 2; dx++) {
            int cx = hx + dx;
            int cy = hy + dy;
            if (cx < ARENA_X0 || cx > ARENA_X1 ||
                cy < ARENA_Y0 || cy > ARENA_Y1)
                return 1;                  /* wall */
            uint8_t v = arena_map[cx][cy];
            if (v != 0 && v != own_id)
                return 1;                  /* enemy trail */
        }
    }
    return 0;
}

/* Map one joystick's X/Y + button to a desired direction and speed.
 * Returns the bike's CURRENT dir unchanged if in deadzone or reversal. */
static inline void input_to_bike(uint8_t jx, uint8_t jy,
                                 uint8_t btn_fast,
                                 BikeDir *io_dir, int *out_speed) {
    int dx = (int)jx - JOY_CENTER;
    int dy = (int)jy - JOY_CENTER;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;

    BikeDir requested = *io_dir;
    if (ax >= JOY_DEADZONE || ay >= JOY_DEADZONE) {
        if (ax > ay)      requested = (dx > 0) ? DIR_RIGHT : DIR_LEFT;
        else if (ay > ax) requested = (dy > 0) ? DIR_UP    : DIR_DOWN;
    }

    BikeDir cur = *io_dir;
    int is_reverse =
        (cur == DIR_UP    && requested == DIR_DOWN)  ||
        (cur == DIR_DOWN  && requested == DIR_UP)    ||
        (cur == DIR_LEFT  && requested == DIR_RIGHT) ||
        (cur == DIR_RIGHT && requested == DIR_LEFT);
    if (!is_reverse) *io_dir = requested;

    *out_speed = btn_fast ? BIKE_SPEED_FAST : BIKE_SPEED_NORMAL;
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
  /* USER CODE BEGIN 2 */

	  LCD_Init();
	  LCD_Bitmap(0, 0, 320, 240, arena_bg);

	  struct_mensaje snap = datosJugadores;
	  uint8_t p_j1 = snap.j1_no;
	  uint8_t p_j2 = snap.j2_no;

	  char msg[3];
	  uint8_t val = 0;
	  val = snprintf(msg, sizeof(msg), "%u", 4);
	  HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);


	  /* Zero the arena collision map at boot. */
	  memset(arena_map, 0, sizeof(arena_map));

	  /* Bike 1 spawns near the left edge, heading RIGHT.
	   * Head is the rightmost center pixel of the 16×16 sprite, so the
	   * sprite top-left is at (head_x-15, head_y-7). */
	  Bike bike = {
	      .head_x     = ARENA_X0 + (BIKE_W - 1),      /* 3 + 15 = 18 */
	      .head_y     = ARENA_Y0 + HEAD_HALF + 60,    /* somewhere mid-arena */
	      .prev_tl_x  = 0,
	      .prev_tl_y  = 0,
	      .dir        = DIR_RIGHT,
	      .speed      = BIKE_SPEED_NORMAL,
	      .sheet      = bike_kevinflyn,
	      .transp     = BIKE_TRANSP,
	      .player_id  = 1,
	      .state      = 0,
	  };
	  head_to_topleft(bike.head_x, bike.head_y, bike.dir,
	                  &bike.prev_tl_x, &bike.prev_tl_y);

	  /* Bike 2 spawns near the right edge, heading LEFT. */
	  Bike bike2 = {
	      .head_x     = ARENA_X1 - (BIKE_W - 1),      /* 316 - 15 = 301 */
	      .head_y     = ARENA_Y0 + HEAD_HALF + 60,
	      .prev_tl_x  = 0,
	      .prev_tl_y  = 0,
	      .dir        = DIR_LEFT,
	      .speed      = BIKE_SPEED_NORMAL,
	      .sheet      = bike_ares,
	      .transp     = BIKE_TRANSP,
	      .player_id  = 2,
	      .state      = 0,
	  };
	  head_to_topleft(bike2.head_x, bike2.head_y, bike2.dir,
	                  &bike2.prev_tl_x, &bike2.prev_tl_y);

	  /* Initial paint so the first frame's delta-restore has something to
	   * work against. Trace-aware render functions get arena_map + colors. */
	  LCD_SpriteOverBg(bike.prev_tl_x, bike.prev_tl_y, BIKE_W, BIKE_H,
	                   bike.sheet, 8, (int)bike.dir, 0, 0,
	                   bike.transp, arena_bg, 320,
	                   arena_map, trail_colors);
	  LCD_SpriteOverBg(bike2.prev_tl_x, bike2.prev_tl_y, BIKE_W, BIKE_H,
	                   bike2.sheet, 8, (int)bike2.dir, 0, 0,
	                   bike2.transp, arena_bg, 320,
	                   arena_map, trail_colors);

	  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

	  while (1) {
	      /* 1. Pull latest joystick snapshot into locals. The volatile read
	       *    is a single-frame latch — if a UART IRQ fires mid-frame, we
	       *    use the OLD snapshot for this frame and pick up the new one
	       *    next frame, which is fine at 50 ms UART cadence vs 15 ms frames. */
	      struct_mensaje snap = datosJugadores;

	      /* --- 2. Input (only while alive) --- */
	      if (bike.state == 0)
	          input_to_bike(snap.j1_x, snap.j1_y, snap.j1_no,
	                        &bike.dir, &bike.speed);
	      if (bike2.state == 0)
	          input_to_bike(snap.j2_x, snap.j2_y, snap.j2_no,
	                        &bike2.dir, &bike2.speed);

	      /* --- 3. Step-by-step movement (1 px per step, 'speed' steps/frame).
	       * This prevents tunneling at high speeds AND ensures the trail is
	       * continuous (no gaps) regardless of speed. Each step:
	       *   a) advance head 1 px
	       *   b) check 4×4 head hitbox
	       *   c) stamp 2×2 trace 14 px BEHIND the head */
	      if (bike.state == 0) {
	          for (int step = 0; step < bike.speed; step++) {
	              switch (bike.dir) {
	                  case DIR_RIGHT: bike.head_x++; break;
	                  case DIR_LEFT:  bike.head_x--; break;
	                  case DIR_DOWN:  bike.head_y++; break;
	                  case DIR_UP:    bike.head_y--; break;
	              }
	              if (check_head_collision(bike.head_x, bike.head_y, bike.player_id)) {
	                  bike.state = 1;
	                  /* back off 1 px so sprite rests just short of the wall */
	                  switch (bike.dir) {
	                      case DIR_RIGHT: bike.head_x--; break;
	                      case DIR_LEFT:  bike.head_x++; break;
	                      case DIR_DOWN:  bike.head_y--; break;
	                      case DIR_UP:    bike.head_y++; break;
	                  }
	                  break;
	              }
	              int tx, ty;
	              trace_pos(bike.head_x, bike.head_y, bike.dir, &tx, &ty);
	              Leave_Trace(tx, ty, bike.player_id);
	          }
	      }

	      if (bike2.state == 0) {
	          for (int step = 0; step < bike2.speed; step++) {
	              switch (bike2.dir) {
	                  case DIR_RIGHT: bike2.head_x++; break;
	                  case DIR_LEFT:  bike2.head_x--; break;
	                  case DIR_DOWN:  bike2.head_y++; break;
	                  case DIR_UP:    bike2.head_y--; break;
	              }
	              if (check_head_collision(bike2.head_x, bike2.head_y, bike2.player_id)) {
	                  bike2.state = 1;
	                  switch (bike2.dir) {
	                      case DIR_RIGHT: bike2.head_x--; break;
	                      case DIR_LEFT:  bike2.head_x++; break;
	                      case DIR_DOWN:  bike2.head_y--; break;
	                      case DIR_UP:    bike2.head_y++; break;
	                  }
	                  break;
	              }
	              int tx, ty;
	              trace_pos(bike2.head_x, bike2.head_y, bike2.dir, &tx, &ty);
	              Leave_Trace(tx, ty, bike2.player_id);
	          }
	      }

	      /* --- 4. Render (trace-aware) --- */
	      int b1_tl_x, b1_tl_y, b2_tl_x, b2_tl_y;
	      head_to_topleft(bike.head_x,  bike.head_y,  bike.dir,  &b1_tl_x, &b1_tl_y);
	      head_to_topleft(bike2.head_x, bike2.head_y, bike2.dir, &b2_tl_x, &b2_tl_y);

	      LCD_RestoreBgDelta(bike.prev_tl_x, bike.prev_tl_y, b1_tl_x, b1_tl_y,
	                         BIKE_W, BIKE_H, arena_bg, 320,
	                         arena_map, trail_colors);
	      LCD_RestoreBgDelta(bike2.prev_tl_x, bike2.prev_tl_y, b2_tl_x, b2_tl_y,
	                         BIKE_W, BIKE_H, arena_bg, 320,
	                         arena_map, trail_colors);

	      LCD_SpriteOverBg(b1_tl_x, b1_tl_y, BIKE_W, BIKE_H,
	                       bike.sheet, 8, (int)bike.dir, 0, 0,
	                       bike.transp, arena_bg, 320,
	                       arena_map, trail_colors);
	      LCD_SpriteOverBg(b2_tl_x, b2_tl_y, BIKE_W, BIKE_H,
	                       bike2.sheet, 8, (int)bike2.dir, 0, 0,
	                       bike2.transp, arena_bg, 320,
	                       arena_map, trail_colors);

	      bike.prev_tl_x  = b1_tl_x; bike.prev_tl_y  = b1_tl_y;
	      bike2.prev_tl_x = b2_tl_x; bike2.prev_tl_y = b2_tl_y;

	      /*if (dbg_pending) {
	          dbg_pending = 0;
	          char dbg[80];
	          int n = snprintf(dbg, sizeof(dbg),
	              "J1 x=%3u y=%3u A=%u B=%u | J2 x=%3u y=%3u A=%u B=%u\r\n",
	              dbg_copy[4], dbg_copy[5], dbg_copy[0], dbg_copy[1],
	              dbg_copy[6], dbg_copy[7], dbg_copy[2], dbg_copy[3]);
	          HAL_UART_Transmit(&huart2, (uint8_t *)dbg, n, HAL_MAX_DELAY);
	      }*/

	      char msg[3];
	      uint8_t val = 0;

	      if ((p_j1 != snap.j1_no) || (p_j2 != snap.j2_no)) {

	          if ((snap.j1_no == 0) && (snap.j2_no == 0)){
	              val = snprintf(msg, sizeof(msg), "%u", 4);
	              HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);
	              HAL_UART_Transmit(&huart2, (uint8_t *)msg, val, HAL_MAX_DELAY);
	          } else if ((p_j1 != 1) || (p_j2 != 1)){
	              val = snprintf(msg, sizeof(msg), "%u", 5);
	              HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);
	              HAL_UART_Transmit(&huart2, (uint8_t *)msg, val, HAL_MAX_DELAY);
	          }

	          p_j1 = snap.j1_no;
	          p_j2 = snap.j2_no;
	      }


	      /*if ((prev_val == snap.j1_no)) {
	    	  //prev_val = snap.j1_no;
	      }else if (snap.j1_no){
	    	  val = snprintf(msg, sizeof(msg), "%u", 5);
	    	  prev_val = snap.j1_no;
	    	  HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);
	    	  HAL_UART_Transmit(&huart2, (uint8_t *)msg, val, HAL_MAX_DELAY);
	      }else{
	    	  val = snprintf(msg, sizeof(msg), "%u", 4);
	    	  prev_val = snap.j1_no;
	    	  HAL_UART_Transmit(&huart3, (uint8_t *)msg, val, HAL_MAX_DELAY);
	    	  HAL_UART_Transmit(&huart2, (uint8_t *)msg, val, HAL_MAX_DELAY);
	      }*/

	      HAL_Delay(15);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	}
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
  HAL_GPIO_WritePin(GPIOB, LCD_CS_Pin|SD_CS_Pin, GPIO_PIN_RESET);

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
        if (sincronizado == 0) {
            /* Hunting for the 0xAA sync byte. */
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
