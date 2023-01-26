# wyhash

`wyhash.h` and `wyhash32.h` were copied from [the wyhash GitHub repo][1], 
commit ea3b25e from 2022-11-02.

## What is it?

From the wyhash [README][2]:

>wyhash and wyrand are the ideal 64-bit hash function and PRNG respectively:

>solid: wyhash passed SMHasher, wyrand passed BigCrush, practrand.

>portable: 64-bit/32-bit system, big/little endian.

>fastest: Efficient on 64-bit machines, especially for short keys.

>simplest: In the sense of code size.

>salted: We use dynamic secret to avoid intended attack.

>wyhash is the default hashing algorithm of the great Zig, V, Nim and Go (since 1.17) language.

... it's also used by [Abseil][3]'s [hashmap][4].

[1]: https://github.com/wangyi-fudan/wyhash/blob/master/wyhash32.h
[2]: https://github.com/wangyi-fudan/wyhash
[3]: https://github.com/abseil/abseil-cpp
[4]: https://github.com/abseil/abseil-cpp/blob/master/absl/hash/internal/wyhash.cc
