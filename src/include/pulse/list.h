#ifndef LIST_H
#define LIST_H

#include <pulse/details/common.h>
#include <etl/concepts.h>


namespace pulse {


namespace details {

template <typename Tr, typename TPtr>
concept ListTrait = requires(TPtr &p, const TPtr &pNext) {
    { Tr::GetNext(p) } -> etl::same_as<TPtr>;

    { Tr::SetNext(p, pNext) } -> etl::same_as<void>;
};

// Make this class friend if having private `next`.
template <typename TPtr>
struct ListDefaultTrait {
    static TPtr
    GetNext(const TPtr &p)
    {
        return p->next;
    }

    template <typename TNextPtr>
    static void
    SetNext(TPtr &p, TNextPtr &&next)
    {
        p->next = etl::forward<TNextPtr>(next);
    }
};


template <typename TPtr, details::ListTrait<TPtr> Trait>
class ListIterator {
public:
    ListIterator(TPtr item):
        curItem(item)
    {}

    bool
    operator ==(const ListIterator<TPtr, Trait> &other) const
    {
        return curItem == other.curItem;
    }

    void
    operator ++()
    {
        if (curItem) {
            curItem = Trait::GetNext(curItem);
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


/// Intrusive singly-linked list.
template <typename TPtr, details::ListTrait<TPtr> Trait = details::ListDefaultTrait<TPtr>>
struct List {
    TPtr head = TPtr();

    List() = default;

    List(const List &other) = delete;

    List(List &&other):
        head(etl::move(other.head))
    {
        other.head = TPtr();
    }

    bool
    IsEmpty() const
    {
        return !head;
    }

    void
    AddFirst(TPtr item);

    /// @return First item if any, null if empty list.
    TPtr
    PopFirst();

    /// @return True if found and removed, false if not found.
    bool
    Remove(const TPtr &item);

    details::ListIterator<TPtr, Trait>
    begin() const
    {
        return {head};
    }

    details::ListIterator<TPtr, Trait>
    end() const
    {
        return {TPtr()};
    }
};

template <typename TPtr, details::ListTrait<TPtr> Trait>
void
List<TPtr, Trait>::AddFirst(TPtr item)
{
    PULSE_ASSERT(!Trait::GetNext(item));
    if (!head) {
        head = etl::move(item);
    } else {
        Trait::SetNext(item, etl::move(head));
        head = etl::move(item);
    }
}

template <typename TPtr, details::ListTrait<TPtr> Trait>
TPtr
List<TPtr, Trait>::PopFirst()
{
    TPtr res = head;
    if (res) {
        head = Trait::GetNext(res);
        Trait::SetNext(res, TPtr());
    }
    return etl::move(res);
}

template <typename TPtr, details::ListTrait<TPtr> Trait>
bool
List<TPtr, Trait>::Remove(const TPtr &item)
{
    TPtr p = head;
    TPtr prev = TPtr();
    while (p) {
        if (p == item) {
            if (prev) {
                Trait::SetNext(prev, Trait::GetNext(p));
            } else {
                head = Trait::GetNext(p);
            }
            Trait::SetNext(p, TPtr());
            return true;
        }
        prev = p;
        p = Trait::GetNext(p);
    }
    return false;
}


/// Intrusive singly-linked list with tail pointer.
template <typename TPtr, details::ListTrait<TPtr> Trait = details::ListDefaultTrait<TPtr>>
struct TailedList {
    TPtr head = TPtr(),
         tail = TPtr();

    TailedList() = default;

    TailedList(const TailedList &other) = delete;

    TailedList(TailedList &&other):
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

    details::ListIterator<TPtr, Trait>
    begin() const
    {
        return {head};
    }

    details::ListIterator<TPtr, Trait>
    end() const
    {
        return {TPtr()};
    }
};

template <typename TPtr, details::ListTrait<TPtr> Trait>
void
TailedList<TPtr, Trait>::AddFirst(TPtr item)
{
    PULSE_ASSERT(!Trait::GetNext(item));
    if (!head) {
        PULSE_ASSERT(!tail);
        head = item;
        tail = etl::move(item);
    } else {
        PULSE_ASSERT(tail);
        Trait::SetNext(item, etl::move(head));
        head = etl::move(item);
    }
}

template <typename TPtr, details::ListTrait<TPtr> Trait>
void
TailedList<TPtr, Trait>::AddLast(TPtr item)
{
    PULSE_ASSERT(!Trait::GetNext(item));
    if (!tail) {
        PULSE_ASSERT(!head);
        head = item;
        tail = etl::move(item);
    } else {
        PULSE_ASSERT(head);
        Trait::SetNext(tail, item);
        tail = etl::move(item);
    }
}

template <typename TPtr, details::ListTrait<TPtr> Trait>
TPtr
TailedList<TPtr, Trait>::PopFirst()
{
    TPtr res = head;
    if (res) {
        head = Trait::GetNext(res);
        if (!head) {
            tail = TPtr();
        }
        Trait::SetNext(res, TPtr());
    }
    return etl::move(res);
}

template <typename TPtr, details::ListTrait<TPtr> Trait>
bool
TailedList<TPtr, Trait>::Remove(const TPtr &item)
{
    TPtr p = head;
    TPtr prev = TPtr();
    while (p) {
        if (p == item) {
            if (prev) {
                Trait::SetNext(prev, Trait::GetNext(p));
            } else {
                head = Trait::GetNext(p);
            }
            if (tail == p) {
                tail = prev;
            }
            Trait::SetNext(p, TPtr());
            return true;
        }
        prev = p;
        p = Trait::GetNext(p);
    }
    return false;
}

} // namespace pulse

#endif /* LIST_H */
