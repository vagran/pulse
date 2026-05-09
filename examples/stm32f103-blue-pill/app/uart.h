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
            uart(&uart)
        {
            GetRegion();
        }

        virtual void
        WriteChar(char c) override
        {
            if (!writeSize) {
                GetRegion();
            }
            if (writeSize) {
                *writePtr = c;
                writeSize--;
                writePtr++;
                uart->buffer->CommitWrite(1);
                uart->CommitWrite();
            }
        }

    private:
        Uart *uart;
        uint8_t *writePtr = nullptr;
        size_t writeSize = 0;

        void
        GetRegion()
        {
            auto region = uart->buffer->GetWriteRegion();
            if (!region.empty()) {
                writePtr = region.data();
                writeSize = region.size();
            }
        }
    };

    void
    Initialize(int baudRate, uint8_t *buffer, size_t bufferSize);

    void
    Write(etl::string_view s);

    void
    WriteCharSync(uint8_t c);

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
    etl::optional<pulse::RingBuffer<uint8_t>> buffer;

    void
    CommitWrite();
};

extern Uart uart;

#endif /* UART_H */
