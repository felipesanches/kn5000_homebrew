/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * net_none.c -- the KN7000 has no network: what Chocolate Doom's game loop (d_loop.c) and doom/d_main.c call of its
 * network code (net_*.c, left out), as a client that never connects and a server that never starts. A game is always
 * the single-player one d_loop.c sets up when no -server, -connect or -autojoin is given. */
#include <stddef.h>

#include "doomtype.h"
#include "net_client.h"
#include "net_dedicated.h"
#include "net_gui.h"
#include "net_io.h"
#include "net_loop.h"
#include "net_query.h"
#include "net_sdl.h"
#include "net_server.h"

boolean net_client_connected = false, drone = false;
net_waitdata_t net_client_wait_data;
char *net_client_reject_reason = NULL;
net_module_t net_loop_client_module, net_loop_server_module, net_sdl_module;

boolean NET_CL_Connect(net_addr_t *addr, net_connect_data_t *data) { (void)addr; (void)data; return false; }
void NET_CL_Disconnect(void) {}
void NET_CL_Run(void) {}
void NET_CL_StartGame(net_gamesettings_t *settings) { (void)settings; }
void NET_CL_SendTiccmd(ticcmd_t *ticcmd, int maketic) { (void)ticcmd; (void)maketic; }
boolean NET_CL_GetSettings(net_gamesettings_t *settings) { (void)settings; return false; }
void NET_Init(void) {}
void NET_BindVariables(void) {}

void NET_SV_Init(void) {}
void NET_SV_Run(void) {}
void NET_SV_Shutdown(void) {}
void NET_SV_AddModule(net_module_t *module) { (void)module; }
void NET_SV_RegisterWithMaster(void) {}

char *NET_AddrToString(net_addr_t *addr) { (void)addr; return "(no network)"; }
void NET_ReferenceAddress(net_addr_t *addr) { (void)addr; }
void NET_ReleaseAddress(net_addr_t *addr) { (void)addr; }

void NET_LANQuery(void) {}
void NET_MasterQuery(void) {}
void NET_QueryAddress(const char *addr) { (void)addr; }
net_addr_t *NET_FindLANServer(void) { return NULL; }
void NET_WaitForLaunch(void) {}
void NET_DedicatedServer(void) {}
