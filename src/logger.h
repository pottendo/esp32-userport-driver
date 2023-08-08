/* -*-c++-*-
 * This file is part of FE playground.
 * 
 * FE playground is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * FE playground is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with FE playground.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef __logger_h__
#define __logger_h__

#include <Arduino.h>
#include <stdlib.h>
#define PRIORITY_LOG uxTaskPriorityGet(nullptr)

void log_msg(const String &s);
void log_msg(const char *s, ...);
void log_msg_isr(bool from_isr, const char *s, ...);
void setup_log(void);
void loop_log(void);
BaseType_t log_get_stack_wm(void);

template <class T>
struct Mallocator
{
    typedef T value_type;
    typedef value_type *pointer;
    typedef const value_type *const_pointer;
    typedef value_type &reference;
    typedef const value_type &const_reference;

    Mallocator() = default;
    template <class U>
    constexpr Mallocator(const Mallocator<U> &) noexcept {}
    template <typename U>
    struct rebind
    {
        //        using other = Mallocator<U>;
        typedef Mallocator<U> other;
    };

    T *
    allocate(std::size_t n)
    {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_array_new_length();
#ifdef BOARD_HAS_PSRAM
        if (auto p = static_cast<T *>(ps_malloc(n * sizeof(T))))
            return p;
#else
        if (auto p = static_cast<T *>(malloc(n * sizeof(T))))
            return p;
#endif
        throw std::bad_alloc();
    }
    void deallocate(T *p, std::size_t n) noexcept { free(p); }
    void construct(T *p, const T &val) { new (p) T(val); }
    void destroy(T *p) { p->~T(); }

private:
#if 0
    void report(T *p, std::size_t n, bool alloc = true) const
    {
        std::cout << (alloc ? "Alloc: " : "Dealloc: ") << sizeof(T) * n
                  << " bytes at " << std::hex << std::showbase
                  << reinterpret_cast<void *>(p) << std::dec << '\n';
    }
#endif
};

template <class T, class U>
bool operator==(const Mallocator<T> &, const Mallocator<U> &) { return true; }
template <class T, class U>
bool operator!=(const Mallocator<T> &, const Mallocator<U> &) { return false; }

#endif