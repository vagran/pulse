#include <uart.h>
#include <panic.h>

#include <pulse/port.h>

#include <nrf_gpio.h>


Uart uart;

void
Uart::Initialize(nrf_uart_baudrate_t baudRate, uint8_t *buffer, size_t bufferSize)
{
    etl::construct_at(&this->buffer, buffer, bufferSize);

    h = nrfx_uart_t NRFX_UART_INSTANCE(0);
    nrfx_uart_config_t config = NRFX_UART_DEFAULT_CONFIG;
    config.baudrate = baudRate;
    config.p_context = this;
    config.pseltxd = NRF_GPIO_PIN_MAP(0, 20);
    if (nrfx_uart_init(&h, &config, &Uart::HandleEvent) != NRF_SUCCESS) {
        Panic("UART init failed");
    }
}

void
Uart::Write(etl::string_view s)
{
    pulse::CriticalSection cs;
    buffer.Write(reinterpret_cast<const uint8_t *>(s.data()), s.size());
    CommitWrite();
}

void
Uart::WriteChar(char c)
{
    pulse::CriticalSection cs;
    buffer.Write(reinterpret_cast<const uint8_t *>(&c), 1);
    CommitWrite();
}

void
Uart::CommitWrite()
{
    if (txInProgress) {
        return;
    }
    etl::span<const uint8_t> data = buffer.GetReadRegion();
    if (data.empty()) {
        return;
    }
    txInProgress = true;
    if (nrfx_uart_tx(&h, data.data(), data.size()) != NRF_SUCCESS) {
        txInProgress = false;
        return;
    }
}

void
Uart::WriteCharSync(char c)
{
    nrf_uart_task_trigger(h.p_reg, NRF_UART_TASK_STARTTX);
    nrf_uart_event_clear(h.p_reg, NRF_UART_EVENT_TXDRDY);
    nrf_uart_txd_set(h.p_reg, c);
    while (!nrf_uart_event_check(h.p_reg, NRF_UART_EVENT_TXDRDY));
    nrf_uart_task_trigger(h.p_reg, NRF_UART_TASK_STOPTX);
}

void
Uart::PanicFlush()
{
    while (true) {
        auto region = this->buffer.GetReadRegion();
        if (region.empty()) {
            break;
        }
        for (uint8_t c: region) {
            WriteCharSync(c);
        }
    }
}

void
Uart::HandleEvent(const nrfx_uart_event_t *e, void *context)
{
    reinterpret_cast<Uart *>(context)->HandleEvent(e);
}

void
Uart::HandleEvent(const nrfx_uart_event_t *e)
{
    if (e->type == NRFX_UART_EVT_TX_DONE) {
        buffer.CommitRead(e->data.rxtx.bytes);
        if (buffer.IsEmpty()) {
            txInProgress = false;
        } else {
            etl::span<const uint8_t> data = buffer.GetReadRegion();
            if (nrfx_uart_tx(&h, data.data(), data.size()) != NRF_SUCCESS) {
                txInProgress = false;
            }
        }
    }
}
