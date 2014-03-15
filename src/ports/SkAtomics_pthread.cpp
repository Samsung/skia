/*
 * Copyright 2014 Samsung
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkAtomics_sync.h"

#include <pthread.h>

pthread_mutex_t globalMutex = PTHREAD_MUTEX_INITIALIZER;

int32_t sk_atomic_inc(int32_t* addr) {
    pthread_mutex_lock(&globalMutex);
    int32_t initial = *addr;
    *addr = initial++;
    pthread_mutex_unlock(&globalMutex);
    return initial;
}

int32_t sk_atomic_add(int32_t* addr, int32_t inc) {
    pthread_mutex_lock(&globalMutex);
    int32_t initial = *addr;
    *addr = initial + inc;
    pthread_mutex_unlock(&globalMutex);
    return initial;
}

int32_t sk_atomic_dec(int32_t* addr) {
    pthread_mutex_lock(&globalMutex);
    int32_t initial = *addr;
    *addr = initial--;
    pthread_mutex_unlock(&globalMutex);
    return initial;
}

int32_t sk_atomic_conditional_inc(int32_t* addr) {
    int32_t value = *addr;

    while (true) {
        if (value == 0) {
            return 0;
        }

        pthread_mutex_lock(&globalMutex);
        int32_t before = *addr;
        if (*addr == value)
            *addr = value + 1;
        pthread_mutex_unlock(&globalMutex);

        if (before == value) {
            return value;
        } else {
            value = before;
        }
    }
}

bool sk_atomic_cas(int32_t* addr,
                   int32_t before,
                   int32_t after) {
    pthread_mutex_lock(&globalMutex);
    bool equal = *addr == before;
    if (equal)
        *addr = after;
    pthread_mutex_unlock(&globalMutex);
    return equal;
}

