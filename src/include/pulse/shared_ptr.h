#ifndef PULSE_SHARED_PTR_H
#define PULSE_SHARED_PTR_H

#include <pulse/debug.h>
#include <etl/atomic.h>
#include <etl/limits.h>


namespace pulse {

namespace details {

template <typename Tr, typename T>
concept SharedPtrTrait = requires(T &obj) {
    // static void AddRef(T &);
    // Adds one reference.
    { Tr::AddRef(obj) } -> etl::same_as<void>;

    // static bool ReleaseRef(T &);
    // Returns true if last reference released.
    { Tr::ReleaseRef(obj) } -> etl::same_as<bool>;

    // static void Delete(T &);
    // Called when last reference released.
    { Tr::Delete(obj) } -> etl::same_as<void>;
};

// Make this class friend if having private `refCounter`.
template <typename T>
struct SharedPtrDefaultTrait {
    static void
    AddRef(T &obj)
    {
        // Initial reference should always be set on construction. After that reference can be added
        // only by already reference object.
        PULSE_ASSERT(obj.refCounter > 0);
        if (obj.refCounter == etl::numeric_limits<decltype(obj.refCounter)>::max()) {
            PULSE_PANIC("SharedPtr reference counter overflow");
        }
        obj.refCounter++;
    }

    /// @return True if last reference released.
    static bool
    ReleaseRef(T &obj)
    {
        PULSE_ASSERT(obj.refCounter != 0);
        return --obj.refCounter == 0;
    }

    static void
    Delete(T &obj)
    {
        delete &obj;
    }
};

// Make this class friend if having private `refCounter`.
template <typename T>
struct SharedPtrDefaultAtomicTrait {
    static void
    AddRef(T &obj)
    {
        auto prevValue = obj.refCounter.fetch_add(1);
        // Initial reference should always be set on construction. After that reference can be added
        // only by already reference object.
        PULSE_ASSERT(prevValue > 0);
        if (prevValue == etl::numeric_limits<decltype(prevValue)>::max()) {
            PULSE_PANIC("SharedPtr reference counter overflow");
        }
    }

    /// @return True if last reference released.
    static bool
    ReleaseRef(T &obj)
    {
        auto prevValue = obj.refCounter.fetch_sub(1);
        PULSE_ASSERT(prevValue != 0);
        return prevValue == 1;
    }

    static void
    Delete(T &obj)
    {
        delete &obj;
    }
};

} // namespace details


/** Intrusive shared pointer. */
template <typename T, details::SharedPtrTrait<T> Trait = details::SharedPtrDefaultAtomicTrait<T>>
class SharedPtr {
public:
    SharedPtr():
        ptr(nullptr)
    {}

    SharedPtr(etl::nullptr_t):
        ptr(nullptr)
    {}

    explicit
    SharedPtr(T *ptr, bool initialRef = false):
        ptr(ptr)
    {
        if (ptr) {
            if (!initialRef) {
                Trait::AddRef(*ptr);
            }
        }
    }

    SharedPtr(const SharedPtr &other):
        SharedPtr(other.ptr)
    {}

    template<typename Y>
    SharedPtr(const SharedPtr<Y> & other):
        SharedPtr(other.ptr)
    {}

    SharedPtr(SharedPtr &&other):
        ptr(other.ptr)
    {
        other.ptr = nullptr;
    }

    template<typename Y>
    SharedPtr(SharedPtr<Y> &&other):
        ptr(other.ptr)
    {
        other.ptr = nullptr;
    }

    SharedPtr &
    operator =(const SharedPtr &other)
    {
        if (&other == this) {
            return *this;
        }
        if (other.ptr) {
            Trait::AddRef(*other.ptr);
        }
        Reset();
        ptr = other.ptr;
        return *this;
    }

    template<typename Y>
    SharedPtr &
    operator =(const SharedPtr<Y> &other)
    {
        if (other.ptr) {
            Trait::AddRef(*other.ptr);
        }
        Reset();
        ptr = other.ptr;
        return *this;
    }

    SharedPtr &
    operator =(SharedPtr &&other)
    {
        if (&other == this) {
            return *this;
        }
        Reset();
        ptr = other.ptr;
        other.ptr = nullptr;
        return *this;
    }

    template<typename Y>
    SharedPtr &
    operator =(SharedPtr<Y> &&other)
    {
        Reset();
        ptr = other.ptr;
        other.ptr = nullptr;
        return *this;
    }

    ~SharedPtr()
    {
        Reset();
    }

    void
    Reset()
    {
        if (ptr) {
            if (Trait::ReleaseRef(*ptr)) {
                Trait::Delete(*ptr);
            }
            ptr = nullptr;
        }
    }

    void
    Swap(SharedPtr &other)
    {
        etl::swap(ptr, other.ptr);
    }

    operator bool() const
    {
        return ptr;
    }

    bool
    operator ==(const SharedPtr &other) const
    {
        return ptr == other.ptr;
    }

    bool
    operator !=(const SharedPtr &other) const
    {
        return ptr != other.ptr;
    }

    bool
    operator ==(etl::nullptr_t) const
    {
        return ptr == nullptr;
    }

    bool
    operator !=(etl::nullptr_t) const
    {
        return ptr != nullptr;
    }

    T &
    operator *() const
    {
        return *ptr;
    }

    T *
    operator ->() const
    {
        return ptr;
    }

    T *
    Get() const
    {
        return ptr;
    }

private:
    T *ptr;
};

} // namespace pulse

#endif /* PULSE_SHARED_PTR_H */
