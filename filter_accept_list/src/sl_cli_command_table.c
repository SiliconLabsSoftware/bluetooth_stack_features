/***************************************************************************//**
 * @file sl_cli_command_table.c
 * @brief Declarations of relevant command structs for cli framework.
 * @version x.y.z
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#include <stdlib.h>

#include "sl_cli_config.h"
#include "sl_cli_command.h"
#include "sl_cli_arguments.h"
#include "cli_functions.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 *****************************   TEMPLATED FILE   ******************************
 ******************************************************************************/

/*******************************************************************************
 * Example syntax (.slcc or .slcp) for populating this file:
 *
 *   template_contribution:
 *     - name: cli_command          # Register a command
 *       value:
 *         name: status             # Name of command
 *         handler: status_command  # Function to be called. Must be defined
 *         help: "Prints status"    # Optional help description
 *         shortcuts:               # Optional shortcut list
 *           - name: st
 *         argument:                # Argument list, if apliccable
 *           - type: uint8          # Variable type
 *             help: "Channel"      # Optional description
 *           - type: string
 *             help: "Text"
 *     - name: cli_group            # Register a group
 *       value:
 *         name: shell              # Group name
 *         help: "Shell commands"   # Optional help description
 *         shortcuts:               # Optional shortcuts
 *           - name: sh
 *     - name: cli_command
 *       value:
 *         name: repeat
 *         handler: repeat_cmd
 *         help: "Repeat commands"
 *         shortcuts:
 *           - name: r
 *           - name: rep
 *         group: shell            # Associate command with group
 *         argument:
 *           - type: string
 *             help: "Text"
 *           - type: additional
 *             help: "More text"
 *
 * For subgroups, an optional unique id can be used to allow a particular name to
 * be used more than once. In the following case, from the command line the
 * following commands are available:
 *
 * >  root_1 shell status
 * >  root_2 shell status
 *
 *     - name: cli_group            # Register a group
 *       value:
 *         name: root_1             # Group name
 *
 *     - name: cli_group            # Register a group
 *       value:
 *         name: root_2             # Group name
 *
 *    - name: cli_group             # Register a group
 *       value:
 *         name: shell              # Group name
 *         id: shell_root_1         # Optional unique id for group
 *         group: root_1            # Add group to root_1 group
 *
 *    - name: cli_group             # Register a group
 *       value:
 *         name: shell              # Group name
 *         id: shell_root_2         # Optional unique id for group
 *         group: root_2            # Add group to root_1 group
 *
 *    - name: cli_command           # Register a command
 *       value:
 *         name: status
 *         handler: status_1
 *         group: shell_root_1      # id of subgroup
 *
 *    - name: cli_command           # Register a command
 *       value:
 *         name: status
 *         handler: status_2
 *         group: shell_root_2      # id of subgroup
 *
 ******************************************************************************/

/*******************************************************************************
 * No template contributions supplied to project. Provide external definition
 * of command table or regenerate project with template contributions.
 ******************************************************************************/
static const sl_cli_command_info_t cli_cmd_scan =
  SL_CLI_COMMAND(scan,
                 "Control scanning.",
                 "0=Disable 1=Enable" SL_CLI_UNIT_SEPARATOR,
                 { SL_CLI_ARG_UINT8, SL_CLI_ARG_END, });

static const sl_cli_command_info_t cli_cmd_connect =
  SL_CLI_COMMAND(connect,
                 "Connect a discovered device.",
                 "index of the discovered device",
                 { SL_CLI_ARG_UINT8, SL_CLI_ARG_END, });

static const sl_cli_command_info_t cli_cmd_advertise =
  SL_CLI_COMMAND(advertise,
                 "Control advertising.",
                 "0=Disable 1=Enable" SL_CLI_UNIT_SEPARATOR,
                 { SL_CLI_ARG_UINT8, SL_CLI_ARG_END, });

static const sl_cli_command_info_t cli_cmd_pair =
  SL_CLI_COMMAND(pair,
                 "Pair with the connected device.",
                 "",
                 { SL_CLI_ARG_END, });

static const sl_cli_command_info_t cli_cmd_add_peer =
  SL_CLI_COMMAND(add_peer,
                 "Add the currently connected device to the Filter Accept List.",
                 "",
                 { SL_CLI_ARG_END, });

static const sl_cli_command_info_t cli_cmd_add_discovered =
  SL_CLI_COMMAND(add_discovered,
                 "Add a discovered device to the Filter Accept List.",
                 "index of the discovered device",
                 { SL_CLI_ARG_UINT8, SL_CLI_ARG_END, });

static const sl_cli_command_info_t cli_cmd_filter =
  SL_CLI_COMMAND(filter,
                 "Control filtering.",
                 "0=Disable 1=Enable" SL_CLI_UNIT_SEPARATOR,
                 { SL_CLI_ARG_UINT8, SL_CLI_ARG_END, });

static const sl_cli_command_info_t cli_cmd_disconnect =
  SL_CLI_COMMAND(disconnect,
                 "Close the current connection.",
                 "",
                 { SL_CLI_ARG_END, });

static const sl_cli_command_info_t cli_cmd_bondings =
  SL_CLI_COMMAND(bondings,
                 "List the bonded devices.",
                 "",
                 { SL_CLI_ARG_END, });

static const sl_cli_command_info_t cli_cmd_clear =
  SL_CLI_COMMAND(clear,
                 "Clear the Filter Accept List.",
                 "",
                 { SL_CLI_ARG_END, });

static const sl_cli_command_info_t cli_cmd_unpair =
  SL_CLI_COMMAND(unpair,
                 "Unpair a device in the bonded devices list.",
                 "index in the bonded devices list",
                 { SL_CLI_ARG_UINT8, SL_CLI_ARG_END, });

const sl_cli_command_entry_t sl_cli_default_command_table[] =
{
  { "scan", &cli_cmd_scan, false },
  { "connect", &cli_cmd_connect, false },
  { "advertise", &cli_cmd_advertise, false },
  { "add_peer", &cli_cmd_add_peer, false },
  { "add_discovered", &cli_cmd_add_discovered, false },
  { "clear", &cli_cmd_clear, false },
  { "pair", &cli_cmd_pair, false },
  { "bondings", &cli_cmd_bondings, false },
  { "filter", &cli_cmd_filter, false },
  { "disconnect", &cli_cmd_disconnect, false },
  { "unpair", &cli_cmd_unpair, false },
  { NULL, NULL, false }
};

#ifdef __cplusplus
}
#endif
