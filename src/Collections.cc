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
#include <iostream>


// just like DictEntry except `key` isn't const, allowing it to be swapped/assigned.
struct MutEntry {
    Val key;
    Val value;
    operator DictEntry&() {return *(DictEntry*)this;}    // so that Dict::keyCmp will work
};


// Returns the DictEntry with this key, or else the pos where it should go (DictEntry with next higher key),
// or else the end.
static DictEntry* _findEntry(slice<DictEntry> entries, Val key) {
    return std::lower_bound(entries.begin(), entries.end(), DictEntry{key, nullval}, Dict::keyCmp);
}


void Dict::sort(size_t count) {
    std::sort((MutEntry*)begin(), (MutEntry*)begin() + count, Dict::keyCmp);
}


slice<DictEntry> Dict::items() {
    slice<DictEntry> all = allItems();
    return {all.data, uint32_t(_findEntry(all, nullval) - all.data)};
}


Val* Dict::find(Val key) {
    slice<DictEntry> all = allItems();
    if (DictEntry *ep = _findEntry(all, key); ep != all.end() && ep->key == key)
        return &ep->value;
    else
        return nullptr;
}


bool Dict::set(Val key, Val value) {
    slice<DictEntry> all = allItems();
    if (DictEntry *ep = _findEntry(all, key); ep == all.end()) {
        return false; // not found and full!
    } else if (ep->key == key) {
        ep->value = value;
        return true;
    } else {
        assert(!full());
        ::memmove(ep + 1, ep, (all.end() - ep - 1) * sizeof(DictEntry));
        new (ep) DictEntry {key, value};
        return true;
    }
}


bool Dict::replace(Val key, Val newValue) {
    if (Val *valp = find(key)) {
        *valp = newValue;
        return true;
    } else {
        return false;
    }
}


bool Dict::remove(Val key) {
    slice<DictEntry> all = allItems();
    if (DictEntry *ep = _findEntry(all, key); ep != all.end()) {
        ::memmove(ep, ep + 1, (all.end() - ep) * sizeof(DictEntry));
        new (ep) DictEntry {nullval, nullval};
        return true;
    } else {
        return false;
    }
}



std::ostream& operator<<(std::ostream& out, String const* str) {
    out << "String[“" << str->get() << "”]";
    return out;
}

std::ostream& operator<<(std::ostream& out, Array const* arr) {
    out << "Array[" << arr->count();
    if (!arr->empty()) {
        out << ": ";
        int n = 0;
        for (Val val : *arr) {
            if (n++) out << ", ";
            out << val;
        }
    }
    return out << "]";
}


std::ostream& operator<<(std::ostream& out, Dict const* dict) {
    out << "Dict{" << dict->count();
    int n = 0;
    for (auto &entry : *dict) {
        if (n++) out << ", ";
        out << entry.key << ": " << entry.value;
    }
    return out << "}";
}


std::ostream& operator<< (std::ostream& out, Object const* obj) {
    switch (auto type = obj->type()) {
        case Type::String:  return out << obj->as<String>();
        case Type::Array:   return out << obj->as<Array>();
        case Type::Dict:    return out << obj->as<Dict>();
        default:            return out << TypeName(type) << "[]";
    }
}
