//
// Heap.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

class Object;
class SymbolTable;
class Val;


using byte = std::byte;

using  intpos =  int32_t;
using uintpos = uint32_t;

using heapsize = uintpos;      ///< Like `size_t` for Heaps.

enum class heappos : uintpos { };      ///< A position in a Heap, relative to its base.

static constexpr heappos nullpos {0};

static inline heappos operator+ (heappos p, intpos i) {return heappos(uintpos(p) + i);}
static inline heappos operator- (heappos p, intpos i) {return heappos(uintpos(p) - i);}
static inline std::strong_ordering operator<=> (heappos p, size_t i) {return uintpos(p) <=> i;}
static inline std::strong_ordering operator<=> (heappos p, intpos i) {return int64_t(p) <=> int64_t(i);}


/// A simple container for dynamic allocation.
/// Pointers within a Heap are 32-bit values, offsets from the heap's base address.
/// Allocation uses a simple bump (arena) allocator.
class Heap {
public:
    static constexpr size_t  MaxSize = 1 << 31;
    static const size_t Overhead;

    // Constructs a new empty Heap starting at address `base` and capacity `size`.
    Heap(void *base, size_t capacity) noexcept;

    // Constructs a new empty Heap with space allocated by malloc.
    explicit Heap(size_t capacity);

    Heap(Heap&&) noexcept;
    Heap& operator=(Heap&&) noexcept;
    ~Heap();

    // Constructs a Heap from already-existing heap data. Throws if the data is not valid.
    static Heap existing(void *base, size_t used, size_t capacity);

    bool valid() const                  {return _base != nullptr;}

    const void*  base() const           {return _base;}         ///< Address of start of heap.
    const size_t capacity() const       {return _end - _base;}  ///< Maximum size it can grow to
    const size_t used() const           {return _cur - _base;}  ///< Maximum byte-offset used
    const size_t remaining() const      {return _end - _cur;}   ///< Bytes of capacity left
    const size_t available() const      {return remaining();}

    /// The heap's root value. Starts as Null, but usually an Array or Dict.
    Val rootVal() const;

    /// Sets the heap's root value.
    void setRoot(Val);

    Object* rootObject() const;
    void setRoot(Object* obj);
    template <class T> T* root() const;

    /// Resets the Heap to an empty state.
    void reset();

    void garbageCollectTo(Heap &dstHeap);

    //---- Current Heap:

    /// The current heap of the current thread, or nullptr if none.
    static Heap* current();

    //---- Allocation:

    /// Allocates space for `size` bytes.
    /// If there's not enough space, calls the `AllocFailureHandler` and retries.
    /// If there's no `AllocFailureHandler`, or the handler returns false,
    /// `alloc` returns nullptr.
    ///
    /// Note that if there is a failure handler that runs the garbage collector,
    /// then `alloc` may move objects, invalidating `Object` pointers and `Val`s!
    void* alloc(heapsize size);

    /// A callback that's invoked when the Heap doesn't have enough space for an allocation.
    /// It should attempt to increase the free space, then return true.
    /// If it can't do anything, it must return false.
    /// The typical things for the callback to do are garbage-collect or grow the heap.
    using AllocFailureHandler = bool(*)(Heap*,heapsize sizeNeeded);

    /// Sets the allocation-failure handler. See @ref AllocFailureHandler for details.
    void setAllocFailureHandler(AllocFailureHandler h)  {_allocFailureHandler = h;}

    /// Changes the size of the heap. All this does is move the end-of-heap pointer;
    /// it doesn't reallocate or move the heap or invalidate any Object pointers.
    /// Returns true on success, false if the new size is illegal.
    /// - It is illegal to grow a malloced heap.
    /// - It is illegal to shrink a Heap smaller than its current `used()` size.
    /// If you grow a non-malloced heap, the new address space at the end must be available
    /// and writeable, otherwise Bad Things will happen when the Heap writes into it.
    bool resize(size_t newSize);

    //---- Address Translation:

    /// Translates a `heappos` offset to a real address.
    void* at(heappos off)               {assert(validPos(off)); return _base + uintpos(off);}
    const void* at(heappos off) const   {assert(validPos(off)); return _base + uintpos(off);}

    /// Translates a real address to a `heappos` offset.
    heappos pos(const void *ptr) const {
        assert(ptr >= _base && ptr < _end);
        return heappos((byte*)ptr - _base);
    }

    bool contains(const void *ptr) const     {return ptr >= _base && ptr < _cur;}

    /// Returns true if a `heappos` is valid in this Heap, i.e. doesn't point past the end of
    /// allocated memory.
    bool validPos(heappos pos) const;

    SymbolTable const& symbolTable() const {return *_symbolTable;}
    SymbolTable& symbolTable()             {return *_symbolTable;}

    using Visitor = std::function<bool(const Object*)>;

    /// Calls the Visitor callback once for each live (reachable) object.
    void visit(Visitor const&);

    /// Calls the Visitor callback once for each object, even if it's unreachable garbage.
    void visitAll(Visitor const&);

    void registerExternalRoots(Val rootArray[], size_t count);
    void unregisterExternalRoots(Val rootArray[]);

private:
    friend class Object;
    friend class SymbolTable;
    friend class GarbageCollector;
    friend class UsingHeap;

    Heap();
    Heap(void *base, size_t capacity, bool malloced);
    void clearForwarding();

    Heap const* enter() const;
    void exit() const;
    void exit(Heap const* newCurrent) const;

    // Allocates space without initializing it. Caller MUST initialize (see Object constructor)
    void* rawAlloc(heapsize size) {
        do {
            byte *result = _cur;
            byte *newCur = result + size;
            if (newCur <= _end) {
                _cur = newCur;
                return result;
            }
        } while (_allocFailureHandler && _allocFailureHandler(this, size));
        return nullptr;
    }

    Object* firstObject();
    Object* nextObject(Object *obj);

    Val symbolTableVal() const;
    void setSymbolTableVal(Val);

    byte*   _base;
    byte*   _end;
    byte*   _cur;
    AllocFailureHandler _allocFailureHandler = nullptr;
    std::unique_ptr<SymbolTable> _symbolTable;
    bool    _malloced = false;
};



/// Makes a heap current (on this thread) while in scope.
/// When it exits scope, the previously-current heap is restored.
class UsingHeap {
public:
    explicit UsingHeap(Heap const* heap)    :_heap(heap) {_prev = heap->enter();}
    explicit UsingHeap(Heap const& heap)    :UsingHeap(&heap) { }
    ~UsingHeap()                            {_heap->exit(_prev);}
private:
    Heap const* _heap;
    Heap const* _prev;
};



class ConstHeapRef {
public:
    ConstHeapRef()                  :_heap(Heap::current()) { }
    ConstHeapRef(nullptr_t)         :ConstHeapRef() { }
    ConstHeapRef(Heap const* h)     :_heap(h) { }
    ConstHeapRef(Heap const& h)     :_heap(&h) { }

    Heap const* get()           {return _heap;}
    Heap const* operator* ()    {return _heap;}
    Heap const* operator->()    {return _heap;}

protected:
    Heap const* _heap;
};


class HeapRef : public ConstHeapRef {
public:
    HeapRef()           :ConstHeapRef() { }
    HeapRef(nullptr_t)  :ConstHeapRef() { }
    HeapRef(Heap *h)    :ConstHeapRef(h) { }
    HeapRef(Heap &h)    :ConstHeapRef(h) { }

    Heap* operator* () const    {return (Heap*)_heap;}
    Heap* operator->() const    {return (Heap*)_heap;}
};


#define IN_MUT_HEAP HeapRef heap //= nullptr
#define IN_HEAP     ConstHeapRef heap //= nullptr

#define CUR_HEAP    ConstHeapRef(nullptr)



/// A typical copying garbage collector that copies all live objects into another Heap.
/// At the end it swaps the memory of the two Heaps, so the original heap is now clean,
/// and the other heap can be freed or reused for the next GC.
class GarbageCollector {
public:
    /// Constructs the GC and copies all Values reachable from the root into a temporary Heap
    /// with the same capacity as this one.
    GarbageCollector(Heap &heap);

    /// Constructs the GC and copies all Values reachable from the root into `otherHeap`.
    GarbageCollector(Heap &heap, Heap &otherHeap);

    /// Updates an existing Val that came from the "from" heap,
    /// returning an equivalent Val that's been copied to the "to" heap.
    /// You MUST call this, or any of the `update` methods below,
    /// on any live references to values in `fromHeap`, or they'll be out of date.
    ///
    /// Do not do anything else with the heap while the GarbageCollector is in scope!
    [[nodiscard]] Val scan(Val v);

    [[nodiscard]] Object* scan(Object*);

    // These are equivalent to scanValue but update the Val/Ptr/Object in place:
    void update(Val* val);
    template <class T> void update(T** obj);

    // The destructor swaps the two heaps, so _fromHeap is now the live one.
    ~GarbageCollector()     {_fromHeap.reset(); std::swap(_fromHeap, _toHeap);}

private:
    void scanRoot();
    template <class T> Val scanValueAs(Val val);

    std::unique_ptr<Heap> _tempHeap;    // Owns temporary heap, if there is one
    Heap &_fromHeap, &_toHeap;          // The source and destination heaps
};
