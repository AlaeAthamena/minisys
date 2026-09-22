#include "kvstore.h"

void lru_add(lru_list_t *lru, kv_entry_t *entry) {
    if (!lru || !entry) return;
    entry->lru_prev = NULL;
    entry->lru_next = lru->head;
    if (lru->head) {
        lru->head->lru_prev = entry;
    }
    lru->head = entry;
    if (!lru->tail) {
        lru->tail = entry;
    }
    lru->count++;
}

void lru_remove(lru_list_t *lru, kv_entry_t *entry) {
    if (!lru || !entry) return;
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        lru->head = entry->lru_next;
    }

    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        lru->tail = entry->lru_prev;
    }

    entry->lru_prev = NULL;
    entry->lru_next = NULL;
    lru->count--;
}

void lru_touch(lru_list_t *lru, kv_entry_t *entry) {
    if (!lru || !entry || lru->head == entry) return;
    lru_remove(lru, entry);
    lru_add(lru, entry);
}

kv_entry_t *lru_pop_tail(lru_list_t *lru) {
    if (!lru || !lru->tail) return NULL;
    kv_entry_t *tail = lru->tail;
    lru_remove(lru, tail);
    return tail;
}
