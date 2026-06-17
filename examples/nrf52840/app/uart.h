#ifndef UART_H
#define UART_H

#include <nrfx_uart.h>
#include <pulse/fast_ring_buffer.h>
#include <pulse/format.h>


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
    Initialize(nrf_uart_baudrate_t baudRate, uint8_t *buffer, size_t bufferSize);

    void
    WriteChar(char c);

    void
    Write(etl::string_view s);

    // Should be used only after UART interrupts disabled.
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
    union {
        pulse::RingBuffer<uint8_t> buffer;
    };
    nrfx_uart_t h;
    bool txInProgress = false;

    /** Should be called in critical section. */
    void
    CommitWrite();

    void
    HandleEvent(const nrfx_uart_event_t *e);

    static void
    HandleEvent(const nrfx_uart_event_t *e, void *context);
};

extern Uart uart;

#endif /* UART_H */
