#include "vm/frame.h"
#include <stdio.h>
#include <stdlib.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include <bitmap.h>


static struct frame_table frame_table;

static bool try_evicting();

static struct list_elem* find_frame(void *frame_no){
	//printf("find_frame\n");
	struct list_elem *e;
	struct frame *f=NULL;
	for(e=list_begin(&frame_table.allframes);e!=list_end (&frame_table.allframes);
			  	           e = list_next (e))
	{
		f=list_entry(e,struct frame,elem);
		if(f->frame_no==frame_no)
			return e;
	}
	return NULL;
}

void init_frame_table(){
	list_init(&frame_table.allframes);
	frame_table.count=0;
	lock_init(&frame_table.frame_mutex);
}

void frame_table_done(){
	//empty allframe list
}

void* get_frame(enum palloc_flags flags, bool is_swappable){
	//printf("enter get_frame\n");
	lock_acquire(&frame_table.frame_mutex);
	void* frame_no= palloc_get_page(flags);
	if(!frame_no){
		//printf("trying new frame\n");
		if(!try_evicting()){
			PANIC("SWAP FULL");
		//	printf("bhargyaaaaaa\n");
		}
		frame_no= palloc_get_page(flags);
	}
	if(frame_no){
		struct frame *f=malloc(sizeof(struct frame));
		if(!f)
			return NULL;
		f->frame_no=frame_no;
		f->owner=thread_current();
		f->page_no=NULL;
		f->is_swappable=is_swappable;
		frame_table.count++;
		list_push_back(&frame_table.allframes,&f->elem);
	}
	lock_release(&frame_table.frame_mutex);
	return frame_no;
}

void free_frame(void *frame_no){
	//printf("free_frame\n");
	lock_acquire(&frame_table.frame_mutex);
	struct list_elem *f=find_frame(frame_no);
	if(!f)
		return;
	list_remove(f);
	palloc_free_page(frame_no);
	free(list_entry(f,struct frame,elem));
	lock_release(&frame_table.frame_mutex);
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */

bool install_frame(void *upage, void *kpage, bool writable){
	//printf("install_frame\n");
	lock_acquire(&frame_table.frame_mutex);
	struct frame *f=list_entry(find_frame(kpage),struct frame,elem);
	bool success;
	if(!f)
		success=false;
	else{
		struct thread *t = thread_current ();
		f->page_no=upage;
		/* Verify that there's not already a page at that virtual
				     address, then map our page there. */
		success=(pagedir_get_page (t->pagedir, upage) == NULL
				          && pagedir_set_page (t->pagedir, upage, kpage, writable));
	}
	lock_release(&frame_table.frame_mutex);
	return success;
}

void frame_set_swappable(void *frame_no,bool flag){
	lock_acquire(&frame_table.frame_mutex);
	//printf("what the hell\n");
	if(!frame_no)
		return;
	struct frame *f=list_entry(find_frame(frame_no),struct frame,elem);
	//printf("what the hell\n");
	if(f)
		f->is_swappable=flag;
	lock_release(&frame_table.frame_mutex);
}

bool load_frame(struct file *file,void *upage,
		off_t ofs,uint32_t read_bytes, uint32_t zero_bytes, bool writable,
		enum page_location loc,bool is_swappable){
	void *kpage;
	bool success=false;
		if((loc == DISK) || (loc == MMAP)){
			if(!file)
				file=thread_current()->my_stats.my_exectutable;
			if(!file)
				goto done;
			kpage = get_frame (PAL_USER,is_swappable);
			if (kpage == NULL){
				goto done;
			}
			bool file_lock_status=get_file_lock();
			file_seek(file,ofs);
			if (file_read (file, kpage, read_bytes) != (int) read_bytes){
				free_frame(kpage);
				release_file_lock(file_lock_status);
				goto done;
			}
			release_file_lock(file_lock_status);
			memset (kpage + read_bytes, 0, zero_bytes);
			if (!install_frame(upage, kpage, writable)){
				free_frame(kpage);
				goto done;
			}
			success= true;
		}
		else if(loc == MEMORY){
			kpage=get_frame(PAL_USER|PAL_ZERO, is_swappable);
			if(!kpage)
				goto done;
			if(!install_frame(upage,kpage, writable)){
				free_frame(kpage);
				goto done;
			}
			success= true;
		}
		else if(loc == SWAP){
			kpage=get_frame(PAL_USER|PAL_ZERO,is_swappable);
			if(!kpage)
				goto done;
			struct addon_page_table *entry=lookup_apt(upage);
			if(!entry)
				goto done;
			move_to_frame_table(entry->swap_idx,kpage);
			entry->swap_idx=-1;
			if(!install_frame(upage,kpage, writable)){
				free_frame(kpage);
				goto done;
			}
			entry->location=MEMORY;
			success= true;
			//printf("loaded from swap at %0x\n",upage);
			//printf("%s\n", *(int *)(int *)0xbfffffe4);
			goto done;
		}
	done:;
	return success;
}

bool page_eviction_updates(struct frame *f){
	//printf("page_eviction_updates\n");
	bool success=false;
	lock_acquire(&f->owner->apt_lock);

	struct addon_page_table *entry = lookup_other_apt(&f->owner->apt,f->page_no);
	if(!entry)
		goto done;
	//printf("page_eviction_updates1\n");
	if(entry->location == DISK){
		if(!pagedir_is_dirty(f->owner->pagedir,f->page_no)){
			pagedir_clear_page(f->owner->pagedir,f->page_no);
			success= true;
			goto done;
		}
		pagedir_clear_page(f->owner->pagedir,f->page_no);
		size_t idx=move_to_swap(f->frame_no);
		if(idx==BITMAP_ERROR)
			goto done;
		entry->swap_idx=idx;
		entry->location=SWAP;
		success= true;
		goto done;
	}
	//printf("page_eviction_updates2\n");
	if(entry->location == MMAP){
		if(!pagedir_is_dirty(f->owner->pagedir,f->page_no)){
			pagedir_clear_page(f->owner->pagedir,f->page_no);
			success= true;
			goto done;
		}
		pagedir_clear_page(f->owner->pagedir,f->page_no);
		bool status=get_file_lock();
		file_seek(entry->file,entry->ofs);
		file_write(entry->file,f->frame_no,entry->read_bytes);
		release_file_lock(status);
		success= true;
		goto done;
	}
	//printf("page_eviction_updates3\n");
	if(entry->location == MEMORY){
		//printf("page_eviction memory\n");
		pagedir_clear_page(f->owner->pagedir,f->page_no);
		size_t idx=move_to_swap(f->frame_no);
		if(idx==BITMAP_ERROR)
			goto done;
		entry->swap_idx=idx;
		entry->location=SWAP;
		success= true;
		goto done;
	}
	done:
	lock_release(&f->owner->apt_lock);
	return success;
}

bool try_evicting(){
	//printf("trying evicting\n");
	struct list_elem *e;
	struct frame *f;

	for(e=list_begin(&frame_table.allframes);e!=list_end (&frame_table.allframes);
		  	           e = list_next (e))
	 {
		f=list_entry(e,struct frame,elem);
		//printf("trying evicting loop %x\n",f->frame_no);
		 if(f->page_no){
			 if(!pagedir_is_accessed(f->owner->pagedir,f->page_no)){
				 if(f->is_swappable==false)
					 continue;
				 if(!page_eviction_updates(f))
					 return false;
				 list_remove(e);
				 //pagedir_clear_page(f->owner->pagedir,f->page_no);
				 //printf("hha\n");
				 palloc_free_page(f->frame_no);
				 //printf("hha2\n");
				 free(f);
				 return true;
			 }
			 else{
				 pagedir_set_accessed(f->owner->pagedir,f->page_no,false);
			 }
		 }
	 }
	//printf("trying evicting\n");
	for(e=list_begin(&frame_table.allframes);e!=list_end (&frame_table.allframes);
			  	           e = list_next (e)){
		f=list_entry(e,struct frame,elem);
		if(f->page_no){
			if(!pagedir_is_accessed(f->owner->pagedir,f->page_no)){
				if(f->is_swappable==false)
					continue;
				 if(!page_eviction_updates(f))
					return false;
				 list_remove(e);
				 //pagedir_clear_page(f->owner->pagedir,f->page_no);
				 //printf("hha\n");
				 palloc_free_page(f->frame_no);
				 //printf("hha2\n");
				 free(f);
				 return true;
			}
		}
	}
	return false;
}

void frame_thread_delete(struct thread *t){
	struct list_elem *e;
	struct frame *f;
	lock_acquire(&frame_table.frame_mutex);
	e=list_begin(&frame_table.allframes);
	while(e!=list_end(&frame_table.allframes)){
		f=list_entry(e,struct frame,elem);
		if(f->owner==t)
		{
			if(pagedir_get_page(t->pagedir,f->page_no))
				pagedir_clear_page(t->pagedir,f->page_no);
			palloc_free_page(f->frame_no);
			e=list_remove(e);
			free(f);
		}
		else{
			e = list_next(e);
		}
	}
	lock_release(&frame_table.frame_mutex);
}
