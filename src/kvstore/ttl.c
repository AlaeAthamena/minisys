#include "kvstore.h"

static void swap_entries(ttl_heap_t *heap, size_t i, size_t j) {
    kv_entry_t *tmp = heap->data[i];
    heap->data[i] = heap->data[j];
    heap->data[j] = tmp;
    heap->data[i]->heap_idx = i;
    heap->data[j]->heap_idx = j;
}

static void heapify_up(ttl_heap_t *heap, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (heap->data[idx]->expire_at < heap->data[parent]->expire_at) {
            swap_entries(heap, idx, parent);
            idx = parent;
        } else {
            break;
        }
    }
}

static void heapify_down(ttl_heap_t *heap, size_t idx) {
    size_t size = heap->size;
    while (2 * idx + 1 < size) {
        size_t left = 2 * idx + 1;
        size_t right = 2 * idx + 2;
        size_t smallest = idx;

        if (left < size && heap->data[left]->expire_at < heap->data[smallest]->expire_at) {
            smallest = left;
        }
        if (right < size && heap->data[right]->expire_at < heap->data[smallest]->expire_at) {
            smallest = right;
        }
        if (smallest != idx) {
            swap_entries(heap, idx, smallest);
            idx = smallest;
        } else {
            break;
        }
    }
}

bool ttl_heap_push(ttl_heap_t *heap, kv_entry_t *entry) {
    if (heap->size >= heap->capacity) {
        size_t new_cap = heap->capacity == 0 ? 16 : heap->capacity * 2;
        kv_entry_t **new_data = realloc(heap->data, sizeof(kv_entry_t *) * new_cap);
        if (!new_data) return false;
        heap->data = new_data;
        heap->capacity = new_cap;
    }

    size_t idx = heap->size++;
    heap->data[idx] = entry;
    entry->heap_idx = idx;
    heapify_up(heap, idx);
    return true;
}

kv_entry_t *ttl_heap_peek(ttl_heap_t *heap) {
    if (heap->size == 0) return NULL;
    return heap->data[0];
}

kv_entry_t *ttl_heap_pop(ttl_heap_t *heap) {
    if (heap->size == 0) return NULL;
    kv_entry_t *top = heap->data[0];
    heap->size--;
    if (heap->size > 0) {
        heap->data[0] = heap->data[heap->size];
        heap->data[0]->heap_idx = 0;
        heapify_down(heap, 0);
    }
    top->heap_idx = (size_t)-1;
    return top;
}

void ttl_heap_remove(ttl_heap_t *heap, kv_entry_t *entry) {
    if (entry->heap_idx == (size_t)-1 || entry->heap_idx >= heap->size) return;
    size_t idx = entry->heap_idx;
    heap->size--;
    if (idx < heap->size) {
        heap->data[idx] = heap->data[heap->size];
        heap->data[idx]->heap_idx = idx;
        heapify_down(heap, idx);
        heapify_up(heap, idx);
    }
    entry->heap_idx = (size_t)-1;
}
