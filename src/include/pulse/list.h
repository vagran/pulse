#ifndef LIST_H
#define LIST_H

#include <pulse/details/common.h>
#include <etl/concepts.h>


namespace pulse {


namespace details {

// Used for allowing TaskList instantiation in TaskPromise. Full definition fails to compile due to
// TaskPromise being incomplete type at this point.
template<typename TPtr, auto GetListItem>
concept ListItemAccessorWeak = requires(TPtr ptr) {
    requires etl::is_reference_v<decltype(GetListItem(ptr))>;
};

template<typename TPtr, auto GetListItem>
concept ListItemAccessor = requires(TPtr ptr) {
    requires ListItemAccessorWeak<TPtr, GetListItem>;
    { GetListItem(ptr).next } -> etl::same_as<TPtr &>;
    GetListItem(ptr).next = ptr;
};

template<typename TPtr>
auto &
GetDefaultListItem(TPtr ptr)
{
    return *ptr;
}

template <typename TPtr, auto GetListItem>
requires details::ListItemAccessorWeak<TPtr, GetListItem>
class ListIterator {
public:
    ListIterator(TPtr item):
        curItem(item)
    {}

    bool
    operator ==(const ListIterator<TPtr, GetListItem> &other) const
    {
        return curItem == other.curItem;
    }

    void
    operator ++()
    {
        if (curItem) {
            curItem = GetListItem(curItem).next;
        }
    }

    TPtr
    operator *() const
    {
        return curItem;
    }

private:
    TPtr curItem;
};

} // namespace details


// Singly-linked list. Weak constraints version for using when *TPtr is not yet fully defined.
template <typename TPtr, auto GetListItem = details::GetDefaultListItem<TPtr>>
requires details::ListItemAccessorWeak<TPtr, GetListItem>
struct ListWeak {
    TPtr head = TPtr();

    ListWeak() = default;

    ListWeak(const ListWeak &other) = delete;

    ListWeak(ListWeak &&other) noexcept:
        head(etl::move(other.head))
    {
        other.head = TPtr();
    }

    void
    AddFirst(TPtr item);

    TPtr
    PopFirst();

    /// @return True if found and removed, false if not found.
    bool
    Remove(const TPtr &item);

    details::ListIterator<TPtr, GetListItem>
    begin() const
    {
        return {head};
    }

    details::ListIterator<TPtr, GetListItem>
    end() const
    {
        return {TPtr()};
    }
};

template <typename TPtr, auto GetListItem>
requires details::ListItemAccessorWeak<TPtr, GetListItem>
void
ListWeak<TPtr, GetListItem>::AddFirst(TPtr item)
{
    PULSE_ASSERT(!GetListItem(item).next);
    if (!head) {
        head = etl::move(item);
    } else {
        auto &li = GetListItem(item);
        li.next = etl::move(head);
        head = etl::move(item);
    }
}

template <typename TPtr, auto GetListItem>
requires details::ListItemAccessorWeak<TPtr, GetListItem>
TPtr
ListWeak<TPtr, GetListItem>::PopFirst()
{
    TPtr res = head;
    if (res) {
        auto &li = GetListItem(res);
        head = li.next;
        li.next = TPtr();
    }
    return etl::move(res);
}

template <typename TPtr, auto GetListItem>
requires details::ListItemAccessorWeak<TPtr, GetListItem>
bool
ListWeak<TPtr, GetListItem>::Remove(const TPtr &item)
{
    TPtr p = head;
    TPtr prev = TPtr();
    while (p) {
        auto &li = GetListItem(p);
        if (p == item) {
            if (prev) {
                auto &prevLi = GetListItem(prev);
                prevLi.next = li.next;
            } else {
                head = li.next;
            }
            li.next = TPtr();
            return true;
        }
        prev = p;
        p = li.next;
    }
    return false;
}


// Singly-linked list.
template <typename TPtr, auto GetListItem = details::GetDefaultListItem<TPtr>>
requires details::ListItemAccessor<TPtr, GetListItem>
using List = ListWeak<TPtr, GetListItem>;


// Singly-linked list with tail pointer.
template <typename TPtr, auto GetListItem = details::GetDefaultListItem<TPtr>>
requires details::ListItemAccessor<TPtr, GetListItem>
struct TailedList {
    TPtr head = TPtr(),
         tail = TPtr();

    TailedList() = default;

    TailedList(const TailedList &other) = delete;

    TailedList(TailedList &&other) noexcept:
        head(etl::move(other.head)),
        tail(etl::move(other.tail))
    {
        other.head = TPtr();
        other.tail = TPtr();
    }

    bool
    IsEmpty() const
    {
        return !head;
    }

    void
    AddFirst(TPtr item);

    void
    AddLast(TPtr item);

    /** @return First item if any, null if empty list. */
    TPtr
    PopFirst();

    /// @return True if found and removed, false if not found.
    bool
    Remove(const TPtr &item);
};

template <typename TPtr, auto GetListItem>
requires details::ListItemAccessor<TPtr, GetListItem>
void
TailedList<TPtr, GetListItem>::AddFirst(TPtr item)
{
    PULSE_ASSERT(!GetListItem(item).next);
    if (!head) {
        PULSE_ASSERT(!tail);
        head = item;
        tail = etl::move(item);
    } else {
        auto &li = GetListItem(item);
        li.next = etl::move(head);
        head = etl::move(item);
    }
}

template <typename TPtr, auto GetListItem>
requires details::ListItemAccessor<TPtr, GetListItem>
void
TailedList<TPtr, GetListItem>::AddLast(TPtr item)
{
    PULSE_ASSERT(!GetListItem(item).next);
    if (!tail) {
        head = item;
        tail = etl::move(item);
    } else {
        auto &li = GetListItem(tail);
        li.next = item;
        tail = etl::move(item);
    }
}

template <typename TPtr, auto GetListItem>
requires details::ListItemAccessor<TPtr, GetListItem>
TPtr
TailedList<TPtr, GetListItem>::PopFirst()
{
    TPtr res = head;
    if (res) {
        auto &li = GetListItem(res);
        head = li.next;
        if (!head) {
            tail = TPtr();
        }
        li.next = TPtr();
    }
    return etl::move(res);
}

template <typename TPtr, auto GetListItem>
requires details::ListItemAccessor<TPtr, GetListItem>
bool
TailedList<TPtr, GetListItem>::Remove(const TPtr &item)
{
    TPtr p = head;
    TPtr prev = TPtr();
    while (p) {
        auto &li = GetListItem(p);
        if (p == item) {
            if (prev) {
                auto &prevLi = GetListItem(prev);
                prevLi.next = li.next;
            } else {
                head = li.next;
            }
            if (tail == p) {
                tail = prev;
            }
            li.next = TPtr();
            return true;
        }
        prev = p;
        p = li.next;
    }
    return false;
}

} // namespace pulse

#endif /* LIST_H */
