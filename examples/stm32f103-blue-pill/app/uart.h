#ifndef UART_H
#define UART_H

#include <stm32f1xx_hal.h>
#include <pulse/ring_buffer.h>

#include <etl/string_view.h>


extern "C" void
USART1_IRQHandler();

class Uart {
public:
    class WriteIterator {
    public:
        WriteIterator(const WriteIterator &) = default;

        WriteIterator(pulse::RingBuffer<uint8_t> &buffer):
            buffer(&buffer)
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
                buffer->CommitWrite(1);
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
        pulse::RingBuffer<uint8_t> *buffer;
        uint8_t *writePtr = nullptr;
        size_t writeSize = 0;
        uint8_t sink;

        void
        GetRegion()
        {
            auto region = buffer->GetWriteRegion();
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

    /** Flush current buffer in case of Panic engaged. Should be called with interrupts disabled. */
    void
    PanicFlush();

private:
    friend void USART1_IRQHandler();

    UART_HandleTypeDef h;
    etl::optional<pulse::RingBuffer<uint8_t>> buffer;
};

extern Uart uart;

#endif /* UART_H */
