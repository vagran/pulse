#ifndef SHARED_PTR_H
#define SHARED_PTR_H

#include <pulse/details/common.h>

namespace pulse {

namespace details {

template <typename Tr, typename T>
concept SharedPtrTrait = requires(T& obj) {
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

template <typename T>
struct SharedPtrDefaultTrait {
    static void
    AddRef(T &obj)
    {
        if (obj.refCounter == etl::numeric_limits<decltype(obj.refCounter)>::max()) {
            PULSE_PANIC("Reference counter overflow");
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

} // namespace details


/** Intrusive shared pointer. */
template <typename T, class Trait = details::SharedPtrDefaultTrait<T>>
requires details::SharedPtrTrait<Trait, T>
class SharedPtr {
public:
    SharedPtr():
        ptr(nullptr)
    {}

    SharedPtr(T *ptr):
        ptr(ptr)
    {
        if (ptr) {
            Trait::AddRef(*ptr);
        }
    }

    SharedPtr(const SharedPtr &other):
        SharedPtr(other.ptr)
    {}

    template<typename Y>
    SharedPtr(const SharedPtr<Y> & other):
        SharedPtr(other.ptr)
    {}

    SharedPtr(SharedPtr<T> &&other):
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
        Reset();
        ptr = other.ptr;
        if (ptr) {
            Trait::AddRef(*ptr);
        }
        return *this;
    }

    template<typename Y>
    SharedPtr &
    operator =(const SharedPtr<Y> &other)
    {
        Reset();
        ptr = other.ptr;
        if (ptr) {
            Trait::AddRef(*ptr);
        }
        return *this;
    }

    SharedPtr &
    operator =(SharedPtr &&other) noexcept
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
    operator =(SharedPtr<Y> &&other) noexcept
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

private:
    T *ptr;
};

} // namespace pulse

#endif /* SHARED_PTR_H */
