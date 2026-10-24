// ostypes.h
//
// Created by T.Kupilik on $Date: $.
//
// Copyright (c) 2011 T.Kupilik
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// Neither the name of the project's author nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// OS types definitions.

// -----------------------------------------------------------------------------

#ifndef __OS_OSTYPES_H
#define __OS_OSTYPES_H

#include <stddef.h>
#include <stdint.h>

namespace Rntm {

// -----------------------------------------------------------------------------

/// Task's main function

typedef void (*MainFnc)(void * param);

/// Size of array [items]

#define NB_ITEMS(a) (sizeof(a)/sizeof((a)[0]))

// -----------------------------------------------------------------------------

/// List template class.

template <class Type>
class OsList
{
public:
    /// Node class used to build list.
    class Node
    {
    public:
        /// Default contructor.
        Node() throw()
        {
            prev = next = this;
        }

        /// Contructor.
        Node(const Type & val) throw()
        {
            prev = next = this;
            this->val = val;
        }
    
        /// Destructor
        ~Node() throw()
        {
            Remove();
        }
    
        /// Removes node from list.
        void Remove()
        {
            next->prev = prev;
            prev->next = next;
            prev = next = this;
        }
        
        /// Appends node into list before this node.
        void AppendBefore(Node & node)
        {
            node.next       = this;
            node.prev       = prev;
            prev->next      = &node;
            prev            = &node;
        }

    public:
        Node   *next;                   ///< Next item in list.
        Node   *prev;                   ///< Previous item in list.
        Type    val;                    ///< Item value.
    };
    
public:
    /// @return Ptr to first node in list.
    Node * Head()
    {
        return (m_list.next);
    }

    /// @return Ptr to last node in list (main node).
    Node * Tail()
    {
        return (&m_list);
    }
    
    /// Returns true if list is empty.
    bool IsEmpty()
    {
        return (Head() == Tail());
    }

    /// Add item to end of list.
    void AddTail(Node & node)
    {
        m_list.AppendBefore(node);
    }

    /// Add item to the begin of list.
    void AddHead(Node & node)
    {
        Head()->AppendBefore(node);
    }

protected:
    Node    m_list;                     ///< Main list node.
};

} // namespace Rntm

// -----------------------------------------------------------------------------

#endif // __OS_OSTYPES_H

// -----------------------------------------------------------------------------
