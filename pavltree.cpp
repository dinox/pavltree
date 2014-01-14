/*
 *  PTreeSet<T, Threads>
 *  pavltree.cpp
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

#include "pavltree.hpp"

using namespace pavltree;

namespace pavltree {

    SpinLock::SpinLock() {
        this->p.store(false);
    }

    void SpinLock::lock()
    {
        while(!this->p.exchange(true));
        // Critical section
    }

    void SpinLock::unlock()
    {
        // End of critical section
        this->p.store(false); // unlock
    }

    bool SpinLock::try_lock()
    {
        return !this->p;
    }

    template<typename T, int Threads>
    PTreeSet<T, Threads>::PTreeSet() {
        rootHolder = new PTreeSet<T, Threads>::Node(0, NULL);
        return;
    }

    template<typename T, int Threads>
    PTreeSet<T, Threads>::~PTreeSet() {
        delete rootHolder.load()->left.load();
    }

    /*
     * Verifier for consistency of the tree
     *
     */
    template<typename T, int Threads>
    void PTreeSet<T, Threads>::verify() {
        assert(!this->rootHolder.load()->parent.load());
        assert(this->rootHolder.load()->left.load() == this->rootHolder.load()->right.load());
        this->verify(this->rootHolder.load()->left.load());
    }

    template<typename T, int Threads>
    void PTreeSet<T, Threads>::verify(PTreeSet<T, Threads>::Node *node) {
        if (!node)
            return;
        assert(node->parent.load()->left.load() == node
               || node->parent.load()->right.load() == node);
        assert(!node->left.load() || node->left.load()->parent.load() == node);
        assert(!node->left.load() || node->left.load()->key < node->key);
        assert(!node->right.load() || node->right.load()->parent.load() == node);
        assert(!node->right.load() || node->right.load()->key > node->key);
        assert(node->bf() < 2 && node->bf() > -2);
        //cout << setw(3) << node->key << " ";
        //cout << setfill('.') << setw(16) << hex << node->version << dec << setfill(' ') << endl;
        verify(node->left.load());
        verify(node->right.load());
    }

    /* Contains
     * Parameters: The value to find
     * Returns: True if value present in tree, otherwise false
     * Complexity: O(log n)
     *
     */ 
    template<typename T, int Threads>
    bool PTreeSet<T, Threads>::contains(T value) {
        int key = hash(value);
        for (volatile Node *curr = rootHolder.load()->left; curr != NULL; ) {
            if (curr->key == key) {
                if (curr->has_value)
                    return true;
                else
                    return false;
            } else if (curr->key > key) {
                curr = curr->left;
            } else {
                curr = curr->right;
            }
        }
        return false;
    }

    /* Add
     * Inserts a value into the tree
     * Parameters: The value to add in the tree
     * Returns: True if value was added to the tree, otherwise false
     * Complexity: O(log n)
     */
    template<typename T, int Threads>
    int PTreeSet<T, Threads>::add(T value) {
        int key = hash(value);
        Node * node;
    Retry:
        // Ensure root is not null, otherwise create a root.
        if (!rootHolder.load()->left) {
            rootHolder.load()->left = new Node (key, rootHolder);
            rootHolder.load()->right.store(rootHolder.load()->left);
            node = rootHolder.load()->left;
        } else {
            Node *parent = rootHolder.load()->left;
            for (;;) {
                if (parent->key == key) {
                    // Already exists in tree
                    if (parent->has_value)
                        return false;
                    else {
                        parent->has_value.store(true);
                        return true;
                    }
                } else if (parent->key > key) {
                    if (parent->left) {
                        parent = parent->left;
                        if (!parent)
                          goto Retry;
                    } else {
                        node = new Node (key, parent);
                        if (!tryInsert(parent, node)) {
                            goto Retry;
                        }
                        break;
                    }
                    // parent->key < key
                } else {
                    if (parent->right) {
                        parent = parent->right;
                        if (!parent)
                          goto Retry;
                    } else {
                        node = new Node (key, parent);
                        if (!tryInsert(parent, node)) {
                            goto Retry;
                        }
                        break;
                    }
                }
            }
        }
        {
            std::lock_guard<Lock> l(balance_lock);
            fixHeightAndRebalance(node);
        }
        return true;
    }

    template<typename T, int Threads>
    bool PTreeSet<T, Threads>::tryInsert(Node* parent, Node* node) {
        std::lock_guard<Lock> l(balance_lock);
        if (parent->key > node->key) {
            Node* pL = parent->left;
            if (pL != nullptr) {
                return false;
            }
            parent->left.store(node);
        } else if (parent->key < node->key) {
            Node* pR = parent->right;
            if (pR != nullptr) {
                return false;
            }
            parent->right.store(node);
        } else {
            return false;
        }
        return true;
    }

    /* Remove
     * Removes a value from the tree
     * Parameters: The value to remove
     * Returns: True if value was removed, otherwise false
     * Complexity: O(log n)
     */
    template<typename T, int Threads>
    int PTreeSet<T, Threads>::remove(T value) {
        int key = hash(value);
        for (volatile Node *curr = rootHolder.load()->left; curr != NULL; ) {
            if (curr->key == key) {
                if (curr->has_value) {
                    curr->has_value.store(false);
                    return true;
                } else
                    return false;
            } else if (curr->key > key) {
                curr = curr->left;
            } else {
                curr = curr->right;
            }
        }
        return false;
    }

    template<typename T, int Threads>
    void PTreeSet<T, Threads>::fixHeightAndRebalance(Node *node) {
        int key = node->key;
        for (; node != rootHolder;) {
            assert(node != rootHolder);
            node->fixHeight();
            if (node->bf() <= -2) {
                if (key > node->right.load()->key) {
                    node->rotateLeft();
                } else {
                    node->doubleRotateLeft();
                }
            } else if (node->bf() >= 2) {
                if (key < node->left.load()->key) {
                    node->rotateRight();
                } else {
                    node->doubleRotateRight();
                }
            }
            node = node->parent;
        }
        rootHolder.load()->right.store(rootHolder.load()->left.load());
    }

    template<typename T, int Threads>
    PTreeSet<T, Threads>::Node::Node(unsigned int key) : key(key), has_value(true) {
        this->left = NULL;
        this->right = NULL;
        this->parent = NULL;
        this->height = 0;
    }

    template<typename T, int Threads>
    PTreeSet<T, Threads>::Node::Node(unsigned int key, Node *parent) : key(key), has_value(true) {
        this->left = NULL;
        this->right = NULL;
        this->parent = parent;
        this->height = 0;
    }

    template<typename T, int Threads>
    PTreeSet<T, Threads>::Node::~Node() {
        if (this->left) {
            delete this->left.load();
        }
        if (this->right) {
            delete this->right.load();
        }
    }
    template<typename T, int Threads>
    void PTreeSet<T, Threads>::Node::rotateLeft() {
        assert(this->right);
        PTreeSet<T, Threads>::Node *tR = this->right.load();
        PTreeSet<T, Threads>::Node *tP = this->parent.load();
        PTreeSet<T, Threads>::Node *tRL = tR->left.load();

        this->right.store(tRL);
        tR->left.store(this);
        if (tP->left == this) {
            tP->left.store(tR);
        } else {
            tP->right.store(tR);
        }
        tR->parent.store(tP);
        this->parent.store(tR);
        if (tRL) {
            tRL->parent.store(this);
        }
        this->fixHeight();
        tR->fixHeight();
    }

    template<typename T, int Threads>
    void PTreeSet<T, Threads>::Node::rotateRight() {
        assert(this->left);
        PTreeSet<T, Threads>::Node *tL = this->left.load();
        PTreeSet<T, Threads>::Node *tP = this->parent.load();
        PTreeSet<T, Threads>::Node *tLR = tL->right.load();
        this->left.store(tLR);
        tL->right.store(this);
        if (tP->left.load() == this) {
            tP->left.store(tL);
        } else {
            tP->right.store(tL);
        }
        tL->parent.store(tP);
        this->parent.store(tL);
        if (tLR) {
            tLR->parent.store(this);
        }
        this->fixHeight();
        tL->fixHeight();
    }

    template<typename T, int Threads>
    void PTreeSet<T, Threads>::Node::doubleRotateLeft() {
        if (this->right.load()->left.load())
            this->right.load()->rotateRight();
        return this->rotateLeft();
    }

    template<typename T, int Threads>
    void PTreeSet<T, Threads>::Node::doubleRotateRight() {
        if (this->left.load()->right.load())
            this->left.load()->rotateLeft();
        return this->rotateRight();
    }

    template<typename T, int Threads>
    void PTreeSet<T, Threads>::Node::fixHeight() {
        int lh = this->left.load() == NULL ? 0 : 1 + this->left.load()->height,
        rh = this->right.load() == NULL ? 0 : 1 + this->right.load()->height;
        this->height = lh>rh ? lh : rh;
    }

    template<typename T, int Threads>
    int PTreeSet<T, Threads>::Node::bf () {
        int rh = this->right.load() == NULL ? 0 : 1 + this->right.load()->height,
        lh = this->left.load() == NULL ? 0 : 1 + this->left.load()->height;
        return lh-rh;
    }

}
