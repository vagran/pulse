#ifndef LIST_H
#define LIST_H

#include <pulse/details/common.h>
#include <etl/concepts.h>


namespace pulse {

template<typename TPtr, auto GetListItem>
concept ListItemAccessor = requires(TPtr ptr) {
    requires etl::is_reference_v<decltype(GetListItem(ptr))>;
    { GetListItem(ptr).next } -> etl::same_as<TPtr &>;
    GetListItem(ptr).next = ptr;
};


// Singly-linked list.
template <typename TPtr, auto GetListItem>
requires ListItemAccessor<TPtr, GetListItem>
struct List {
    TPtr head = TPtr();

    void
    AddFirst(TPtr item);
};

template <typename TPtr, auto GetListItem>
requires ListItemAccessor<TPtr, GetListItem>
void
List<TPtr, GetListItem>::AddFirst(TPtr item)
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


// Singly-linked list with tail pointer.
template <typename TPtr, auto GetListItem>
requires ListItemAccessor<TPtr, GetListItem>
struct TailedList {
    TPtr head = TPtr(),
         tail = TPtr();

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
};

template <typename TPtr, auto GetListItem>
requires ListItemAccessor<TPtr, GetListItem>
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
requires ListItemAccessor<TPtr, GetListItem>
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
requires ListItemAccessor<TPtr, GetListItem>
TPtr
TailedList<TPtr, GetListItem>::PopFirst()
{
    TPtr res = head;
    if (res) {
        auto &li = GetListItem(res);
        head = li.next;
        li.next = TPtr();
    }
    return etl::move(res);
}

} // namespace pulse

#endif /* LIST_H */
