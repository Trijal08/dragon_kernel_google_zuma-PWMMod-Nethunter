#include "lwis_ioctl_past.h"
#include "lwis_commands.h"

static void populate_transaction_info_from_cmd_v5(void *_cmd,
						  struct lwis_transaction *k_transaction)
{
	/* TODO: b/342032857 - Will change once we have the info_v6 structure. */
	struct lwis_cmd_transaction_info *cmd = _cmd;
	k_transaction->info = cmd->info;
}

static void populate_cmd_v5_info_from_transaction(void *_cmd,
						  struct lwis_transaction *k_transaction, int error)
{
	/* TODO: b/342032857 - Will change once we have the info_v6 structure. */
	struct lwis_cmd_transaction_info *cmd = _cmd;
	cmd->info = k_transaction->info;
	if (error != 0)
		cmd->info.id = LWIS_ID_INVALID;
}

struct cmd_transaction_submit_ops transaction_cmd_v5_ops = {
	.cmd_size = sizeof(struct lwis_cmd_transaction_info),
	.populate_transaction_info_from_cmd = populate_transaction_info_from_cmd_v5,
	.populate_cmd_info_from_transaction = populate_cmd_v5_info_from_transaction,
};

static void populate_transaction_info_from_cmd_v4(void *_cmd,
						  struct lwis_transaction *k_transaction)
{
	struct lwis_cmd_transaction_info_v4 *k_info_v4 = _cmd;
	int i;

	k_transaction->info.trigger_event_id = k_info_v4->info.trigger_event_id;
	k_transaction->info.trigger_event_counter = k_info_v4->info.trigger_event_counter;
	k_transaction->info.num_io_entries = k_info_v4->info.num_io_entries;
	k_transaction->info.io_entries = k_info_v4->info.io_entries;
	k_transaction->info.run_in_event_context = k_info_v4->info.run_in_event_context;
	k_transaction->info.reserved = k_info_v4->info.reserved;
	k_transaction->info.emit_success_event_id = k_info_v4->info.emit_success_event_id;
	k_transaction->info.emit_error_event_id = k_info_v4->info.emit_error_event_id;
	k_transaction->info.is_level_triggered = k_info_v4->info.is_level_triggered;
	k_transaction->info.id = k_info_v4->info.id;
	k_transaction->info.current_trigger_event_counter =
		k_info_v4->info.current_trigger_event_counter;
	k_transaction->info.submission_timestamp_ns = k_info_v4->info.submission_timestamp_ns;
	k_transaction->info.trigger_condition.num_nodes =
		k_info_v4->info.trigger_condition.num_nodes;
	k_transaction->info.trigger_condition.operator_type =
		k_info_v4->info.trigger_condition.operator_type;

	for (i = 0; i < k_info_v4->info.trigger_condition.num_nodes; i++) {
		k_transaction->info.trigger_condition.trigger_nodes[i].type =
			k_info_v4->info.trigger_condition.trigger_nodes[i].type;
		if (k_info_v4->info.trigger_condition.trigger_nodes[i].type == LWIS_TRIGGER_EVENT) {
			k_transaction->info.trigger_condition.trigger_nodes[i].event.id =
				k_info_v4->info.trigger_condition.trigger_nodes[i].event.id;
			k_transaction->info.trigger_condition.trigger_nodes[i]
				.event.precondition_fence_fd =
				k_info_v4->info.trigger_condition.trigger_nodes[i]
					.event.precondition_fence_fd;
			k_transaction->info.trigger_condition.trigger_nodes[i].event.counter =
				k_info_v4->info.trigger_condition.trigger_nodes[i].event.counter;
		} else {
			/* LWIS_TRIGGER_FENCE or LWIS_TRIGGER_FENCE_PLACEHOLDER */
			k_transaction->info.trigger_condition.trigger_nodes[i].fence_fd =
				k_info_v4->info.trigger_condition.trigger_nodes[i].fence_fd;
		}
	}
	k_transaction->info.create_completion_fence_fd = k_info_v4->info.completion_fence_fd;

	k_transaction->info.is_high_priority_transaction =
		k_info_v4->info.is_high_priority_transaction;
	memcpy(k_transaction->info.transaction_name, k_info_v4->info.transaction_name,
	       sizeof(k_transaction->info.transaction_name));

	k_transaction->info.num_nested_transactions = k_info_v4->info.num_nested_transactions;
	memcpy(k_transaction->info.nested_transaction_ids, k_info_v4->info.nested_transaction_ids,
	       sizeof(k_transaction->info.nested_transaction_ids));

	k_transaction->info.num_completion_fences = 0;
}

static void populate_cmd_v4_info_from_transaction(void *_cmd,
						  struct lwis_transaction *k_transaction, int error)
{
	struct lwis_cmd_transaction_info_v4 *k_info_v4 = _cmd;
	struct lwis_transaction_info_v4 *info_v4 = &k_info_v4->info;
	struct lwis_transaction_info *info = &k_transaction->info;
	int i;

	info_v4->trigger_event_id = info->trigger_event_id;
	info_v4->trigger_event_counter = info->trigger_event_counter;
	info_v4->num_io_entries = info->num_io_entries;
	info_v4->io_entries = info->io_entries;
	info_v4->run_in_event_context = info->run_in_event_context;
	info_v4->reserved = info->reserved;
	info_v4->emit_success_event_id = info->emit_success_event_id;
	info_v4->emit_error_event_id = info->emit_error_event_id;
	info_v4->is_level_triggered = info->is_level_triggered;
	info_v4->id = info->id;
	info_v4->current_trigger_event_counter = info->current_trigger_event_counter;
	info_v4->submission_timestamp_ns = info->submission_timestamp_ns;
	info_v4->trigger_condition.num_nodes = info->trigger_condition.num_nodes;
	info_v4->trigger_condition.operator_type = info->trigger_condition.operator_type;

	for (i = 0; i < info->trigger_condition.num_nodes; i++) {
		info_v4->trigger_condition.trigger_nodes[i].type =
			info->trigger_condition.trigger_nodes[i].type;
		if (info->trigger_condition.trigger_nodes[i].type == LWIS_TRIGGER_EVENT) {
			info_v4->trigger_condition.trigger_nodes[i].event.id =
				info->trigger_condition.trigger_nodes[i].event.id;
			info_v4->trigger_condition.trigger_nodes[i].event.precondition_fence_fd =
				info->trigger_condition.trigger_nodes[i].event.precondition_fence_fd;
			info_v4->trigger_condition.trigger_nodes[i].event.counter =
				info->trigger_condition.trigger_nodes[i].event.counter;
		} else {
			/* LWIS_TRIGGER_FENCE or LWIS_TRIGGER_FENCE_PLACEHOLDER */
			info_v4->trigger_condition.trigger_nodes[i].fence_fd =
				info->trigger_condition.trigger_nodes[i].fence_fd;
		}
	}

	info_v4->completion_fence_fd = info->create_completion_fence_fd;
	info_v4->is_high_priority_transaction = info->is_high_priority_transaction;
	memcpy(info_v4->transaction_name, info->transaction_name,
	       sizeof(info_v4->transaction_name));

	info_v4->num_nested_transactions = info->num_nested_transactions;
	memcpy(info_v4->nested_transaction_ids, info->nested_transaction_ids,
	       sizeof(info_v4->nested_transaction_ids));

	if (error != 0)
		info_v4->id = LWIS_ID_INVALID;
}

struct cmd_transaction_submit_ops transaction_cmd_v4_ops = {
	.cmd_size = sizeof(struct lwis_cmd_transaction_info_v4),
	.populate_transaction_info_from_cmd = populate_transaction_info_from_cmd_v4,
	.populate_cmd_info_from_transaction = populate_cmd_v4_info_from_transaction,
};
