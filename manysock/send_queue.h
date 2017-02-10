#ifndef _SEND_QUEUE_H_
#define _SEND_QUEUE_H_

#include <algorithm>
#include <vector>

#include <cassert>

#include <packet.h>

using std::make_heap;
using std::pop_heap;
using std::vector;
using std::greater;
using std::less;

// IMPORTANT: For good performance of the underlying min-heap, the q elements (type T) should be small!

template <class T> class SendQueue
{
public:
    SendQueue(const vector<T>& elems);
    void pop_and_insert(const T& newelem);
    T front() const;
    size_t size() const;
private:
    std::vector<T> elems_;
    T biggest;
};

template <class T> SendQueue<T>::SendQueue(const vector<T>& elems) : elems_(elems)
{
    make_heap(elems_.begin(), elems_.end(), greater<T>());
    biggest = *max_element(elems_.begin(), elems_.end(), less<T>());
}

template <class T> void SendQueue<T>::pop_and_insert(const T& new_elem)
{
    //if (elems_.size() >= 3 && elems_[1] > new_elem && elems_[2] > new_elem)
    if (elems_.size() >= 3 && greater<T>()(elems_[1], new_elem) && greater<T>()(elems_[2], new_elem))
    {
        elems_[0] = new_elem;
        //assert(std::is_heap(elems_.begin(), elems_.end(), greater<T>()));
        return;
    }

    // if (greater<T>()(new_elem, biggest))
    // {
    //     biggest = new_elem;
    //     elems_.push_back(new_elem);
    //     //assert(std::is_heap(elems_.begin(), elems_.end(), greater<T>()));
    //     return;
    // }
    elems_.push_back(new_elem);
    pop_heap(elems_.begin(), elems_.end(), greater<T>());
    elems_.pop_back();
    //assert(std::is_heap(elems_.begin(), elems_.end(), greater<T>()));
}

template <class T> T SendQueue<T>::front() const
{
    assert(!elems_.empty());
    return elems_[0];
}

template <class T> size_t SendQueue<T>::size() const { return elems_.size(); }


#endif
