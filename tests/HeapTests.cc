//
// HeapTests.cc
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "smol_world.hh"
#include "catch.hpp"
#include <iostream>

using namespace std;


TEST_CASE("Empty Heap", "[heap]") {
    Heap heap(10000);
    // attributes:
    CHECK(heap.valid());
    CHECK(heap.base() != nullptr);
    CHECK(heap.capacity() == 10000);
    CHECK(heap.used() == Heap::Overhead);
    CHECK(heap.remaining() == 10000 - Heap::Overhead);

    CHECK(!heap.contains(nullptr));

    // roots:
    CHECK(heap.rootVal() == nullval);
    CHECK(heap.rootObject() == nullptr);

    // current heap:
    CHECK(Heap::current() == nullptr);
    {
        UsingHeap u(heap);
        CHECK(Heap::current() == &heap);
    }
    CHECK(Heap::current() == nullptr);

    // visit:
    heap.visit([&](const Object *obj) { FAIL("Visitor should not be called"); return false; });
}


TEST_CASE("Alloc", "[heap]") {
    Heap heap(10000);
    auto ptr = (byte*)heap.alloc(123);
    cout << "ptr = " << (void*)ptr << endl;
    REQUIRE(ptr != nullptr);
    CHECK(heap.contains(ptr));
    CHECK(heap.contains(ptr + 122));
    CHECK(!heap.contains(ptr + 123));

    CHECK(heap.used() == Heap::Overhead + 2 + 123);
    CHECK(heap.remaining() == 10000 - Heap::Overhead - 2 - 123);

    int i = 0;
    heap.visitAll([&](const Object *obj) {
        CHECK(heap.contains(obj));
        CHECK(obj->type() == Type::Blob);
        switch (i++) {
            case 0: CHECK(obj->dataSize() == 123); return true;
            default: FAIL("Invalid object visited"); return false;
        }
    });
    CHECK(i == 1);

    auto ptr2 = (byte*)heap.alloc(9859); // exactly fills the heap
    cout << "ptr2= " << (void*)ptr2 << endl;
    REQUIRE(ptr2 != nullptr);
    CHECK(heap.contains(ptr2));
    CHECK(heap.contains(ptr2 + 9858));
    CHECK(!heap.contains(ptr2 + 9859));

    CHECK(heap.used() == 10000);
    CHECK(heap.remaining() == 0);

    i = 0;
    heap.visitAll([&](const Object *obj) {
        CHECK(heap.contains(obj));
        CHECK(obj->type() == Type::Blob);
        switch (i++) {
            case 0: CHECK(obj->dataSize() == 123); return true;
            case 1: CHECK(obj->dataSize() == 9859); return true;
            default: FAIL("Invalid object visited"); return false;
        }
    });
    CHECK(i == 2);

    CHECK(heap.alloc(1) == nullptr);
}


static void testAllocRangeOfSizes(heapsize BaseSize, int NumBlocks) {
    Heap heap(Heap::Overhead + NumBlocks * (4 + BaseSize) + (NumBlocks * (NumBlocks - 1)) / 2);
    cerr << "Heap size is " << heap.capacity() << endl;

    vector<Blob*> blocks(NumBlocks);
    size_t dataSize = 0;
    for (int i = 0; i < NumBlocks; ++i) {
        size_t size = BaseSize + i;
        INFO("Block size " << size);
        Blob* blob = blocks[i] = Blob::create(size, heap);
        //cerr << "Block " << size << " = " << (void*)blob << endl;
        REQUIRE(blob != nullptr);
        CHECK(heap.contains(blob));
        CHECK(blob->type() == Type::Blob);
        CHECK(blob->capacity() == size);
        memset(blob->begin(), uint8_t(i), size);
        CHECK(blob->type() == Type::Blob);
        CHECK(blob->capacity() == size);
        if (i > 0) {
            auto prev = blocks[i - 1];
            CHECK(prev->type() == Type::Blob);
            CHECK(prev->capacity() == size - 1);
        }
        dataSize += size;
    }
    cerr << "Allocated " << heap.used() << " bytes; overhead of "
    << (double(heap.used() - dataSize) / NumBlocks) << " bytes/block\n";

    for (int i = 0; i < NumBlocks; ++i) {
        size_t size = BaseSize + i;
        INFO("Block #" << i);
        Blob *blob = blocks[i];
        CHECK(blob->type() == Type::Blob);
        auto data = (uint8_t*)blob->begin();
        REQUIRE(heap.contains(data));
        for (int j = 0; j < size; j++) {
            if (data[j] != uint8_t(i)) {
                FAIL(" byte " << j << " is " << data[j] << ", expected " << uint8_t(i));
            }
        }
    }

    int i = 0;
    heap.visitAll([&](const Object *obj) {
        INFO("Block #" << i);
        REQUIRE(i < NumBlocks);
        CHECK(obj == blocks[i]);
        ++i;
        return true;
    });
    CHECK(i == NumBlocks);
}


TEST_CASE("Alloc Small Objects", "[heap]")      {testAllocRangeOfSizes(0,     500);}
TEST_CASE("Alloc Bigger Objects", "[heap]")     {testAllocRangeOfSizes(900,   500);}
TEST_CASE("Alloc Big Objects", "[heap]")        {testAllocRangeOfSizes(Object::LargeSize - 50, 100);}
TEST_CASE("Alloc Real Big Objects", "[heap]")   {testAllocRangeOfSizes(99990,  20);}
TEST_CASE("Alloc Huge Objects", "[heap]")       {testAllocRangeOfSizes(Object::MaxSize - 2,  2);}
