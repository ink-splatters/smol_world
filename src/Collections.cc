//
// Collections.cc
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

#include "Collections.hh"
#include <iomanip>
#include <iostream>

namespace snej::smol {


#pragma mark - VECTOR:


void Vector::_setSize(heapsize sz) {
    assert(sz <= capacity());
    *Collection::begin() = int(sz);
}

bool Vector::insert(Value val, heapsize pos) {
    heapsize sz = size();
    assert(pos <= sz);
    if (sz >= capacity())
        return false;
    // Move items up:
    auto items = this->items();
    iterator dst = &items[pos];
    for (iterator i = items.end(); i > dst; --i)
        i[0] = i[-1];
    // Store the value:
    *dst = val;
    _setSize(sz + 1);
    return true;
}

bool Vector::append(Value val) {
    if (auto sz = size(); sz >= capacity()) {
        return false;
    } else {
        allItems()[sz + 1] = val;
        _setSize(sz + 1);
        return true;
    }
}


#pragma mark - DICT:


static bool keyCmp(DictEntry const& a, DictEntry const& b) {
    return a.key.block() > b.key.block();   // reverse order
}

static bool keyValueCmp(DictEntry const& a, Block const* b) {
    return a.key.block() > b;   // reverse order
}

DictEntry& DictEntry::operator=(DictEntry && other) {
    (Val&)key = other.key;
    value = other.value;
    return *this;
}

void swap(DictEntry const&, DictEntry const&);



// Returns the DictEntry with this key, or else the pos where it should go (DictEntry with next higher key),
// or else the end.
static DictEntry* _findEntry(slice<DictEntry> entries, Block const* key) {
    return (DictEntry*) std::lower_bound(entries.begin(), entries.end(), key, keyValueCmp);
}


void Dict::dump(std::ostream& out) const {
    std::string_view prefix = "\t[";
    for (auto &entry : allItems()) {
        out << prefix << std::setw(10) << (void*)entry.key.block() << " " << entry.key
        << " = " << entry.value;
        prefix = "\n\t ";
    }
    if (capacity() == 0)
        out << prefix;
    out << " ]\n";
}

void Dict::dump() const {dump(std::cout);}



void Dict::sort(size_t count) {
    std::sort(begin(), begin() + count, keyCmp);
}


slice<DictEntry> Dict::items() const {
    slice<DictEntry> all = allItems();
    return {all.begin(), _findEntry(all, nullptr)};
}


Val* Dict::find(Symbol key) {
    slice<DictEntry> all = allItems();
    if (DictEntry *ep = _findEntry(all, key.block()); ep != all.end() && ep->key == Value(key))
        return &ep->value;
    else
        return nullptr;
}


bool Dict::set(Symbol key, Value value, bool insertOnly) {
    slice<DictEntry> all = allItems();
    if (DictEntry *ep = _findEntry(all, key.block()); ep == all.end()) {
        return false;   // not found, and would go after last item (so dict must be full)
    } else if (ep->key == Value(key)) {
        if (insertOnly) return false;
        ep->value = value;
        return true;
    } else if (all.back().key == nullval) {
        for (auto p = all.end()-1; p > ep; --p) // can't use memmove bc of damned relative ptrs
            p[0] = std::move(p[-1]);
        (Val&)ep->key = key;
        ep->value = value;
        return true;
    } else {
        return false; // not found, but no room to insert
    }
}


bool Dict::replace(Symbol key, Value newValue) {
    if (Val *valp = find(key)) {
        *valp = newValue;
        return true;
    } else {
        return false;
    }
}


bool Dict::remove(Symbol key) {
    slice<DictEntry> all = allItems();
    if (DictEntry *ep = _findEntry(all, key.block()); ep != all.end() && ep->key == Value(key)) {
        for (auto p = ep + 1; p < all.end(); ++p) // can't use memmove bc of damned relative ptrs
            p[-1] = std::move(p[0]);
        new (&all.back()) DictEntry {};
        return true;
    } else {
        return false;
    }
}


#pragma mark - I/O:


std::ostream& operator<<(std::ostream& out, Null const& val) {
    return out << (val.isNull() ? "null" : "nullish");
}

std::ostream& operator<<(std::ostream& out, Bool const& val) {
    return out << (val ? "true" : "false");
}

std::ostream& operator<<(std::ostream& out, Int const& val) {
    return out << val.asInt();
}

std::ostream& operator<<(std::ostream& out, BigInt const& val) {
    return out << val.asInt();
}

std::ostream& operator<<(std::ostream& out, Float const& val) {
    if (val.isDouble())
        return out << val.asDouble();
    else
        return out << val.asFloat();
}

std::ostream& operator<<(std::ostream& out, String const& str) {
    out << "“" << str.str() << "”";
    return out;
}

std::ostream& operator<<(std::ostream& out, Symbol const& str) {
    out << "«" << str.str() << "»";
    return out;
}

std::ostream& operator<<(std::ostream& out, Blob const& blob) {
    out << "Blob<" << std::hex;
    for (byte b : blob.bytes().upTo(32)) {
        out << std::setw(2) << unsigned(b);
    }
    if (blob.size() > 32)
        out << " …";
    out << std::dec << ">";
    return out;
}

std::ostream& operator<<(std::ostream& out, Array const& arr) {
    out << "Array[" << arr.size();
    if (!arr.empty()) {
        out << ": ";
        int n = 0;
        for (Val const& val : arr) {
            if (n++) out << ", ";
            out << val;
        }
    }
    return out << "]";
}

std::ostream& operator<<(std::ostream& out, Vector const& vec) {
    out << "Vector[" << vec.size();
    if (!vec.empty()) {
        out << ": ";
        int n = 0;
        for (Val const& val : vec) {
            if (n++) out << ", ";
            out << val;
        }
    }
    return out << "]";
}

std::ostream& operator<<(std::ostream& out, Dict const& dict) {
    out << "Dict{" << dict.size();
    int n = 0;
    for (auto &entry : dict) {
        if (n++) out << ", ";
        out << entry.key << ": " << entry.value;
    }
    return out << "}";
}

std::ostream& operator<< (std::ostream& out, Value val) {
    val.visit([&](auto t) {out << t;});
    return out;
}

}
