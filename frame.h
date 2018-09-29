#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdio.h>
#include <stdlib.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/off_t.h"

struct frame{
	struct thread *owner;
	void *frame_no;
	void *page_no;
	bool is_swappable;
	struct list_elem elem;
};

struct frame_table{
	struct list allframes;
	size_t count;
	struct lock frame_mutex;
};
void init_frame_table();
//void* get_frame(enum palloc_flags flags, bool is_swappable);
//bool load_frame(struct file *file,void *upage,
//		off_t ofs,uint32_t read_bytes, uint32_t zero_bytes, bool writable,
//		enum page_location loc,bool is_swappable);
bool install_frame(void *upage, void *kpage, bool writable);
void frame_set_swappable(void *frame_no,bool flag);
void frame_thread_delete(struct thread *t);
#endif
