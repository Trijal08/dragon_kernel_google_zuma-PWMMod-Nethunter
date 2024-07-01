/*
 * Google LWIS Fence
 *
 * Copyright (c) 2022 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/poll.h>

#include "lwis_device_top.h"
#include "lwis_commands.h"
#include "lwis_fence.h"
#include "lwis_transaction.h"

#define HASH_CLIENT(x) hash_ptr(x, LWIS_CLIENTS_HASH_BITS)

bool lwis_fence_debug;
module_param(lwis_fence_debug, bool, 0644);

static int lwis_fence_release(struct inode *node, struct file *fp);
static ssize_t lwis_fence_get_status(struct file *fp, char __user *user_buffer, size_t len,
				     loff_t *offset);
static ssize_t lwis_fence_write_status(struct file *fp, const char __user *user_buffer, size_t len,
				       loff_t *offset);
static unsigned int lwis_fence_poll(struct file *fp, poll_table *wait);

static const struct file_operations fence_file_ops = {
	.owner = THIS_MODULE,
	.release = lwis_fence_release,
	.read = lwis_fence_get_status,
	.write = lwis_fence_write_status,
	.poll = lwis_fence_poll,
};

static int get_fence_status(struct lwis_fence *lwis_fence)
{
	int status;
	unsigned long flags;
	spin_lock_irqsave(&lwis_fence->lock, flags);
	status = lwis_fence->status;
	spin_unlock_irqrestore(&lwis_fence->lock, flags);
	return status;
}

/*
 *  lwis_fence_release: Closing an instance of a LWIS fence
 */
static int lwis_fence_release(struct inode *node, struct file *fp)
{
	struct lwis_fence *lwis_fence = fp->private_data;
	struct lwis_fence_trigger_transaction_list *tx_list;
	struct lwis_pending_transaction_id *transaction_id;
	/* Temporary vars for traversal */
	struct hlist_node *n;
	struct list_head *it_tran, *it_tran_tmp;
	int i;

	lwis_debug_dev_info(lwis_fence->lwis_top_dev->dev, "Releasing lwis_fence fd-%d",
			    lwis_fence->fd);

	if (lwis_fence->status == LWIS_FENCE_STATUS_NOT_SIGNALED) {
		dev_err(lwis_fence->lwis_top_dev->dev,
			"lwis_fence fd-%d release without being signaled", lwis_fence->fd);
	}

	if (!hash_empty(lwis_fence->transaction_list)) {
		hash_for_each_safe (lwis_fence->transaction_list, i, n, tx_list, node) {
			if (!list_empty(&tx_list->list)) {
				list_for_each_safe (it_tran, it_tran_tmp, &tx_list->list) {
					transaction_id =
						list_entry(it_tran,
							   struct lwis_pending_transaction_id,
							   list_node);
					list_del(&transaction_id->list_node);
					kfree(transaction_id);
				}
			}
			hash_del(&tx_list->node);
			kfree(tx_list);
		}
	}

	kfree(lwis_fence);
	return 0;
}

/*
 *  lwis_fence_get_status: Read the LWIS fence's status
 */
static ssize_t lwis_fence_get_status(struct file *fp, char __user *user_buffer, size_t len,
				     loff_t *offset)
{
	int status = 0;
	struct lwis_fence *lwis_fence = fp->private_data;
	int max_len, read_len;

	if (!lwis_fence) {
		return -EFAULT;
	}

	max_len = sizeof(status) - *offset;
	if (len > max_len) {
		len = max_len;
	}

	status = get_fence_status(lwis_fence);
	read_len = len - copy_to_user((void __user *)user_buffer, (void *)&status + *offset, len);

	return read_len;
}

/*
 *  lwis_fence_write_status: Signal fence with the error code from user
 */
static ssize_t lwis_fence_write_status(struct file *fp, const char __user *user_buffer, size_t len,
				       loff_t *offset)
{
	int ret = 0;
	int status = 0;
	struct lwis_fence *lwis_fence = fp->private_data;

	if (!lwis_fence) {
		return -EFAULT;
	}

	if (len != sizeof(lwis_fence->status)) {
		dev_err(lwis_fence->lwis_top_dev->dev,
			"Signal lwis_fence fd-%d with incorrect buffer length\n", lwis_fence->fd);
		return -EINVAL;
	}

	/* Set lwis_fence's status if not signaled */
	len = len - copy_from_user(&status, (void __user *)user_buffer, len);
	ret = lwis_fence_signal(lwis_fence, status);
	if (ret) {
		return ret;
	}

	return len;
}

int lwis_fence_signal(struct lwis_fence *lwis_fence, int status)
{
	unsigned long flags;
	struct lwis_fence_trigger_transaction_list *tx_list;
	/* Temporary vars for hash table traversal */
	struct hlist_node *n;
	int i;

	if (!lwis_fence) {
		return -EFAULT;
	}

	if (status == LWIS_FENCE_STATUS_NOT_SIGNALED) {
		/* Do not allow signaling with not-signaled status code. */
		dev_err(lwis_fence->lwis_top_dev->dev,
			"Cannot signal lwis_fence with invalid status : %d\n", status);
		return -EINVAL;
	}

	spin_lock_irqsave(&lwis_fence->lock, flags);

	if (lwis_fence->status != LWIS_FENCE_STATUS_NOT_SIGNALED) {
		/* Return error if fence is already signaled */
		dev_err(lwis_fence->lwis_top_dev->dev,
			"Cannot signal a lwis_fence fd-%d already signaled, status is %d\n",
			lwis_fence->fd, lwis_fence->status);
		spin_unlock_irqrestore(&lwis_fence->lock, flags);
		return -EINVAL;
	}
	lwis_fence->status = status;
	lwis_debug_dev_info(lwis_fence->lwis_top_dev->dev,
			    "lwis_fence fd %d signaled with status: %d", lwis_fence->fd,
			    lwis_fence->status);
	spin_unlock_irqrestore(&lwis_fence->lock, flags);

	wake_up_interruptible(&lwis_fence->status_wait_queue);

	hash_for_each_safe (lwis_fence->transaction_list, i, n, tx_list, node) {
		hash_del(&tx_list->node);
		lwis_transaction_fence_trigger(tx_list->owner, lwis_fence, &tx_list->list);
		if (!list_empty(&tx_list->list)) {
			dev_err(lwis_fence->lwis_top_dev->dev,
				"Fail to trigger all transactions\n");
		}
		kfree(tx_list);
	}

	return 0;
}

/*
 *  lwis_fence_poll: Poll status function of LWIS fence
 */
static unsigned int lwis_fence_poll(struct file *fp, poll_table *wait)
{
	unsigned long flags;
	int status = 0;
	struct lwis_fence *lwis_fence = fp->private_data;
	if (!lwis_fence) {
		return POLLERR;
	}

	poll_wait(fp, &lwis_fence->status_wait_queue, wait);

	spin_lock_irqsave(&lwis_fence->lock, flags);
	status = lwis_fence->status;
	spin_unlock_irqrestore(&lwis_fence->lock, flags);

	/* Check if the fence is already signaled */
	if (status != LWIS_FENCE_STATUS_NOT_SIGNALED) {
		return POLLIN;
	}

	return 0;
}

int lwis_fence_create(struct lwis_device *lwis_dev)
{
	int fd_or_err;
	struct lwis_fence *new_fence;

	/* Allocate a new instance of lwis_fence struct */
	new_fence = kmalloc(sizeof(struct lwis_fence), GFP_ATOMIC);
	if (!new_fence) {
		return -ENOMEM;
	}

	/* Open a new fd for the new fence */
	fd_or_err =
		anon_inode_getfd("lwis_fence_file", &fence_file_ops, new_fence, O_RDWR | O_CLOEXEC);
	if (fd_or_err < 0) {
		kfree(new_fence);
		dev_err(lwis_dev->dev, "Failed to create a new file instance for lwis_fence\n");
		return fd_or_err;
	}

	new_fence->fd = fd_or_err;
	new_fence->lwis_top_dev = lwis_dev->top_dev;
	new_fence->status = LWIS_FENCE_STATUS_NOT_SIGNALED;
	spin_lock_init(&new_fence->lock);
	init_waitqueue_head(&new_fence->status_wait_queue);
	lwis_debug_dev_info(lwis_dev->dev, "lwis_fence created new LWIS fence fd: %d",
			    new_fence->fd);
	return fd_or_err;
}

struct file *lwis_fence_get(struct lwis_client *client, int fd)
{
	struct file *fence_fp;
	struct lwis_fence *fence;

	fence_fp = fget(fd);
	if (fence_fp == NULL) {
		dev_err(client->lwis_dev->dev, "Fence fd %d results in NULL file pointer", fd);
		return NULL;
	}

	if (fence_fp->f_op != &fence_file_ops) {
		fput(fence_fp);
		dev_err(client->lwis_dev->dev, "Underlying structure for fd %d is not a lwis_fence",
			fd);
		return NULL;
	}

	fence = fence_fp->private_data;
	if (fence->fd != fd) {
		fput(fence_fp);
		dev_err(client->lwis_dev->dev,
			"Invalid lwis_fence with fd %d. Contains stale data \n", fd);
		return NULL;
	}

	return fence_fp;
}

static struct lwis_fence_trigger_transaction_list *transaction_list_find(struct lwis_fence *fence,
									 struct lwis_client *owner)
{
	int hash_key = HASH_CLIENT(owner);
	struct lwis_fence_trigger_transaction_list *tx_list;
	hash_for_each_possible (fence->transaction_list, tx_list, node, hash_key) {
		if (tx_list->owner == owner) {
			return tx_list;
		}
	}
	return NULL;
}

static struct lwis_fence_trigger_transaction_list *
transaction_list_create(struct lwis_fence *fence, struct lwis_client *owner)
{
	struct lwis_fence_trigger_transaction_list *tx_list =
		kmalloc(sizeof(struct lwis_fence_trigger_transaction_list), GFP_ATOMIC);
	if (!tx_list) {
		return NULL;
	}
	tx_list->owner = owner;
	INIT_LIST_HEAD(&tx_list->list);
	hash_add(fence->transaction_list, &tx_list->node, HASH_CLIENT(owner));
	return tx_list;
}

static struct lwis_fence_trigger_transaction_list *
transaction_list_find_or_create(struct lwis_fence *fence, struct lwis_client *owner)
{
	struct lwis_fence_trigger_transaction_list *list = transaction_list_find(fence, owner);
	return (list == NULL) ? transaction_list_create(fence, owner) : list;
}

static int trigger_event_add_transaction(struct lwis_client *client,
					 struct lwis_transaction *transaction,
					 struct lwis_transaction_trigger_event *event)
{
	struct file *precondition_fence_fp = NULL;
	struct lwis_device *lwis_dev = client->lwis_dev;
	struct lwis_device_event_state *event_state;
	struct lwis_fence *precondition_fence;
	struct lwis_transaction_info *info = &transaction->info;
	int32_t operator_type = info->trigger_condition.operator_type;
	size_t all_signaled = info->trigger_condition.num_nodes;
	int precondition_fence_status = LWIS_FENCE_STATUS_NOT_SIGNALED;

	/* Check if the event has been encountered and if the event counters match. */
	event_state = lwis_device_event_state_find(lwis_dev, event->id);
	if (event_state != NULL && transaction->info.is_level_triggered &&
	    EXPLICIT_EVENT_COUNTER(event->counter) &&
	    event->counter == event_state->event_counter) {
		/* The event is currently level triggered, first we need to check if there is a
		 * precondition fence associated with the event. */
		if (event->precondition_fence_fd >= 0) {
			precondition_fence_fp =
				lwis_fence_get(client, event->precondition_fence_fd);
			if (precondition_fence_fp == NULL) {
				return -EBADF;
			}
			precondition_fence = precondition_fence_fp->private_data;
			precondition_fence_status = get_fence_status(precondition_fence);
		}
		/* If the event is not triggered by a precondition fence, or the precondition fence
		 * is already signaled, queue the transaction immediately. */
		if (event->precondition_fence_fd < 0 || precondition_fence_status == 0) {
			/* The event trigger has been satisfied, so we can increase the signal
			 * count. */
			transaction->signaled_count++;
			transaction->queue_immediately =
				operator_type != LWIS_TRIGGER_NODE_OPERATOR_AND ||
				transaction->signaled_count == all_signaled;
			return 0;
		}
	}

	return lwis_trigger_event_add_weak_transaction(client, info->id, event->id,
						       event->precondition_fence_fd);
}

static int trigger_fence_add_transaction(int fence_fd, struct lwis_client *client,
					 struct lwis_transaction *transaction)
{
	unsigned long flags;
	struct file *fp = NULL;
	struct lwis_fence *lwis_fence;
	struct lwis_pending_transaction_id *pending_transaction_id;
	struct lwis_fence_trigger_transaction_list *tx_list;
	int ret = 0;

	if (transaction->num_trigger_fences >= LWIS_TRIGGER_NODES_MAX_NUM) {
		dev_err(client->lwis_dev->dev,
			"Invalid num_trigger_fences value in transaction %d\n", fence_fd);
		return -EINVAL;
	}

	pending_transaction_id = kmalloc(sizeof(struct lwis_pending_transaction_id), GFP_ATOMIC);
	if (!pending_transaction_id) {
		return -ENOMEM;
	}

	fp = lwis_fence_get(client, fence_fd);
	if (fp == NULL) {
		kfree(pending_transaction_id);
		return -EBADF;
	}
	lwis_fence = fp->private_data;

	pending_transaction_id->id = transaction->info.id;

	spin_lock_irqsave(&lwis_fence->lock, flags);
	if (lwis_fence->status == LWIS_FENCE_STATUS_NOT_SIGNALED) {
		transaction->trigger_fence_fps[transaction->num_trigger_fences++] = fp;
		tx_list = transaction_list_find_or_create(lwis_fence, client);
		list_add(&pending_transaction_id->list_node, &tx_list->list);
		lwis_debug_dev_info(
			client->lwis_dev->dev,
			"lwis_fence transaction id %llu added to its trigger fence fd %d ",
			transaction->info.id, lwis_fence->fd);
	} else {
		kfree(pending_transaction_id);
		lwis_debug_dev_info(
			client->lwis_dev->dev,
			"lwis_fence fd-%d not added to transaction id %llu, fence already signaled with error code %d \n",
			fence_fd, transaction->info.id, lwis_fence->status);
		if (!transaction->info.is_level_triggered) {
			/* If level triggering is disabled, return an error. */
			fput(fp);
			ret = -EINVAL;
		} else {
			transaction->trigger_fence_fps[transaction->num_trigger_fences++] = fp;
			/* If the transaction's trigger_condition evaluates to true, queue the
			 * transaction to be executed immediately.
			 */
			if (lwis_fence_triggered_condition_ready(transaction, lwis_fence->status)) {
				if (lwis_fence->status != 0) {
					transaction->resp->error_code = -ECANCELED;
				}
				transaction->queue_immediately = true;
			}
		}
	}
	spin_unlock_irqrestore(&lwis_fence->lock, flags);

	return ret;
}

bool lwis_triggered_by_condition(struct lwis_transaction *transaction)
{
	return (transaction->info.trigger_condition.num_nodes > 0);
}

bool lwis_event_triggered_condition_ready(struct lwis_transaction *transaction,
					  struct lwis_transaction *weak_transaction,
					  int64_t event_id, int64_t event_counter)
{
	int32_t operator_type;
	size_t all_signaled;
	struct lwis_transaction_info *info = &transaction->info;
	int i;
	struct lwis_fence *lwis_fence;
	bool is_node_signaled = false;

	operator_type = info->trigger_condition.operator_type;
	all_signaled = info->trigger_condition.num_nodes;

	/*
	 * Three scenarios to consider a node signaled:
	 * 1) Event ID and event counter match,
	 * 2) Event ID match, event counter not specified but precondition fence signaled, or,
	 * 3) Event ID match, event counter and precondition fence not specified.
	 */
	for (i = 0; i < info->trigger_condition.num_nodes; i++) {
		is_node_signaled = false;
		if (info->trigger_condition.trigger_nodes[i].type != LWIS_TRIGGER_EVENT ||
		    info->trigger_condition.trigger_nodes[i].event.id != event_id) {
			continue;
		}

		if (info->trigger_condition.trigger_nodes[i].event.counter == event_counter ||
		    (info->trigger_condition.trigger_nodes[i].event.counter ==
			     LWIS_EVENT_COUNTER_ON_NEXT_OCCURRENCE &&
		     weak_transaction->precondition_fence_fp == NULL)) {
			is_node_signaled = true;
		} else if (info->trigger_condition.trigger_nodes[i].event.counter ==
			   LWIS_EVENT_COUNTER_ON_NEXT_OCCURRENCE) {
			lwis_fence = weak_transaction->precondition_fence_fp->private_data;
			is_node_signaled =
				(lwis_fence != NULL && get_fence_status(lwis_fence) == 0);
			lwis_debug_info(
				"TransactionId %lld: event 0x%llx (%lld), precondition fence %d %s signaled",
				info->id, event_id, event_counter,
				info->trigger_condition.trigger_nodes[i].event.precondition_fence_fd,
				is_node_signaled ? "" : "NOT");
		}

		if (is_node_signaled) {
			transaction->signaled_count++;
			list_del(&weak_transaction->event_list_node);
			if (weak_transaction->precondition_fence_fp) {
				fput(weak_transaction->precondition_fence_fp);
			}
			kfree(weak_transaction);
			/* The break here assumes that this event ID only appears once in the trigger
			 * expression. Might need to revisit this. */
			break;
		}
	}

	if (i >= info->trigger_condition.num_nodes) {
		/* No event counter is matched */
		return false;
	}

	switch (operator_type) {
	case LWIS_TRIGGER_NODE_OPERATOR_AND:
		return transaction->signaled_count == all_signaled;
	case LWIS_TRIGGER_NODE_OPERATOR_OR:
	case LWIS_TRIGGER_NODE_OPERATOR_NONE:
		return true;
	}

	return false;
}

bool lwis_fence_triggered_condition_ready(struct lwis_transaction *transaction, int fence_status)
{
	int32_t operator_type;
	size_t all_signaled;

	operator_type = transaction->info.trigger_condition.operator_type;
	all_signaled = transaction->info.trigger_condition.num_nodes;

	transaction->signaled_count++;
	if ((operator_type == LWIS_TRIGGER_NODE_OPERATOR_AND ||
	     operator_type == LWIS_TRIGGER_NODE_OPERATOR_OR) &&
	    transaction->signaled_count == all_signaled) {
		return true;
	} else if (operator_type == LWIS_TRIGGER_NODE_OPERATOR_AND && fence_status != 0) {
		/*
		   This condition is ready to cancel transaction as long as there is
		   an error condition from fence with operator type "AND".
		   No matter whether all condition nodes are signaled.
		*/
		return true;
	} else if (operator_type == LWIS_TRIGGER_NODE_OPERATOR_OR && fence_status == 0) {
		return true;
	} else if (operator_type == LWIS_TRIGGER_NODE_OPERATOR_NONE) {
		return true;
	}

	return false;
}

int lwis_parse_trigger_condition(struct lwis_client *client, struct lwis_transaction *transaction)
{
	struct lwis_transaction_info *info;
	struct lwis_device *lwis_dev;
	int i, ret;

	if (!transaction || !client) {
		return -EINVAL;
	}

	info = &transaction->info;
	lwis_dev = client->lwis_dev;

	if (info->trigger_condition.num_nodes > LWIS_TRIGGER_NODES_MAX_NUM) {
		dev_err(lwis_dev->dev,
			"Trigger condition contains %lu node, more than the limit of %d\n",
			info->trigger_condition.num_nodes, LWIS_TRIGGER_NODES_MAX_NUM);
		return -EINVAL;
	}

	for (i = 0; i < info->trigger_condition.num_nodes; i++) {
		if (info->trigger_condition.trigger_nodes[i].type == LWIS_TRIGGER_EVENT) {
			ret = trigger_event_add_transaction(
				client, transaction,
				&info->trigger_condition.trigger_nodes[i].event);
		} else {
			ret = trigger_fence_add_transaction(
				info->trigger_condition.trigger_nodes[i].fence_fd, client,
				transaction);
		}
		if (ret) {
			return ret;
		}
	}

	return 0;
}

int lwis_initialize_transaction_fences(struct lwis_client *client,
				       struct lwis_transaction *transaction)
{
	struct lwis_transaction_info *info = &transaction->info;
	struct lwis_device *lwis_dev = client->lwis_dev;
	int i;
	int fd_or_err;

	if (!transaction || !client) {
		return -EINVAL;
	}

	if (info->trigger_condition.num_nodes > LWIS_TRIGGER_NODES_MAX_NUM) {
		dev_err(lwis_dev->dev,
			"Trigger condition contains %lu node, more than the limit of %d\n",
			info->trigger_condition.num_nodes, LWIS_TRIGGER_NODES_MAX_NUM);
		return -EINVAL;
	}

	/* If triggered by trigger_condition */
	if (lwis_triggered_by_condition(transaction)) {
		/* Initialize all placeholder fences in the trigger_condition */
		for (i = 0; i < info->trigger_condition.num_nodes; i++) {
			if (info->trigger_condition.trigger_nodes[i].type !=
			    LWIS_TRIGGER_FENCE_PLACEHOLDER) {
				continue;
			}

			fd_or_err = lwis_fence_create(lwis_dev);
			if (fd_or_err < 0) {
				return fd_or_err;
			}
			info->trigger_condition.trigger_nodes[i].fence_fd = fd_or_err;
		}
	}

	/* Initialize completion fence if one is requested */
	if (info->create_completion_fence_fd == LWIS_CREATE_COMPLETION_FENCE) {
		fd_or_err = lwis_fence_create(client->lwis_dev);
		if (fd_or_err < 0) {
			return fd_or_err;
		}
		info->create_completion_fence_fd = fd_or_err;
	}

	return 0;
}

/*
 *  add_completion_fence: Adds a single completion fence to the transaction
 */
static int add_completion_fence(struct lwis_client *client, struct lwis_transaction *transaction,
				int fence_fd)
{
	struct file *fp;
	struct lwis_fence *lwis_fence;
	struct lwis_fence_pending_signal *fence_pending_signal;

	fp = lwis_fence_get(client, fence_fd);
	if (fp == NULL) {
		return -EBADF;
	}

	lwis_fence = fp->private_data;
	fence_pending_signal = lwis_fence_pending_signal_create(lwis_fence, fp);
	if (fence_pending_signal == NULL) {
		return -ENOMEM;
	}
	list_add(&fence_pending_signal->node, &transaction->completion_fence_list);
	lwis_debug_dev_info(client->lwis_dev->dev,
			    "lwis_fence transaction id %llu add completion fence fd %d ",
			    transaction->info.id, lwis_fence->fd);

	return 0;
}

int lwis_add_completion_fences_to_transaction(struct lwis_client *client,
					      struct lwis_transaction *transaction)
{
	int ret = 0;
	int i;
	int fence_fd;
	struct lwis_device *lwis_dev = client->lwis_dev;
	struct lwis_transaction_info *info = &transaction->info;

	/* If a completion fence is requested but not initialized, we cannot continue. */
	if (info->create_completion_fence_fd == LWIS_CREATE_COMPLETION_FENCE) {
		dev_err(lwis_dev->dev,
			"Cannot add uninitialized completion fence to transaction\n");
		return -EPERM;
	}
	/* Otherwise, add the created completion fence to the transaction's list. */
	if (info->create_completion_fence_fd >= 0) {
		ret = add_completion_fence(client, transaction, info->create_completion_fence_fd);
		if (ret) {
			return ret;
		}
	}
	/* Add each external completion fence to the transaction's completion fence list. */
	for (i = 0; i < info->num_completion_fences; ++i) {
		fence_fd = info->completion_fence_fds[i];
		if (fence_fd < 0) {
			dev_err(lwis_dev->dev, "Invalid external completion fence fd %d\n",
				fence_fd);
			return -EINVAL;
		}
		ret = add_completion_fence(client, transaction, fence_fd);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

struct lwis_fence_pending_signal *lwis_fence_pending_signal_create(struct lwis_fence *fence,
								   struct file *fp)
{
	struct lwis_fence_pending_signal *pending_fence_signal =
		kmalloc(sizeof(struct lwis_fence_pending_signal), GFP_ATOMIC);
	if (!pending_fence_signal) {
		return NULL;
	}
	pending_fence_signal->fp = fp;
	pending_fence_signal->fence = fence;
	pending_fence_signal->pending_status = LWIS_FENCE_STATUS_NOT_SIGNALED;
	return pending_fence_signal;
}

void lwis_fences_pending_signal_emit(struct lwis_device *lwis_device,
				     struct list_head *pending_fences)
{
	int ret;
	struct lwis_fence_pending_signal *pending_fence;
	struct list_head *it_fence, *it_fence_tmp;

	list_for_each_safe (it_fence, it_fence_tmp, pending_fences) {
		pending_fence = list_entry(it_fence, struct lwis_fence_pending_signal, node);
		ret = lwis_fence_signal(pending_fence->fence, pending_fence->pending_status);
		if (ret) {
			dev_err(lwis_device->dev, "Failed signaling fence with fd %d",
				pending_fence->fence->fd);
		}
		list_del(&pending_fence->node);
		fput(pending_fence->fp);
		kfree(pending_fence);
	}
}

void lwis_pending_fences_move_all(struct lwis_device *lwis_device,
				  struct lwis_transaction *transaction,
				  struct list_head *pending_fences, int error_code)
{
	struct lwis_fence_pending_signal *pending_fence, *temp;

	/* For each fence in transaction's signal list, move to pending_fences for signaling */
	list_for_each_entry_safe (pending_fence, temp, &transaction->completion_fence_list, node) {
		pending_fence->pending_status = error_code;
		list_move_tail(&pending_fence->node, pending_fences);
	}
}
