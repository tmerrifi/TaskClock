
#ifndef LISTARRAY_H

#define LISTARRAY_H

//implementation of a simple array-based, doubly linked list. Has constant time
//inserts, removes and lookups. NOT THREAD SAFE...we assume locks are held when
//calling these functions


#define LISTARRAY_MAX_SIZE 4096

#define LISTARRAY_ENTRY_NULL -1

#define LISTARRAY_ENTRY_DEAD (LISTARRAY_MAX_SIZE+1)

#define LISTARRAY_ENTRIES_SIZE (LISTARRAY_MAX_SIZE * sizeof(struct listarray_entry))

#define listarray_getentry(la, index) (la->entries[index])

typedef int16_t listarray_entry_ptr_t;

struct listarray_entry{
    listarray_entry_ptr_t next, prev;
};

struct listarray{
    struct listarray_entry entries[LISTARRAY_MAX_SIZE];
    listarray_entry_ptr_t head, tail;
    int count;
    int max_index;
};

#define listarray_foreach(la, int_i)                 \
    for (int_i=la->head;int_i!=LISTARRAY_ENTRY_NULL;int_i=listarray_getentry(la, int_i).next) \
    
#define listarray_foreach_allelements(la, int_i) \
    for (int_i=0;int_i<la->max_index;int_i++) \

#define __print_debugging(la, int_i)                                   \
    printk(KERN_EMERG "index: %d, next %d, prev %d, head %d, tail %d, count %d\n", \
           int_i, listarray_getentry(la, int_i).next, listarray_getentry(la, int_i).prev, \
           la->head, la->tail, la->count);

static inline void listarray_init(struct listarray * la){
    int i;
    la->head=la->tail=LISTARRAY_ENTRY_NULL;
    la->count=0;
    la->max_index=0;
    //initialize all entries to being not in the list
    for (i=0;i<LISTARRAY_MAX_SIZE;++i){
        la->entries[i].next=la->entries[i].prev=LISTARRAY_ENTRY_DEAD;
    }
}

static inline int listarray_count(struct listarray * la){
    return la->count;
}

static inline void listarray_insert(struct listarray * la, int index){
    //printk(KERN_EMERG "inserting %d\n", index);
    //__print_debugging(la, index);
    //if it doesn't pass this, then something is wrong (its already in the list?)
    BUG_ON(!(listarray_getentry(la, index).next==LISTARRAY_ENTRY_DEAD && 
             listarray_getentry(la, index).prev==LISTARRAY_ENTRY_DEAD));
    //is the list empty?
    if (la->tail==LISTARRAY_ENTRY_NULL){
        la->head=la->tail=index;
        listarray_getentry(la, index).prev=LISTARRAY_ENTRY_NULL;
    }
    else{
        listarray_getentry(la, la->tail).next=index;
        listarray_getentry(la, index).prev=la->tail;
        la->tail=index;
    }
    listarray_getentry(la, index).next=LISTARRAY_ENTRY_NULL;
    la->count++;
    if (index>la->max_index){
        la->max_index=index;
    }

    //printk(KERN_EMERG "done inserting %d\n", index);
    //__print_debugging(la, index);

}

static inline void listarray_remove(struct listarray * la, int index){
    int tmp;
    //printk(KERN_EMERG "removing %d\n", index);
    //__print_debugging(la, index);
    //printk(KERN_EMERG "*****list*****\n");
    //listarray_foreach(la, tmp){
    //printk(KERN_EMERG "-----%d\n", tmp);
    //}
    //printk(KERN_EMERG "******end list******\n\n");

    BUG_ON(la->count <= 0);
    if (listarray_getentry(la, index).prev==LISTARRAY_ENTRY_NULL &&
        listarray_getentry(la, index).next==LISTARRAY_ENTRY_NULL){
        //only item in the list
        la->head=la->tail=LISTARRAY_ENTRY_NULL;
    }
    else if (listarray_getentry(la, index).prev==LISTARRAY_ENTRY_NULL){
        //head of list
        la->head=listarray_getentry(la,index).next;
        listarray_getentry(la,la->head).prev=LISTARRAY_ENTRY_NULL;
    }
    else if (listarray_getentry(la, index).next==LISTARRAY_ENTRY_NULL){
        //tail of list
        la->tail=listarray_getentry(la,index).prev;
        //set our prev's next to point to NULL
        listarray_getentry(la,la->tail).next=LISTARRAY_ENTRY_NULL;
    }
    else{
        //in the middle somewhere, set our prev's next == to our next
        listarray_getentry(la, listarray_getentry(la,index).prev).next =
            listarray_getentry(la,index).next;
        //set our next's prev to our prev
        listarray_getentry(la, listarray_getentry(la,index).next).prev =
            listarray_getentry(la,index).prev;
        
    }
    listarray_getentry(la, index).prev=listarray_getentry(la,index).next=LISTARRAY_ENTRY_DEAD;
    la->count--;
    
    
    //printk(KERN_EMERG "done removing %d\n", index);
    //__print_debugging(la, index);

}

//is this item in the list?
static inline int listarray_in_list(struct listarray * la, int index){
    return (listarray_getentry(la, index).next!=LISTARRAY_ENTRY_DEAD &&
            listarray_getentry(la, index).prev!=LISTARRAY_ENTRY_DEAD);
}

#endif
