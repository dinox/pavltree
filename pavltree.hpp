/*
 *  PTreeSet<T, Threads>
 *  pavltree.hpp
 *
 *  Created by Erik Henriksson on 21.11.13.
 *  Copyright (c) 2013 Erik Henriksson. All rights reserved.
 *
 *  This file is part of PAVLTree.
 *
 *  PAVLTree is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  PAVLTree is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with PAVLTree.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PAVL_TREE_TREE
#define PAVL_TREE_TREE

#include<iostream>
#include<atomic>
#include<queue>
#include<mutex>
#include<thread>
#include<iomanip>
#include<vector>
#include<cassert>
#include<atomic>
#include "hash.hpp"

namespace pavltree {

    /* Custom spinlock, much faster than mutex 
     */
    class SpinLock {
    private:
        std::atomic<bool> p;
    public:
        SpinLock();
        void lock();
        void unlock();
        bool try_lock();
    };

    typedef SpinLock Lock;

    /* PTreeSet
     * The main class. Assumes there is a hash function defined in "hash.hpp"
     * that has following signature
     *      int hash(T value)
     * This function is used when ordering items in the set.
     */
    template<typename T, int Threads>
    class PTreeSet {
    public:
        // Variables
        // Methods
        struct Node;
        PTreeSet<T, Threads> ();
        ~PTreeSet<T, Threads>();
        bool contains(T);
        int add(T);
        int remove(T);
        volatile Node *min();
        volatile Node *max();
        volatile Node *succ(Node*);
        volatile Node *prev(Node*);
        void verify();
    private:
        // Variables
        std::atomic<Node*> rootHolder;
        SpinLock balance_lock;
        // Methods
        void fixHeightAndRebalance(Node*);
        volatile Node *FindParent(int);
        bool tryInsert(Node*, Node*);
        void verify(Node*);
    };

    /* Node
     * Class for nodes of the tree. Not intended to be used outside the tree, if
     * you are not sure of what you are doing.
     */
    template<typename T, int Threads>
    struct PTreeSet<T, Threads>::Node {
        //Invariant: left->value < right->value
        std::atomic<Node*> left, right, parent;
        std::atomic<int> height;
        const int key;
        std::atomic<bool> has_value;
        // Methods
        Node (unsigned int);
        Node (unsigned int, Node*);
        ~Node();
        void rotateLeft();
        void rotateRight();
        void doubleRotateLeft();
        void doubleRotateRight();
        void fixHeight();
        int getHeight();
        int bf();
        Node copy();
    };

}

#endif //PAVL_TREE_TREE
