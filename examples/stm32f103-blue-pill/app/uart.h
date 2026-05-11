#ifndef UART_H
#define UART_H

#include <stm32f1xx_hal.h>
#include <pulse/ring_buffer.h>
#include <pulse/format.h>


extern "C" void
USART1_IRQHandler();

class Uart {
public:
    class OutputStream: public pulse::fmt::OutputStream {
    public:
        OutputStream(const OutputStream &) = default;

        OutputStream(Uart &uart):
            uart(uart)
        {}

        virtual void
        WriteChar(char c) override
        {
            uart.WriteChar(c);
        }

    private:
        Uart &uart;
    };

    Uart()
    {}

    void
    Initialize(int baudRate, uint8_t *buffer, size_t bufferSize);

    void
    WriteChar(char c);

    void
    Write(etl::string_view s);

    void
    WriteCharSync(char c);

    template<class... Args>
    void
    Format(etl::string_view fmt, Args&&... args)
    {
        auto stream = OutputStream(*this);
        pulse::fmt::FormatTo(stream, fmt, etl::forward<Args>(args)...);
    }

    /** Flush current buffer in case of Panic engaged. Should be called with interrupts disabled. */
    void
    PanicFlush();

private:
    friend void USART1_IRQHandler();

    UART_HandleTypeDef h;

    union {
        pulse::RingBuffer<uint8_t> buffer;
    };

    void
    CommitWrite();
};

extern Uart uart;

#endif /* UART_H */
