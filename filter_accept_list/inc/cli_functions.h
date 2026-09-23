/*
 * cli_functions.h
 *
 */

#ifndef CLI_FUNCTIONS_H_
#define CLI_FUNCTIONS_H_

#include "sl_cli_types.h"
#ifdef __cplusplus
extern "C" {
#endif
void scan(sl_cli_command_arg_t *arguments);
void connect(sl_cli_command_arg_t *arguments);
void advertise(sl_cli_command_arg_t *arguments);
void connect(sl_cli_command_arg_t *arguments);
void pair(sl_cli_command_arg_t *arguments);
void filter(sl_cli_command_arg_t *arguments);
void clear(sl_cli_command_arg_t *arguments);
void add_peer(sl_cli_command_arg_t *arguments);
void add_discovered(sl_cli_command_arg_t *arguments);
void disconnect(sl_cli_command_arg_t *arguments);
void bondings(sl_cli_command_arg_t *arguments);
void unpair(sl_cli_command_arg_t *arguments);
#ifdef __cplusplus
}
#endif
#endif /* CLI_FUNCTIONS_H_ */
