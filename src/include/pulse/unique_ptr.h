#ifndef PULSE_UNIQUE_PTR_H
#define PULSE_UNIQUE_PTR_H

#include <etl/concepts.h>
#include <etl/utility.h>


namespace pulse {

namespace details {

template <typename Tr, typename T>
concept UniquePtrTrait = requires(T *obj) {
    // static void Delete(T *);
    { Tr::Delete(obj) } -> etl::same_as<void>;
};

template <typename T>
struct UniquePtrDefaultTrait {
    static void
    Delete(T *obj)
    {
        delete obj;
    }
};

} // namespace details

/// etl::unique_ptr always stores deleter object, which is not needed in most cases, thus doubling
/// object size (one byte deleter and alignment). So use this alternative when embedded deleter
/// object is not needed.
template <typename T, details::UniquePtrTrait<T> Trait = details::UniquePtrDefaultTrait<T>>
class UniquePtr {
public:
    UniquePtr():
        ptr(nullptr)
    {}

    UniquePtr(etl::nullptr_t):
        ptr(nullptr)
    {}

    UniquePtr(T *ptr):
        ptr(ptr)
    {}

    UniquePtr(const UniquePtr &) = delete;

    UniquePtr(UniquePtr &&other):
        ptr(other.ptr)
    {
        other.ptr = nullptr;
    }

    ~UniquePtr()
    {
        Reset();
    }

    UniquePtr &
    operator =(const UniquePtr &) = delete;

    UniquePtr &
    operator =(UniquePtr &&other)
    {
        if (&other == this) {
            return *this;
        }
        Reset();
        ptr = other.ptr;
        other.ptr = nullptr;
        return *this;
    }

    UniquePtr &
    operator =(etl::nullptr_t)
    {
        Reset();
        return *this;
    }

    /// Delete the owned object (if any) and optionally take ownership of a new one.
    void
    Reset(T *newPtr = nullptr)
    {
        if (ptr) {
            Trait::Delete(ptr);
        }
        ptr = newPtr;
    }

    /// Release ownership without deleting the owned object.
    /// @return Previously owned pointer.
    T *
    Release()
    {
        T *result = ptr;
        ptr = nullptr;
        return result;
    }

    void
    Swap(UniquePtr &other)
    {
        etl::swap(ptr, other.ptr);
    }

    operator bool() const
    {
        return ptr != nullptr;
    }

    bool
    operator ==(const UniquePtr &other) const
    {
        return ptr == other.ptr;
    }

    bool
    operator !=(const UniquePtr &other) const
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

#endif /* PULSE_UNIQUE_PTR_H */
