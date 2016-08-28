/*
 * Remote client connect/exit notices on snomask +F (far).
 * To avoid flooding, connects/exits part of netjoins/netsplits are not shown.
 * Consequently, it is not possible to use these notices to keep track
 * of all clients.
 * -- jilles
 */

using namespace ircd;

static const char sno_desc[] =
	"Adds server notice mask +F that allows operators to receive notices for connections on other servers";

static int _modinit(void);
static void _moddeinit(void);
static void h_gcn_new_remote_user(client::client *);
static void h_gcn_client_exit(hook_data_client_exit *);

mapi_hfn_list_av1 gcn_hfnlist[] = {
	{ "new_remote_user", (hookfn) h_gcn_new_remote_user },
	{ "client_exit", (hookfn) h_gcn_client_exit },
	{ NULL, NULL }
};

sno::mode SNO_FARCONNECT { 'F' };

DECLARE_MODULE_AV2(globalconnexit, _modinit, nullptr, NULL, NULL, gcn_hfnlist, NULL, NULL, sno_desc);

static int
_modinit(void)
{
	/* show the fact that we are showing user information in /version */
	opers_see_all_users = true;

	return 0;
}

static void
h_gcn_new_remote_user(client::client *source_p)
{

	if (!has_sent_eob(*source_p->servptr))
		return;
	sendto_realops_snomask_from(SNO_FARCONNECT, L_ALL, source_p->servptr,
			"Client connecting: %s (%s@%s) [%s] {%s} [%s]",
			source_p->name, source_p->username, source_p->orighost,
			show_ip(NULL, source_p) ? source_p->sockhost : "255.255.255.255",
			"?", source_p->info);
}

static void
h_gcn_client_exit(hook_data_client_exit *hdata)
{
	client::client *source_p;

	source_p = hdata->target;

	if (my_connect(*source_p) || !is_client(*source_p))
		return;
	if (!has_sent_eob(*source_p->servptr))
		return;
	sendto_realops_snomask_from(SNO_FARCONNECT, L_ALL, source_p->servptr,
			     "Client exiting: %s (%s@%s) [%s] [%s]",
			     source_p->name,
			     source_p->username, source_p->host, hdata->comment,
                             show_ip(NULL, source_p) ? source_p->sockhost : "255.255.255.255");
}
