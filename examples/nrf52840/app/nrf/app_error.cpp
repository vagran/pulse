#include <app_error.h>
#include <panic.h>

void
app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
    // switch (id) {
    // case NRF_FAULT_ID_SDK_ASSERT: {
    //     assert_info_t * p_info = (assert_info_t *)info;
    //     NRF_LOG_ERROR("ASSERTION FAILED at %s:%u",
    //                     p_info->p_file_name,
    //                     p_info->line_num);
    //     break;
    // }
    // case NRF_FAULT_ID_SDK_ERROR:
    // {
    //     error_info_t * p_info = (error_info_t *)info;
    //     NRF_LOG_ERROR("ERROR %u [%s] at %s:%u\r\nPC at: 0x%08x",
    //                     p_info->err_code,
    //                     nrf_strerror_get(p_info->err_code),
    //                     p_info->p_file_name,
    //                     p_info->line_num,
    //                     pc);
    //         NRF_LOG_ERROR("End of error report");
    //     break;
    // }
    // default:
    //     NRF_LOG_ERROR("UNKNOWN FAULT at 0x%08X", pc);
    //     break;
    // }
    Panic("SDK App error");
}
