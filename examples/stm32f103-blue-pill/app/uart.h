#ifndef UART_H
#define UART_H

#include <stm32f1xx_hal.h>
#include <etl/string.h>


class Uart {
public:
    void
    Initialize(int baudRate, size_t bufSize = 256);

    void
    Write(etl::string_view s);

    void
    WriteCharSync(uint8_t c);

    /** Flush current buffer in case of Panic engaged. Should be called with interrupts disabled. */
    void
    PanicFlush();

private:
    friend void USART1_IRQHandler();

    UART_HandleTypeDef h;
};

extern Uart uart;

#endif /* UART_H */
