/*
 * Stop services kills
 * Well, it won't stop them all, unless this is loaded on all servers.
 *
 * Copyright (C) 2013 Elizabeth Myers. All rights reserved.
 * Licensed under the WTFPLv2
 */

using namespace ircd;

static const char nokill_desc[] = "Prevents operators from killing services";

static void block_services_kill(void *data);

mapi_hfn_list_av1 no_kill_services_hfnlist[] = {
	{ "can_kill", (hookfn) block_services_kill },
	{ NULL, NULL }
};

static void
block_services_kill(void *vdata)
{
	hook_data_client_approval *data = (hook_data_client_approval *) vdata;

	if (!my(*data->client))
		return;

	if (!data->approved)
		return;

	if (is(*data->target, umode::SERVICE))
	{
		sendto_one_numeric(data->client, ERR_ISCHANSERVICE,
				"KILL %s :Cannot kill a network service",
				data->target->name);
		data->approved = 0;
	}
}

DECLARE_MODULE_AV2(no_kill_services, NULL, NULL, NULL, NULL,
			no_kill_services_hfnlist, NULL, NULL, nokill_desc);