#ifndef __DMA_BUFFER_H__
#define __DMA_BUFFER_H__
#include "Arduino.h"
#include "Queue.h"

#ifndef __SCB_DCACHE_LINE_SIZE
#define __SCB_DCACHE_LINE_SIZE  32
#endif

template <size_t A> class AlignedAlloc {
    // AlignedAlloc allocates extra memory before the aligned memory start, and uses that
    // extra memory to stash the pointer returned from malloc. Note memory allocated with
    // Aligned::alloc must be free'd later with Aligned::free.
    public:
        static void *malloc(size_t size) {
            void **ptr, *stashed;
            size_t offset = A - 1 + sizeof(void *);
            if ((A % 2) || !((stashed = ::malloc(size + offset)))) {
                return nullptr;
            }
            ptr = (void **) (((uintptr_t) stashed + offset) & ~(A - 1));
            ptr[-1] = stashed;
            return ptr;
        }

        static void free(void *ptr) {
            if (ptr != nullptr) {
                ::free(((void **) ptr)[-1]);
            }
        }

        static size_t round(size_t size) {
            return ((size + (A-1)) & ~(A-1));
        }
};

enum {
    DMA_BUFFER_DISCONT  = (1 << 0),
    DMA_BUFFER_INTRLVD  = (1 << 1),
};

template <class, size_t> class DMABufferPool;

template <class T, size_t A=__SCB_DCACHE_LINE_SIZE> class DMABuffer {
    typedef DMABufferPool<T, A> Pool;

    private:
        Pool *pool;
        size_t n_samples;
        size_t n_channels;
        T *ptr;
        uint32_t ts;
        uint32_t flags;

    public:
        DMABuffer *next;

        DMABuffer(Pool *pool=nullptr, size_t samples=0, size_t channels=0, T *mem=nullptr):
            pool(pool), n_samples(samples), n_channels(channels), ptr(mem), ts(0), flags(0), next(nullptr) {
        }

        T *data() {
            return ptr;
        }

        size_t size() {
            return n_samples * n_channels;
        }

        size_t bytes() {
            return n_samples * n_channels * sizeof(T);
        }

        void flush() {
            if (ptr) {
                SCB_CleanDCache_by_Addr(data(), bytes());
            }
        }

        void invalidate() {
            if (ptr) {
                SCB_InvalidateDCache_by_Addr(data(), bytes());
            }
        }

        uint32_t timestamp() {
            return ts;
        }

        void timestamp(uint32_t ts) {
            this->ts = ts;
        }

        uint32_t channels() {
            return n_channels;
        }

        void release() {
            if (pool && ptr) {
                pool->release(this);
            }
        }

        void setflags(uint32_t f) {
            flags |= f;
        }

        bool getflags(uint32_t f) {
            return flags & f;
        }

        void clrflags(uint32_t f=0xFFFFFFFFU) {
            flags &= (~f);
        }

        T operator[](size_t i) {
            if (ptr && i < size()) {
                return data()[i];
            }
            return -1;
        }
};

template <class T, size_t A=__SCB_DCACHE_LINE_SIZE> class DMABufferPool {
    private:
        LLQueue<DMABuffer<T>*> freeq;
        LLQueue<DMABuffer<T>*> readyq;
        std::unique_ptr<DMABuffer<T>[]> buffers;
        std::unique_ptr<uint8_t, decltype(&AlignedAlloc<A>::free)> pool;

    public:
        DMABufferPool(size_t n_samples, size_t n_channels, size_t n_buffers):
            buffers(nullptr), pool(nullptr, AlignedAlloc<A>::free) {

            size_t bufsize = AlignedAlloc<A>::round(n_samples * n_channels *sizeof(T));

            // Allocate non-aligned memory for the DMA buffers objects.
            buffers.reset(new DMABuffer<T>[n_buffers]);

            // Allocate aligned memory pool for DMA buffers pointers.
            pool.reset((uint8_t *) AlignedAlloc<A>::malloc(n_buffers * bufsize));

            if (buffers && pool) {
                // Init DMA buffers using aligned pointers to dma buffers memory.
                for (size_t i=0; i<n_buffers; i++) {
                    buffers[i] = DMABuffer<T>(this, n_samples, n_channels, (T *) &pool.get()[i * bufsize]);
                    freeq.push(&buffers[i]);
                }
            }
        }

        bool writable() {
            return !freeq.empty();
        }

        bool readable() {
            return !readyq.empty();
        }

        DMABuffer<T> *allocate() {
            // Get a DMA buffer from the free queue.
            return freeq.pop();
        }

        void release(DMABuffer<T> *buf) {
            // Return DMA buffer to the free queue.
            buf->clrflags();
            freeq.push(buf);
        }

        void enqueue(DMABuffer<T> *buf) {
            // Add DMA buffer to the ready queue.
            readyq.push(buf);
        }

        DMABuffer<T> *dequeue() {
            // Return a DMA buffer from the ready queue.
            return readyq.pop();
        }
};
#endif //__DMA_BUFFER_H__
