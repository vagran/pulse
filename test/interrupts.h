#ifndef INTERRUPTS_H
#define INTERRUPTS_H

/// Use to simulate ISR from dedicated thread.
void
IsrEnter();

void
IsrExit();

/** Define simulated ISR execution scope. */
class IsrGuard {
public:
    bool acquired = false;

    IsrGuard(const IsrGuard &) = delete;

    IsrGuard(bool acquire = true)
    {
        if (acquire) {
            IsrEnter();
            acquired = true;
        }
    }

    IsrGuard(IsrGuard &&other):
        acquired(other.acquired)
    {
        other.acquired = false;
    }

    ~IsrGuard()
    {
        Exit();
    }

    void
    Exit()
    {
        if (acquired) {
            IsrExit();
            acquired = false;
        }
    }
};

#endif /* INTERRUPTS_H */
