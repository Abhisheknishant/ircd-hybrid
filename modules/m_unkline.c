/*
 *  ircd-hybrid: an advanced, lightweight Internet Relay Chat Daemon (ircd)
 *
 *  Copyright (c) 1997-2019 ircd-hybrid development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 *  USA
 */

/*! \file m_unkline.c
 * \brief Includes required functions for processing the UNKLINE command.
 * \version $Id$
 */

#include "stdinc.h"
#include "list.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "conf.h"
#include "conf_cluster.h"
#include "conf_shared.h"
#include "hostmask.h"
#include "numeric.h"
#include "log.h"
#include "misc.h"
#include "send.h"
#include "server_capab.h"
#include "parse.h"
#include "modules.h"
#include "memory.h"


/* static int remove_tkline_match(const char *host, const char *user)
 * Input: A hostname, a username to unkline.
 * Output: returns YES on success, NO if no tkline removed.
 * Side effects: Any matching tklines are removed.
 */
static bool
kline_remove(const struct aline_ctx *aline)
{
  struct irc_ssaddr iphost, *piphost;
  struct MaskItem *conf;

  if (parse_netmask(aline->host, &iphost, NULL) != HM_HOST)
    piphost = &iphost;
  else
    piphost = NULL;

  if ((conf = find_conf_by_address(aline->host, piphost, CONF_KLINE, aline->user, NULL, 0)))
  {
    if (IsConfDatabase(conf))
    {
      delete_one_address_conf(aline->host, conf);
      return true;
    }
  }

  return false;
}

static void
kline_remove_and_notify(struct Client *source_p, const struct aline_ctx *aline)
{
  if (kline_remove(aline) == true)
  {
    if (IsClient(source_p))
      sendto_one_notice(source_p, &me, ":K-Line for [%s@%s] is removed",
                        aline->user, aline->host);

    sendto_realops_flags(UMODE_SERVNOTICE, L_ALL, SEND_NOTICE,
                         "%s has removed the K-Line for: [%s@%s]",
                         get_oper_name(source_p), aline->user, aline->host);
    ilog(LOG_TYPE_KLINE, "%s removed K-Line for [%s@%s]",
         get_oper_name(source_p), aline->user, aline->host);
  }
  else if (IsClient(source_p))
    sendto_one_notice(source_p, &me, ":No K-Line for [%s@%s] found",
                      aline->user, aline->host);
}

/*! \brief UNKLINE command handler
 *
 * \param source_p Pointer to allocated Client struct from which the message
 *                 originally comes from.  This can be a local or remote client.
 * \param parc     Integer holding the number of supplied arguments.
 * \param parv     Argument vector where parv[0] .. parv[parc-1] are non-NULL
 *                 pointers.
 * \note Valid arguments for this command are:
 *      - parv[0] = command
 *      - parv[1] = user\@host mask
 *      - parv[2] = "ON"
 *      - parv[3] = target server
 */
static int
mo_unkline(struct Client *source_p, int parc, char *parv[])
{
  struct aline_ctx aline = { .add = false, .simple_mask = false };

  if (!HasOFlag(source_p, OPER_FLAG_UNKLINE))
  {
    sendto_one_numeric(source_p, &me, ERR_NOPRIVS, "unkline");
    return 0;
  }

  if (EmptyString(parv[1]))
  {
    sendto_one_numeric(source_p, &me, ERR_NEEDMOREPARAMS, "UNKLINE");
    return 0;
  }

  if (parse_aline("UNKLINE", source_p, parc, parv, &aline) == false)
    return 0;

  if (aline.server)
  {
     sendto_match_servs(source_p, aline.server, CAPAB_UNKLN, "UNKLINE %s %s %s",
                        aline.server, aline.user, aline.host);

    /* Allow ON to apply local unkline as well if it matches */
    if (match(aline.server, me.name))
      return 0;
  }
  else
    cluster_distribute(source_p, "UNKLINE", CAPAB_UNKLN, CLUSTER_UNKLINE,
                       "%s %s", aline.user, aline.host);

  kline_remove_and_notify(source_p, &aline);
  return 0;
}

/*! \brief UNKLINE command handler
 *
 * \param source_p Pointer to allocated Client struct from which the message
 *                 originally comes from.  This can be a local or remote client.
 * \param parc     Integer holding the number of supplied arguments.
 * \param parv     Argument vector where parv[0] .. parv[parc-1] are non-NULL
 *                 pointers.
 * \note Valid arguments for this command are:
 *      - parv[0] = command
 *      - parv[1] = target server mask
 *      - parv[2] = user mask
 *      - parv[3] = host mask
 */
static int
ms_unkline(struct Client *source_p, int parc, char *parv[])
{
  struct aline_ctx aline =
  {
    .add = false,
    .simple_mask = false,
    .user = parv[2],
    .host = parv[3],
    .server = parv[1]
  };

  if (parc != 4 || EmptyString(parv[parc - 1]))
    return 0;

  sendto_match_servs(source_p, aline.server, CAPAB_UNKLN, "UNKLINE %s %s %s",
                     aline.server, aline.user, aline.host);

  if (match(aline.server, me.name))
    return 0;

  if (HasFlag(source_p, FLAGS_SERVICE) ||
      shared_find(SHARED_UNKLINE, source_p->servptr->name,
                  source_p->username, source_p->host))
    kline_remove_and_notify(source_p, &aline);

  return 0;
}

static struct Message unkline_msgtab =
{
  .cmd = "UNKLINE",
  .args_min = 2,
  .args_max = MAXPARA,
  .handlers[UNREGISTERED_HANDLER] = m_unregistered,
  .handlers[CLIENT_HANDLER] = m_not_oper,
  .handlers[SERVER_HANDLER] = ms_unkline,
  .handlers[ENCAP_HANDLER] = m_ignore,
  .handlers[OPER_HANDLER] = mo_unkline
};

static void
module_init(void)
{
  mod_add_cmd(&unkline_msgtab);
  capab_add("UNKLN", CAPAB_UNKLN);
}

static void
module_exit(void)
{
  mod_del_cmd(&unkline_msgtab);
  capab_del("UNKLN");
}

struct module module_entry =
{
  .version = "$Revision$",
  .modinit = module_init,
  .modexit = module_exit,
};
