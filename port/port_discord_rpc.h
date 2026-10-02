/*
 * port/port_discord_rpc.h — Discord Rich Presence integration.
 *
 * Publishes an activity blurb ("Exploring Hyrule Town · 12 hearts ·
 * 234 rupees") to Discord via the local IPC socket (no external
 * libraries — we speak the protocol directly). Disabled by default;
 * the F8 → Display tab has a toggle.
 *
 * Lifecycle:
 *   - Port_DiscordRpc_Init(client_id) opens the connection lazily on
 *     the first Update call. Safe to call early even if Discord isn't
 *     running.
 *   - Port_DiscordRpc_Update(...) reports the current game state. Call
 *     once every few seconds — Discord rate-limits to one per 15s and
 *     drops excess updates.
 *   - Port_DiscordRpc_Shutdown() closes the socket cleanly on exit.
 *
 * No-op on platforms we haven't ported the IPC client to yet (Windows
 * needs the named-pipe path, which is TODO).
 */

#ifndef PORT_DISCORD_RPC_H
#define PORT_DISCORD_RPC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Port_DiscordRpc_SetEnabled(bool on);
bool Port_DiscordRpc_IsEnabled(void);

/* One-shot enqueue — coalesces multiple calls within the rate-limit
 * window so callers can just spam it from per-frame code without
 * caring about spam. The "elapsed" timestamp Discord shows is anchored
 * to the moment Rich Presence was first enabled this session; no need
 * for the caller to pass a start time. */
void Port_DiscordRpc_Update(const char* area_name,
                            int hearts_now, int hearts_max,
                            int rupees);

void Port_DiscordRpc_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_DISCORD_RPC_H */
