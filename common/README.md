# common/

Shared helpers reused across demos, so each demo stays focused on what it
*demonstrates* rather than boilerplate. Candidates as the repo grows:

- **Network bring-up** — wait-for-L4 / DHCP helper (currently inlined in the SDK
  sample's `main.c`; lift here once a second demo needs it).
- **Credential provisioning** — wrapper around `creds/gen_creds_header.py` and the
  PEM → `device_credentials.h` flow, so every demo provisions the same way.
- **Telemetry helpers** — small builders on top of `iotcl_telemetry_*`.

Keep these vendor-neutral. Anything that needs a vendor HAL/peripheral belongs in
`vendor/<name>/`, not here.
