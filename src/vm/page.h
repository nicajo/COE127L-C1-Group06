#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "filesys/off_t.h"
#include "threads/thread.h"
#include <hash.h>

enum page_location{
	DISK = 1,	//set if page_data is in disk mmap/executable
	MEMORY =2,	//set if page_data in memory
	SWAP=4,		//set if pag_data in swap
	MMAP=8
};


struct addon_page_table{
	struct hash_elem h_elem;
	struct file *file;
	off_t ofs;
	size_t swap_idx;
	uint8_t *upage;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;
    enum page_location location;
};



bool
new_apt_entry (struct file *file, off_t ofs, uint8_t *upage, uint8_t *kpage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum page_location loc);


bool add_apt_entry(struct addon_page_table *new_entry);

void empty_apt();

void apt_delete(void *vaddr);

void* lookup_apt(void *addr);

void* lookup_other_apt(struct addon_page_table *apt, void *addr);

bool grow_stack(void *addr);

unsigned apt_hash_hash_function(const struct hash_elem *h_elem,
			void *aux UNUSED);
bool apt_hash_less_func(const struct hash_elem *a, const struct
		hash_elem *b, void *aux UNUSED);
void apt_destructor(struct hash_elem *h_elem, void *aux);

#endif
