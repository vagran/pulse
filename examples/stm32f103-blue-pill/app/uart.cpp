#include <uart.h>
#include <panic.h>


Uart uart;


void
Uart::Initialize(int baudRate, size_t bufSize)
{
    //XXX buf
    memset(&h, 0, sizeof(h));

    h.Instance = USART1;
    h.Init.BaudRate = baudRate;
    h.Init.WordLength = UART_WORDLENGTH_8B;
    h.Init.StopBits = UART_STOPBITS_1;
    h.Init.Parity = UART_PARITY_NONE;
    h.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    h.Init.Mode = UART_MODE_TX_RX;
    h.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&h) != HAL_OK) {
        Panic("UART init");
    }
    // __HAL_UART_ENABLE_IT(&h, UART_IT_RXNE);
}

void
Uart::Write(etl::string_view s)
{
    //XXX
    __HAL_UART_ENABLE_IT(&h, UART_IT_TXE);
}

void
Uart::WriteCharSync(uint8_t c)
{
    HAL_UART_Transmit(&h, &c, 1, 0xFFFF);
}

void
Uart::PanicFlush()
{
    //XXX
}

void
USART1_IRQHandler()
{
    if (__HAL_UART_GET_IT_SOURCE(&uart.h, UART_IT_RXNE) &&
        __HAL_UART_GET_FLAG(&uart.h, UART_FLAG_RXNE)) {

        // uint8_t data = uart.h.Instance->DR;
        //XXX
    }

    if (__HAL_UART_GET_IT_SOURCE(&uart.h, UART_IT_TXE) &&
        __HAL_UART_GET_FLAG(&uart.h, UART_FLAG_TXE)) {

        //XXX
        // if (RingBuf_Get(&s_uart1WriteRingBuffer, &data, 1)) {
        //     /* Transmit Data */
        //     uart.h.Instance->DR = ((uint16_t) data & (uint16_t) 0x01FF);
        // } else {
        //     __HAL_UART_DISABLE_IT(&uart.h, UART_IT_TXE);
        // }
    }
}
