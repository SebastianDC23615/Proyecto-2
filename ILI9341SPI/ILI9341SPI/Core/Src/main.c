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

typedef struct {
    int             x, y;          /* current top-left position */
    int             prev_x, prev_y;/* previous frame, used for delta-restore */
    BikeDir         dir;           /* facing == direction of motion */
    int             speed;         /* pixels per frame */
    const uint16_t *sheet;         /* 8-column sprite sheet for this character */
    uint16_t        transp;        /* transparent color key for this sheet */
} Bike;

/* Hitbox rectangle expressed as an offset from the sprite top-left
 * plus its own width and height. One instance per direction (see
 * hitbox_for_dir[] in USER CODE 0). */
typedef struct {
    int off_x, off_y;
    int w, h;
} HitboxRect;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Playfield: inclusive pixel bounds the bike's body must stay inside. */
#define ARENA_X0      3
#define ARENA_Y0      41
#define ARENA_X1      316
#define ARENA_Y1      236

#define BIKE_W        32
#define BIKE_H        32
#define BIKE_TRANSP   0x0263

/* Collision hitbox: narrow rectangle centered in the 32x32 sprite box.
 * The LONG axis (32) always runs with the direction of travel, the SHORT
 * axis (14) is perpendicular and inset 9 px from each sprite edge.
 * Used by trail laying and collision — NOT by rendering (the sprite is
 * still drawn as a full 32x32 composite). */
#define HITBOX_LONG   BIKE_W                          /* 32, along heading */
#define HITBOX_SHORT  14                              /* perp. to heading  */
#define HITBOX_INSET  ((BIKE_W - HITBOX_SHORT) / 2)   /* = 9               */

/* Bounds for the bike's sprite top-left so its body stays in the arena.
 * Because the hitbox's long axis (32) equals BIKE_W, these numbers are
 * identical whether you reason about the sprite box or the hitbox's
 * leading edge along the direction of travel. */
#define BIKE_X_MIN    ARENA_X0
#define BIKE_Y_MIN    ARENA_Y0
#define BIKE_X_MAX    (ARENA_X1 - BIKE_W + 1)   /* 316 - 32 + 1 = 285 */
#define BIKE_Y_MAX    (ARENA_Y1 - BIKE_H + 1)   /* 236 - 32 + 1 = 205 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
extern const uint16_t arena_bg[];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Hitbox geometry per facing. Indexed by BikeDir (UP=0, LEFT=1, DOWN=2,
 * RIGHT=3) so a lookup is just hitbox_for_dir[bike.dir]. Kept read-only
 * in flash. When you add a new sprite that needs a different hitbox,
 * change these constants — not the per-frame code. */
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
  /* USER CODE BEGIN 2 */

	  LCD_Init();
	  LCD_Bitmap(0, 0, 320, 240, arena_bg);

	  /* Single-bike square-loop test. No input, no collisions, no trail yet.
	   * Designed so a future second bike is just another Bike struct, and a
	   * future input layer only needs to overwrite bike.dir each frame. */
	  Bike bike = {
		  .x      = BIKE_X_MIN,
		  .y      = BIKE_Y_MIN,
		  .prev_x = BIKE_X_MIN,
		  .prev_y = BIKE_Y_MIN,
		  .dir    = DIR_RIGHT,
		  .speed  = 2,
		  .sheet  = bike_kevinflyn,
		  .transp = BIKE_TRANSP,
	  };

	  /* Square path: list of corners to visit in order, each paired with the
	   * direction the bike should turn to AFTER it arrives. Cycling target
	   * with (target+1)&3 makes the bike loop forever. */
	  static const struct { int x, y; BikeDir next_dir; } corners[4] = {
		  { BIKE_X_MAX, BIKE_Y_MIN, DIR_DOWN  },  /* top-right    -> turn down  */
		  { BIKE_X_MAX, BIKE_Y_MAX, DIR_LEFT  },  /* bottom-right -> turn left  */
		  { BIKE_X_MIN, BIKE_Y_MAX, DIR_UP    },  /* bottom-left  -> turn up    */
		  { BIKE_X_MIN, BIKE_Y_MIN, DIR_RIGHT },  /* top-left     -> turn right */
	  };
	  int target = 0;

	  /* Paint the bike once at its spawn so frame 1's restore has something to
	   * work against (otherwise the first delta-restore would be a no-op and
	   * the very first sprite draw would still be correct, but this keeps the
	   * visual symmetric with later frames). */
	  LCD_SpriteOverBg(bike.x, bike.y, BIKE_W, BIKE_H,
					   bike.sheet, 8, (int)bike.dir, 0, 0,
					   bike.transp, arena_bg, 320);


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

	while (1) {
		/* 1. Remember where we were so the delta-restore knows what strip
		 *    of background just got uncovered. */
		bike.prev_x = bike.x;
		bike.prev_y = bike.y;

		/* 2. Advance one step in the current facing. */
		switch (bike.dir) {
			case DIR_RIGHT: bike.x += bike.speed; break;
			case DIR_LEFT:  bike.x -= bike.speed; break;
			case DIR_DOWN:  bike.y += bike.speed; break;
			case DIR_UP:    bike.y -= bike.speed; break;
		}

		/* 3. If we just reached or overshot the next corner, snap exactly
		 *    onto it and rotate to the next side of the square. */
		int reached = 0;
		switch (bike.dir) {
			case DIR_RIGHT:
				if (bike.x >= corners[target].x) { bike.x = corners[target].x; reached = 1; }
				break;
			case DIR_LEFT:
				if (bike.x <= corners[target].x) { bike.x = corners[target].x; reached = 1; }
				break;
			case DIR_DOWN:
				if (bike.y >= corners[target].y) { bike.y = corners[target].y; reached = 1; }
				break;
			case DIR_UP:
				if (bike.y <= corners[target].y) { bike.y = corners[target].y; reached = 1; }
				break;
		}
		if (reached) {
			bike.dir = corners[target].next_dir;
			target   = (target + 1) & 3;
		}

		/* 4. Repaint: restore only the freshly-uncovered background strip,
		 *    then composite the sprite at the new position with the new
		 *    facing. (int)bike.dir is the sprite-sheet column. */
		LCD_RestoreBgDelta(bike.prev_x, bike.prev_y, bike.x, bike.y,
						   BIKE_W, BIKE_H, arena_bg, 320);
		LCD_SpriteOverBg(bike.x, bike.y, BIKE_W, BIKE_H,
						 bike.sheet, 8, (int)bike.dir, 0, 0,
						 bike.transp, arena_bg, 320);

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
