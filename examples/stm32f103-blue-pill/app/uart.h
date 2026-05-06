#ifndef UART_H
#define UART_H

#include <stm32f1xx_hal.h>
#include <pulse/ring_buffer.h>

#include <etl/string_view.h>
#include <etl/format.h>


extern "C" void
USART1_IRQHandler();

class Uart {
public:
    class WriteIterator {
    public:
        WriteIterator(const WriteIterator &) = default;

        WriteIterator(Uart &uart):
            uart(&uart)
        {
            GetRegion();
        }

        uint8_t &
        operator *()
        {
            if (writeSize) {
                return *writePtr;
            } else {
                return sink;
            }
        }

        WriteIterator &
        operator++(int)
        {
            if (writeSize) {
                writeSize--;
                writePtr++;
                uart->buffer->CommitWrite(1);
                uart->CommitWrite();
            } else {
                GetRegion();
            }
            return *this;
        }

        WriteIterator &
        operator++()
        {
            return (*this)++;
        }

    private:
        Uart *uart;
        uint8_t *writePtr = nullptr;
        size_t writeSize = 0;
        uint8_t sink;

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
    Format(etl::format_string<Args...> fmt, Args&&... args)
    {
        etl::format_to(WriteIterator(*this), fmt, etl::forward<Args>(args)...);
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
