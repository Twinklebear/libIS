// ======================================================================== //
// Copyright 2018 Intel Corporation                                         //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include <array>
#include <string>
#include <type_traits>
#include <vector>

namespace is {

// TODO WILL: This is just a dump in of my messaging layer stuff from RVK
// https://github.com/Twinklebear/rvk
// TODO: Maybe move to have RVK's messaging utilities be a dependency of this?

class Reader {
public:
    virtual ~Reader();
    virtual void read(char *data, const size_t nbytes) = 0;
};

class Writer {
public:
    virtual ~Writer();
    virtual void write(const char *data, const size_t nbytes) = 0;
};

class ReadBuffer : public Reader {
    size_t begin;
    std::vector<char> buffer;

public:
    ReadBuffer(const std::vector<char> &buffer);
    void read(char *data, const size_t nbytes) override;
};

class WriteBuffer : public Writer {
    size_t end;
    std::vector<char> buffer;

public:
    WriteBuffer(const size_t initial_capacity = 4 * 1024);
    void write(const char *data, const size_t nbytes) override;
    char *data();
    size_t size() const;
};
}

// Avoid needing C++14, define our own enable_if_t
// http://en.cppreference.com/w/cpp/types/enable_if
template <bool B, class T = void>
using enable_if_t = typename std::enable_if<B, T>::type;

is::Reader &operator>>(is::Reader &b, std::string &s);

template <typename T>  //, typename = enable_if_t<std::is_trivially_copyable<T>::value>>
is::Reader &operator>>(is::Reader &b, T &t)
{
    b.read(reinterpret_cast<char *>(&t), sizeof(T));
    return b;
}

template <typename T>  //, typename = enable_if_t<std::is_trivially_copyable<T>::value>>
is::Reader &operator>>(is::Reader &b, std::vector<T> &t)
{
    uint64_t size = 0;
    b >> size;
    t = std::vector<T>(size, T{});
    for (auto &e : t) {
        b >> e;
    }
    return b;
}

template <typename T, size_t N>  //, typename = enable_if_t<std::is_trivially_copyable<T>::value>>
is::Reader &operator>>(is::Reader &b, std::array<T, N> &t)
{
    for (auto &e : t) {
        b >> e;
    }
    return b;
}

is::Writer &operator<<(is::Writer &b, const std::string &s);

template <typename T>  //, typename = enable_if_t<std::is_trivially_copyable<T>::value>>
is::Writer &operator<<(is::Writer &b, const T &t)
{
    b.write(reinterpret_cast<const char *>(&t), sizeof(T));
    return b;
}

template <typename T>  //, typename = enable_if_t<std::is_trivially_copyable<T>::value>>
is::Writer &operator<<(is::Writer &b, const std::vector<T> &t)
{
    const uint64_t size = t.size();
    b << size;
    for (const auto &e : t) {
        b << e;
    }
    return b;
}

template <typename T, size_t N>  //, typename = enable_if_t<std::is_trivially_copyable<T>::value>>
is::Writer &operator>>(is::Writer &b, const std::array<T, N> &t)
{
    for (auto &e : t) {
        b << e;
    }
    return b;
}
