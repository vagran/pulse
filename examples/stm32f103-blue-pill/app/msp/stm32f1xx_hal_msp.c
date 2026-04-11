#include <stm32f1xx_hal.h>

/**
 * @brief  Initializes the Global MSP.
 * @retval None
 */
void
HAL_MspInit(void)
{}

/**
 * @brief  DeInitializes the Global MSP.
 * @retval None
 */
void
HAL_MspDeInit(void)
{}

/**
 * @brief  Initializes the PPP MSP.
 * @retval None
 */
void
HAL_PPP_MspInit(void)
{}

/**
 * @brief  DeInitializes the PPP MSP.
 * @retval None
 */
void
HAL_PPP_MspDeInit(void)
{}

void
HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef init = {0};

    if (huart->Instance == USART1) {
        // TX - PA9, RX - PA10

        /* Enable GPIO TX/RX clock */
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /* Enable USART clock */
        __HAL_RCC_USART1_CLK_ENABLE();

        /* UART TX GPIO pin configuration  */
        init.Pin = GPIO_PIN_9;
        init.Mode = GPIO_MODE_AF_PP;
        init.Pull = GPIO_NOPULL;
        init.Speed = GPIO_SPEED_FREQ_HIGH;

        HAL_GPIO_Init(GPIOA, &init);

        /* UART RX GPIO pin configuration  */
        init.Pin = GPIO_PIN_10;
        init.Mode = GPIO_MODE_AF_INPUT;

        HAL_GPIO_Init(GPIOA, &init);

        /* NVIC for USART1 */
        HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
}
