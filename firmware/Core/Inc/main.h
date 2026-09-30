/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SENSE_EN_Pin GPIO_PIN_14
#define SENSE_EN_GPIO_Port GPIOC
#define IO16_Pin GPIO_PIN_2
#define IO16_GPIO_Port GPIOF
#define IMU_INT_Pin GPIO_PIN_0
#define IMU_INT_GPIO_Port GPIOA
#define BAT_SENSE_Pin GPIO_PIN_1
#define BAT_SENSE_GPIO_Port GPIOA
#define IO0_Pin GPIO_PIN_2
#define IO0_GPIO_Port GPIOA
#define IO1_Pin GPIO_PIN_3
#define IO1_GPIO_Port GPIOA
#define IO2_Pin GPIO_PIN_4
#define IO2_GPIO_Port GPIOA
#define IO3_Pin GPIO_PIN_5
#define IO3_GPIO_Port GPIOA
#define IO4_Pin GPIO_PIN_6
#define IO4_GPIO_Port GPIOA
#define IO5_Pin GPIO_PIN_7
#define IO5_GPIO_Port GPIOA
#define IO6_Pin GPIO_PIN_0
#define IO6_GPIO_Port GPIOB
#define IO7_Pin GPIO_PIN_1
#define IO7_GPIO_Port GPIOB
#define IO8_Pin GPIO_PIN_8
#define IO8_GPIO_Port GPIOA
#define IO9_Pin GPIO_PIN_6
#define IO9_GPIO_Port GPIOC
#define IO10_Pin GPIO_PIN_13
#define IO10_GPIO_Port GPIOA
#define IO11_Pin GPIO_PIN_14
#define IO11_GPIO_Port GPIOA
#define IO12_Pin GPIO_PIN_15
#define IO12_GPIO_Port GPIOA
#define IO13_Pin GPIO_PIN_3
#define IO13_GPIO_Port GPIOB
#define IO14_Pin GPIO_PIN_4
#define IO14_GPIO_Port GPIOB
#define IO15_Pin GPIO_PIN_5
#define IO15_GPIO_Port GPIOB
#define IO17_Pin GPIO_PIN_8
#define IO17_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
