/*
 * COPYRIGHT_NOTICE_1
 */

/* $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/toolkit/futures/src/ptkfuture_common.c,v 1.3 2011/03/09 19:09:53 taanders Exp $ */

/*
 * This file contains only static (private) functions that need to have
 * both managed and unmanaged implementations, depending on the caller's
 * context.  For managed code, these functions should be defined as managed
 * so that no M2U transition is needed.  For the unmanaged global RSE
 * function, these functions should be unmanaged because it may not be
 * safe to invoke them as managed; e.g., the thread doing GC could be a
 * non-Pillar thread.
 */

static int DECLARE(getLockStatus, SUFFIX, CALLING_CONVENTION) (struct PtkFuture_Queue *queue)
{
    int result = pthread_mutex_trylock(&queue->lock);
    if (result == 0) {
        pthread_mutex_unlock(&queue->lock);
    }
    return result;
}

static void DECLARE(resizeNodeArray, SUFFIX, CALLING_CONVENTION) (struct PtkFuture_Queue *queue, unsigned size)
{
    if (size > queue->nodePoolSize) {
        queue->nodePool = ptkRealloc(queue->nodePool, size * sizeof(*queue->nodePool));
        queue->nodePoolSize = size;
    }
}

static void DECLARE(releaseListNode, SUFFIX, CALLING_CONVENTION) (struct PtkFuture_ListNode *node, struct PtkFuture_Queue *queue)
{
    if (queue->nodePoolNext == queue->nodePoolSize) {
        CONCAT(resizeNodeArray, SUFFIX) (queue, queue->nodePoolSize + 100);
    }
    queue->nodePool[queue->nodePoolNext++] = node;
}

static void DECLARE(removeFutureWithNodeQueue, SUFFIX, CALLING_CONVENTION) (ARG_TYPE arg, int futureOffset, struct PtkFuture_ListNode *qnode, struct PtkFuture_Queue *queue)
{
    /* XXX- watch out for a race condition with another thread removing the
       future from the queue or moving it to another queue.
    */
    assert(!arg || GetFuture(arg, futureOffset)->status == PtkFutureStatusStarted);
    qnode->next->prev = qnode->prev;
    qnode->prev->next = qnode->next;
    qnode->list = NULL;
    if (arg) {
        GetFuture(arg, futureOffset)->qnode = NULL;
    }
    CONCAT(releaseListNode, SUFFIX) (qnode, queue);
}

