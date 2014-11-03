/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#if !defined(MPID_RMA_OPLIST_H_INCLUDED)
#define MPID_RMA_OPLIST_H_INCLUDED

#include "mpl_utlist.h"
#include "mpid_rma_types.h"

extern struct MPIDI_RMA_Op *global_rma_op_pool, *global_rma_op_pool_tail, *global_rma_op_pool_start;
extern struct MPIDI_RMA_Target *global_rma_target_pool, *global_rma_target_pool_tail, *global_rma_target_pool_start;

/* MPIDI_CH3I_Win_op_alloc(): get a new op element from op pool and
 * initialize it. If we cannot get one, return NULL. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Win_op_alloc
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPIDI_RMA_Op_t *MPIDI_CH3I_Win_op_alloc(MPID_Win * win_ptr)
{
    MPIDI_RMA_Op_t *e;

    if (win_ptr->op_pool == NULL) {
        /* local pool is empty, try to find something in the global pool */
        if (global_rma_op_pool == NULL)
            return NULL;
        else {
            e = global_rma_op_pool;
            MPL_LL_DELETE(global_rma_op_pool, global_rma_op_pool_tail, e);
        }
    }
    else {
        e = win_ptr->op_pool;
        MPL_LL_DELETE(win_ptr->op_pool, win_ptr->op_pool_tail, e);
    }

    e->dataloop = NULL;
    e->request = NULL;

    return e;
}

/* MPIDI_CH3I_Win_op_free(): put an op element back to the op pool which
 * it belongs to. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Win_op_free
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Win_op_free(MPID_Win * win_ptr, MPIDI_RMA_Op_t * e)
{
    int mpi_errno = MPI_SUCCESS;

    /* Check if we allocated a dataloop for this op (see send/recv_rma_msg) */
    if (e->dataloop != NULL) {
        MPIU_Free(e->dataloop);
    }

    /* We enqueue elements to the right pool, so when they get freed
     * at window free time, they won't conflict with the global pool
     * or other windows */
    /* use PREPEND when return objects back to the pool
       in order to improve cache performance */
    if (e->pool_type == MPIDI_RMA_POOL_WIN)
        MPL_LL_PREPEND(win_ptr->op_pool, win_ptr->op_pool_tail, e);
    else
        MPL_LL_PREPEND(global_rma_op_pool, global_rma_op_pool_tail, e);

    return mpi_errno;
}

/* MPIDI_CH3I_Win_target_alloc(): get a target element from the target pool.
 * If we cannot get one, return NULL. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Win_target_alloc
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPIDI_RMA_Target_t *MPIDI_CH3I_Win_target_alloc(MPID_Win * win_ptr)
{
    MPIDI_RMA_Target_t *e;

    if (win_ptr->target_pool == NULL) {
        /* local pool is empty, try to find something in the global pool */
        if (global_rma_target_pool == NULL)
            return NULL;
        else {
            e = global_rma_target_pool;
            MPL_LL_DELETE(global_rma_target_pool, global_rma_target_pool_tail, e);
        }
    }
    else {
        e = win_ptr->target_pool;
        MPL_LL_DELETE(win_ptr->target_pool, win_ptr->target_pool_tail, e);
    }

    e->read_op_list = e->read_op_list_tail = NULL;
    e->write_op_list = e->write_op_list_tail = NULL;
    e->dt_op_list = e->dt_op_list_tail = NULL;
    e->pending_op_list = e->pending_op_list_tail = NULL;
    e->next_op_to_issue = NULL;

    e->target_rank = -1;
    e->lock_type = MPIDI_RMA_LOCK_TYPE_NONE;
    e->lock_mode = 0;
    e->outstanding_lock = 0;

    e->sync.sync_flag = MPIDI_RMA_NONE;
    e->sync.outstanding_acks = 0;
    e->sync.have_remote_incomplete_ops = 1; /* When I create a new target, there must be
                                               incomplete ops until a FLUSH/UNLOCK packet
                                               is sent. */
    return e;
}

/* MPIDI_CH3I_Win_target_free(): put a target element back to the target pool
 * it belongs to. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Win_target_free
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Win_target_free(MPID_Win * win_ptr, MPIDI_RMA_Target_t * e)
{
    int mpi_errno = MPI_SUCCESS;

    /* We enqueue elements to the right pool, so when they get freed
     * at window free time, they won't conflict with the global pool
     * or other windows */
    MPIU_Assert(e->read_op_list == NULL);
    MPIU_Assert(e->write_op_list == NULL);
    MPIU_Assert(e->dt_op_list == NULL);
    MPIU_Assert(e->pending_op_list == NULL);

    /* use PREPEND when return objects back to the pool
       in order to improve cache performance */
    if (e->pool_type == MPIDI_RMA_POOL_WIN)
        MPL_LL_PREPEND(win_ptr->target_pool, win_ptr->target_pool_tail, e);
    else
        MPL_LL_PREPEND(global_rma_target_pool, global_rma_target_pool_tail, e);

    return mpi_errno;
}

/* MPIDI_CH3I_Win_create_target(): given a rank, create
 * corresponding target in RMA slots. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Win_create_target
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Win_create_target(MPID_Win * win_ptr, int target_rank,
                                               MPIDI_RMA_Target_t **e)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_RMA_Slot_t *slot = NULL;
    MPIDI_RMA_Target_t *t = NULL;

    if (win_ptr->num_slots < win_ptr->comm_ptr->local_size)
        slot = &(win_ptr->slots[target_rank % win_ptr->num_slots]);
    else
        slot = &(win_ptr->slots[target_rank]);

    t = MPIDI_CH3I_Win_target_alloc(win_ptr);
    MPIU_ERR_CHKANDJUMP(t == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem");

    t->target_rank = target_rank;

    /* Enqueue target into target list. */
    MPL_LL_APPEND(slot->target_list, slot->target_list_tail, t);

    assert(t != NULL);

    (*e) = t;

 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

/* MPIDI_CH3I_Win_find_target(): given a rank, find
 * corresponding target in RMA slots. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Win_find_target
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Win_find_target(MPID_Win * win_ptr, int target_rank,
                                             MPIDI_RMA_Target_t **e)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_RMA_Slot_t *slot = NULL;
    MPIDI_RMA_Target_t *t = NULL;

    if (win_ptr->num_slots < win_ptr->comm_ptr->local_size)
        slot = &(win_ptr->slots[target_rank % win_ptr->num_slots]);
    else
        slot = &(win_ptr->slots[target_rank]);

    t = slot->target_list;
    while (t != NULL) {
        if (t->target_rank == target_rank)
            break;
    }

    (*e) = t;

 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

/* MPIDI_CH3I_Win_enqueue_op(): given an operation, enqueue it to the
 * corresponding operation lists in corresponding target element. This
 * routines is only called from operation routines. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Win_enqueue_op
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Win_enqueue_op(MPID_Win * win_ptr,
                                            MPIDI_RMA_Op_t * op)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_RMA_Target_t *target = NULL;

    mpi_errno = MPIDI_CH3I_Win_find_target(win_ptr, op->target_rank, &target);
    if (mpi_errno != MPI_SUCCESS) MPIU_ERR_POP(mpi_errno);
    if (target == NULL) {
        mpi_errno = MPIDI_CH3I_Win_create_target(win_ptr, op->target_rank, &target);
        if (mpi_errno != MPI_SUCCESS) MPIU_ERR_POP(mpi_errno);

        if (win_ptr->states.access_state == MPIDI_RMA_PER_TARGET ||
            win_ptr->states.access_state == MPIDI_RMA_LOCK_ALL_GRANTED) {
            /* If global state is MPIDI_RMA_PER_TARGET, this must not
             * be the first time to create this target (The first time
             * is in Win_lock). Here we recreated it and set the access
             * state to LOCK_GRANTED because before we free the previous
             * one, the lock should already be granted. */
            /* If global state is MPIDI_RMA_LOCK_ALL_GRANTED, all locks
             * should already be granted. So the access state for this
             * target is also set to MPIDI_RMA_LOCK_GRANTED. */
            target->access_state = MPIDI_RMA_LOCK_GRANTED;
        }
        else if (win_ptr->states.access_state == MPIDI_RMA_LOCK_ALL_CALLED) {
            /* If global state is MPIDI_RMA_LOCK_ALL_CALLED, this must
               the first time to create this target, set its access state
               to MPIDI_RMA_LOCK_CALLED. */
            target->access_state = MPIDI_RMA_LOCK_CALLED;
            target->lock_type = MPI_LOCK_SHARED;
        }
    }

    /* Enqueue operation into pending list. */
    MPL_LL_APPEND(target->pending_op_list, target->pending_op_list_tail, op);
    if (target->next_op_to_issue == NULL)
        target->next_op_to_issue = op;

 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


/* MPIDI_CH3I_Win_target_dequeue_and_free(): dequeue and free
 * the target in RMA slots. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Win_target_dequeue_and_free
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Win_target_dequeue_and_free(MPID_Win * win_ptr,
                                                         MPIDI_RMA_Target_t * e)
{
    int mpi_errno = MPI_SUCCESS;
    int target_rank = e->target_rank;
    MPIDI_RMA_Slot_t *slot;

    if (win_ptr->num_slots < win_ptr->comm_ptr->local_size)
        slot = &(win_ptr->slots[target_rank % win_ptr->num_slots]);
    else
        slot = &(win_ptr->slots[target_rank]);

    MPL_LL_DELETE(slot->target_list, slot->target_list_tail, e);

    mpi_errno = MPIDI_CH3I_Win_target_free(win_ptr, e);
    if (mpi_errno != MPI_SUCCESS) MPIU_ERR_POP(mpi_errno);

 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

/* Return nonzero if the RMA operations list is empty.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_isempty
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_RMA_Ops_isempty(MPIDI_RMA_Ops_list_t * list)
{
    return *list == NULL;
}


/* Return a pointer to the first element in the list.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_head
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPIDI_RMA_Op_t *MPIDI_CH3I_RMA_Ops_head(MPIDI_RMA_Ops_list_t * list)
{
    return *list;
}


/* Return a pointer to the last element in the list.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_tail
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPIDI_RMA_Op_t *MPIDI_CH3I_RMA_Ops_tail(MPIDI_RMA_Ops_list_t * list_tail)
{
    return (*list_tail);
}


/* Append an element to the tail of the RMA ops list
 *
 * @param IN    list      Pointer to the RMA ops list
 * @param IN    elem      Pointer to the element to be appended
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_append
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void MPIDI_CH3I_RMA_Ops_append(MPIDI_RMA_Ops_list_t * list, MPIDI_RMA_Ops_list_t * list_tail,
                                             MPIDI_RMA_Op_t * elem)
{
    MPL_LL_APPEND(*list, *list_tail, elem);
}


/* Allocate a new element on the tail of the RMA operations list.
 *
 * @param IN    list      Pointer to the RMA ops list
 * @param OUT   new_ptr   Pointer to the element that was allocated
 * @return                MPI error class
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_alloc_tail
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_RMA_Ops_alloc_tail(MPID_Win * win_ptr, MPIDI_RMA_Ops_list_t * list,
                                                MPIDI_RMA_Ops_list_t * list_tail,
                                                MPIDI_RMA_Op_t ** new_elem)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_RMA_Op_t *tmp_ptr;

    tmp_ptr = MPIDI_CH3I_Win_op_alloc(win_ptr);
    MPIU_ERR_CHKANDJUMP(tmp_ptr == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem");

    MPL_LL_APPEND(*list, *list_tail, tmp_ptr);

    *new_elem = tmp_ptr;

  fn_exit:
    return mpi_errno;
  fn_fail:
    *new_elem = NULL;
    goto fn_exit;
}


/* Unlink an element from the RMA ops list
 *
 * @param IN    list      Pointer to the RMA ops list
 * @param IN    elem      Pointer to the element to be unlinked
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_unlink
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void MPIDI_CH3I_RMA_Ops_unlink(MPIDI_RMA_Ops_list_t * list, MPIDI_RMA_Ops_list_t *list_tail,
                                             MPIDI_RMA_Op_t * elem)
{
    MPL_LL_DELETE(*list, *list_tail, elem);
}


/* Free an element in the RMA operations list.
 *
 * @param IN    list      Pointer to the RMA ops list
 * @param IN    curr_ptr  Pointer to the element to be freed.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_free_elem
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void MPIDI_CH3I_RMA_Ops_free_elem(MPID_Win * win_ptr, MPIDI_RMA_Ops_list_t * list,
                                                MPIDI_RMA_Ops_list_t * list_tail,
                                                MPIDI_RMA_Op_t * curr_ptr)
{
    MPIDI_RMA_Op_t *tmp_ptr = curr_ptr;

    MPIU_Assert(curr_ptr != NULL);

    MPL_LL_DELETE(*list, *list_tail, curr_ptr);

    MPIDI_CH3I_Win_op_free(win_ptr, tmp_ptr);
}


/* Free an element in the RMA operations list.
 *
 * @param IN    list      Pointer to the RMA ops list
 * @param INOUT curr_ptr  Pointer to the element to be freed.  Will be updated
 *                        to point to the element following the element that
 *                        was freed.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_free_and_next
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void MPIDI_CH3I_RMA_Ops_free_and_next(MPID_Win * win_ptr, MPIDI_RMA_Ops_list_t * list,
                                                    MPIDI_RMA_Ops_list_t * list_tail,
                                                    MPIDI_RMA_Op_t ** curr_ptr)
{
    MPIDI_RMA_Op_t *next_ptr = (*curr_ptr)->next;

    MPIDI_CH3I_RMA_Ops_free_elem(win_ptr, list, list_tail, *curr_ptr);
    *curr_ptr = next_ptr;
}


/* Free the entire RMA operations list.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_free
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void MPIDI_CH3I_RMA_Ops_free(MPID_Win * win_ptr, MPIDI_RMA_Ops_list_t * list,
                                           MPIDI_RMA_Ops_list_t * list_tail)
{
    MPIDI_RMA_Op_t *curr_ptr, *tmp_ptr;

    MPL_LL_FOREACH_SAFE(*list, curr_ptr, tmp_ptr) {
        MPIDI_CH3I_RMA_Ops_free_elem(win_ptr, list, list_tail, curr_ptr);
    }
}


/* Retrieve the RMA ops list pointer from the window.  This routine detects
 * whether we are in an active or passive target epoch and returns the correct
 * ops list; we use a shared list for active target and separate per-target
 * lists for passive target.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Get_ops_list
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPIDI_RMA_Ops_list_t *MPIDI_CH3I_RMA_Get_ops_list(MPID_Win * win_ptr, int target)
{
    if (win_ptr->epoch_state == MPIDI_EPOCH_FENCE ||
        win_ptr->epoch_state == MPIDI_EPOCH_START || win_ptr->epoch_state == MPIDI_EPOCH_PSCW) {
        return &win_ptr->at_rma_ops_list;
    }
    else {
        return &win_ptr->targets[target].rma_ops_list;
    }
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Get_ops_list
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPIDI_RMA_Ops_list_t *MPIDI_CH3I_RMA_Get_ops_list_tail(MPID_Win * win_ptr, int target)
{
    if (win_ptr->epoch_state == MPIDI_EPOCH_FENCE ||
        win_ptr->epoch_state == MPIDI_EPOCH_START || win_ptr->epoch_state == MPIDI_EPOCH_PSCW) {
        return &win_ptr->at_rma_ops_list_tail;
    }
    else {
        return &win_ptr->targets[target].rma_ops_list_tail;
    }
}

#endif /* MPID_RMA_OPLIST_H_INCLUDED */