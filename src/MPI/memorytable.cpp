
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

#include "memorytable.hpp"
#include "communication.hpp"

#include <cmath>

namespace lpf {

MemoryTable :: MemoryTable( Communication & comm
        , mpi::IBVerbs & ibverbs
        )
    : m_memreg()
    , m_capacity( 0 )
    , m_added( 0, 0 )
    , m_ibverbs( ibverbs )
    , m_comm( comm )
{ (void) comm; }


MemoryTable :: Slot
MemoryTable :: addLocal( void * mem, std::size_t size )  // nothrow
{
    Memory rec( mem, size, m_ibverbs.regLocal( mem, size));
    return m_memreg.addLocalReg( rec);
}

MemoryTable :: Slot
MemoryTable :: addGlobal( void * mem, std::size_t size ) // nothrow
{ 
    Memory rec(mem, size, -1); 
    Slot slot = m_memreg.addGlobalReg(rec) ; 
    m_added.insert( slot );
    return slot;
}

void MemoryTable :: remove( Slot slot )   // nothrow
{
    if (m_added.contains(slot)) {
        m_added.erase(slot);
    }
    else {
        m_ibverbs.dereg( m_memreg.lookup(slot).slot );
    }
    m_memreg.removeReg( slot );
}


void MemoryTable :: reserve( size_t size ) // throws bad_alloc, strong safe
{
    m_memreg.reserve( size );
    size_t range = m_memreg.range();
    m_added.resize( range );
    m_ibverbs.resizeMemreg( size );

    m_capacity = size;
}

size_t MemoryTable :: capacity() const
{
    return m_capacity;
}

size_t MemoryTable :: range() const
{ 
    return m_memreg.range();
}

bool MemoryTable :: needsSync() const
{ 
    return !m_added.empty();
}

void MemoryTable :: sync(  ) 
{
    if ( !m_added.empty() )
    {
        // Register the global with IBverbs
        typedef DirtyList::iterator It;
        for ( It i = m_added.begin(); i != m_added.end(); ++i)
        {
            ASSERT( !isLocalSlot( *i ));
            void * base = m_memreg.lookup( *i).addr;
            size_t size = m_memreg.lookup( *i ).size;
            mpi::IBVerbs::SlotID s = m_ibverbs.regGlobal( base, size ); 
            m_memreg.update( *i ).slot = s;
        }

        m_comm.barrier();

        // clear the added list
        m_added.clear();
    }
}



} // namespace lpf
