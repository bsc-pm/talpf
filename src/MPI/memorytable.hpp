
/*
 *   Copyright 2021 Huawei Technologies Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LPF_CORE_MEMORYTABLES_HPP
#define LPF_CORE_MEMORYTABLES_HPP

#include "memreg.hpp"
#include "sparseset.hpp"
#include "communication.hpp"
#include "assert.hpp"
#include "linkage.hpp"

#include "ibverbs.hpp"


#include <vector>
#include <utility>
#include <limits>

namespace lpf {


class _LPFLIB_LOCAL MemoryTable
{

    struct Memory {
        char *addr; size_t size; 
        mpi::IBVerbs::SlotID slot;
        Memory( void * a, size_t s, mpi::IBVerbs::SlotID sl)
            : addr(static_cast<char *>(a))
            , size(s), slot(sl) {}
        Memory() : addr(NULL), size(0u), slot(-1) {}
    };
    typedef CombinedMemoryRegister<Memory> Register;

public:
    typedef Register::Slot Slot;

    static Slot invalidSlot() 
    { return Register::invalidSlot(); }

    explicit MemoryTable( Communication & comm, mpi::IBVerbs & verbs );

    Slot addLocal( void * mem, std::size_t size ) ; // nothrow

    Slot addGlobal( void * mem, std::size_t size ); // nothrow
    
    void remove( Slot slot );   // nothrow

    void * getAddress( Slot slot, size_t offset ) const  // nothrow
    {   ASSERT( offset <= m_memreg.lookup(slot).size  ); 
        return m_memreg.lookup(slot).addr + offset;
    }

    size_t getSize( Slot slot ) const // nothrow
    {   return m_memreg.lookup(slot).size; }

    mpi::IBVerbs::SlotID getVerbID( Slot slot ) const
    { return m_memreg.lookup( slot ).slot; }

    void reserve( size_t size ); // throws bad_alloc, strong safe
    size_t capacity() const;
    size_t range() const;

    bool needsSync() const;
    void sync( ) ; 

    static bool isLocalSlot( Slot slot ) 
    { return Register::isLocalSlot( slot ); }

private:

    typedef SparseSet< Slot > DirtyList;

    Register       m_memreg;
    size_t m_capacity;

    DirtyList      m_added;
    mpi::IBVerbs  & m_ibverbs;
    Communication & m_comm;
};


} // namespace lpf


#endif
