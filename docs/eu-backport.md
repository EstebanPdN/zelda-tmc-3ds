# Native European compatibility fixes

The port accepts a clean European ROM. It does not require an IPS patch or a
modified ROM. Its native implementation includes the three gameplay fixes
documented by [Prof9's Minish Cap EU Backport](https://github.com/Prof9/Minish-Cap-EU-Backport):

- Cancelling Eenie's first Kinstone fusion keeps the fusion available. A narrowly
  identified older vanilla save can recover that offer after a durable profile
  backup; completed fusions and Randomizer saves are excluded from this repair.
- Stockwell sells the missing 600-Rupee bomb bag upgrade after the wallet and
  boomerang prerequisites. The added offer uses original English wording and
  the active ROM's purchase menu. The European item-receipt message remains
  European.
- The Ocarina works on the Wind Tribe tower roof using the port's shared area
  permission flag.

These are native C corrections inspired by the behavior documented in Prof9's
Unlicense project. No ROM patch, ROM bytes, extracted graphics, or Nintendo
text is included here.

Regional compatibility also requires selecting data before resolving embedded
GBA pointers. The Goron wall-event table is one such case: selecting a USA
pointer and then interpreting it inside a European ROM addresses unrelated
data. The resolver now selects the regional table, checks its bounds, and
validates the complete tile pattern before applying it.

Host regressions exercise the production callbacks and metadata with
`xmake build -y eu_backport_test kinstone_integration_test region_data_resolver_test`.
The optional native room-capture harness supports `TMC_ROOMCAP_GORON_STAGE=1..6`
to run actual regional world-event scripts. Run it from an isolated working
directory with a privately supplied ROM: the normal save subsystem remains
active. These tests do not establish physical 3DS performance or completion of
an entire playthrough.
