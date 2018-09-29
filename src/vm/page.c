#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "vm/frame.h"

bool
new_apt_entry (struct file* file, off_t ofs, uint8_t *upage, uint8_t *kpage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum page_location loc){
	struct addon_page_table *new= malloc(sizeof(struct addon_page_table));
	if(new){
		//printf("%d\n", upage);
		new->file=file;
		new->ofs=ofs;
		new->upage=upage;

		new->read_bytes=read_bytes;
		new->zero_bytes=zero_bytes;
		new->writable=writable;
		new->location=loc;
		new->swap_idx=-1;
		return add_apt_entry(new);
	}
	return false;
}
bool add_apt_entry(struct addon_page_table *new_entry){
	struct hash_elem *e=hash_insert(&thread_current()->apt, &new_entry->h_elem);
	if( e == NULL)
		return true;
	return false;
}

void* lookup_apt(void *addr){
	lock_acquire(&thread_current()->apt_lock);
	struct addon_page_table *new= malloc(sizeof(struct addon_page_table));
	new->upage=pg_round_down(addr);
	struct hash_elem *elem=hash_find(&thread_current()->apt, &new->h_elem);
	free(new);
	if(elem){
		lock_release(&thread_current()->apt_lock);
		return hash_entry(elem,struct addon_page_table,h_elem);
	}
	//printf("NULL\n");
	lock_release(&thread_current()->apt_lock);
	return NULL;
}

void* lookup_other_apt(struct addon_page_table *apt, void *addr){
	struct addon_page_table *new= malloc(sizeof(struct addon_page_table));
	new->upage=pg_round_down(addr);
	struct hash_elem *elem=hash_find(apt, &new->h_elem);
	free(new);
	if(elem){
			return hash_entry(elem,struct addon_page_table,h_elem);
	}
	//printf("NULL\n");
	return NULL;
}

void apt_delete(void *vaddr){
	struct addon_page_table *entry=lookup_apt(vaddr);
	if(!entry)
		return;
	hash_delete(&thread_current()->apt,&entry->h_elem);
	free(entry);
}

void empty_apt(){
	hash_destroy(&thread_current()->apt, &apt_destructor);
}

void apt_destructor(struct hash_elem *h_elem, void *aux){
	struct addon_page_table *entry=hash_entry(h_elem, struct addon_page_table, h_elem);
	if(entry->location==SWAP)
		free_swap(entry->swap_idx);
	free(entry);
}

unsigned apt_hash_hash_function(const struct hash_elem *elem,
			void *aux)
{
	const struct addon_page_table *p = hash_entry(elem, struct addon_page_table,h_elem);
	return hash_bytes (&p->upage, sizeof(p->upage));
}

bool apt_hash_less_func(const struct hash_elem *a, const struct
		hash_elem *b, void *aux){
	const struct addon_page_table *a_=hash_entry(a, struct addon_page_table, h_elem);
	const struct addon_page_table *b_=hash_entry(b, struct addon_page_table, h_elem);
	return a_->upage < b_->upage;
}

bool grow_stack(void *addr){
	new_apt_entry(NULL,0,pg_round_down(addr),NULL,0,0,true,MEMORY);
	return load_frame(NULL,pg_round_down(addr),0,0,0,true,MEMORY,true);
}
