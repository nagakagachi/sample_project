#pragma once


namespace ngl
{
    // https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/
    template<typename T, u32 CAPACITY>
    class StaticSizeRingBuffer
    {
    public:
        using Type = T;
        static constexpr u32 k_capacity = CAPACITY;
    public:
        StaticSizeRingBuffer()
        {
            
        }
        ~StaticSizeRingBuffer() = default;

        void Reset()
        {
            tail_ = head_;
        }

        // Push to Tail.
        //  Overwrite Oldest Element.
        bool PushTail(const T& item)
        {
            data_[tail_] = item;

            if (IsFull())
                head_ = (head_ + 1) % k_capacity;
            
            tail_ = (tail_ + 1) % k_capacity;

            return true;
        }

        const int CalcRingIndex(int head_based_index) const
        {
            if (0 > head_based_index || Size() <= head_based_index)
            {
                assert(false);
                return -1;
            }
            return (head_based_index + head_) % k_capacity;
        }
        const T* Get(int head_based_index) const
        {
            auto index = CalcRingIndex(head_based_index);
            return &data_[index];
        }
        T* Get(int head_based_index)
        {
            auto index = CalcRingIndex(head_based_index);
            return &data_[index];
        }
        
        int Size() const
        {
            auto size = 0;
            //if(!IsFull())
            {
                if(tail_ >= head_)
                {
                    size = tail_ - head_;
                }
                else
                {
                    size = k_capacity + tail_ - head_;
                }
            }
            return size;
        }
        bool IsEmpty() const
        {
            return tail_ == head_;
        }
        bool IsFull() const
        {
            // 要素一つを番兵としている.
#if 1
            return ((tail_ + 1) % k_capacity) == head_;
#else
            const auto temp = (tail_ >= k_capacity-1)? 0 : tail_+1;
            return temp == head_;
#endif
        }
        
    private:
        std::array<Type, k_capacity> data_{};
        int head_{};
        int tail_{};
    };
}