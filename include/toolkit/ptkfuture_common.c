/*
 * Redistribution and use in source and binary forms, with or without modification, are permitted 
 * provided that the following conditions are met:
 * 1.   Redistributions of source code must retain the above copyright notice, this list of 
 * conditions and the following disclaimer.
 * 2.   Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

