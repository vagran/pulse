#ifndef PORT_H
#define PORT_H

// This header should be provided by the port.
#include <pulse_port.h>


#ifdef __cplusplus
extern "C" {
#endif

/** pulsePort_DisableIsr
 * Either macro or function to disable ISRs.
 */
#ifndef pulsePort_DisableIsr
void
pulsePort_DisableIsr();
#endif


/** pulsePort_EnableIsr
 * Either macro or function to enable ISRs.
 */
#ifndef pulsePort_EnableIsr
void
pulsePort_EnableIsr();
#endif

/** pulsePort_EnterCriticalSection
 * Either macro or function to enter scheduler critical section.
 */
#ifndef pulsePort_EnterCriticalSection
void
pulsePort_EnterCriticalSection();
#endif


/** pulsePort_ExitCriticalSection
 * Either macro or function to exit scheduler critical section.
 */
#ifndef pulsePort_ExitCriticalSection
void
pulsePort_ExitCriticalSection();
#endif


/** pulsePort_Sleep
 * Either macro or function to enter low-power mode sleep until next interrupt.
 */
#ifndef pulsePort_Sleep
void
pulsePort_Sleep();
#endif


#ifdef __cplusplus
} // extern "C"

namespace pulse {

/** Helper class to disable ISRs in this class instance scope. */
class IsrGuard {
public:
    bool acquired = true;

    IsrGuard(const IsrGuard &) = delete;

    IsrGuard()
    {
        pulsePort_DisableIsr();
    }

    ~IsrGuard()
    {
        Exit();
    }

    void
    Exit()
    {
        if (acquired) {
            pulsePort_EnableIsr();
            acquired = false;
        }
    }
};

/** Helper class to work in critical section in this class instance scope. */
class CriticalSection {
public:
    bool acquired = true;

    CriticalSection(const IsrGuard &) = delete;

    CriticalSection()
    {
        pulsePort_EnterCriticalSection();
    }

    ~CriticalSection()
    {
        Exit();
    }

    void
    Exit()
    {
        if (acquired) {
            pulsePort_ExitCriticalSection();
            acquired = false;
        }
    }
};

} // namespace pulse

#endif // __cplusplus

#endif /* PORT_H */
