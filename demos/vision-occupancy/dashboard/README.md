# Dashboard — vision occupancy

![RT1170 vision occupancy dashboard](../docs/images/dashboard.png)

A ready-made IOTCONNECT dashboard export ships here:
[rt1170-vision-occupancy_dashboard_export.json](rt1170-vision-occupancy_dashboard_export.json)
— the layout in the screenshot above, on live hardware data.

## Import

1. In IOTCONNECT: **Dashboards → Create Dashboard → Import dashboard** and
   upload the JSON.
2. When prompted (or per widget afterwards), **bind the widgets to your
   device** — the export was made against a device on the `visionocc`
   template, so any device on that template binds cleanly.
3. The **Latest Snapshot** image widget reads the device's newest **Telemetry
   Files** upload; it populates after your first `snapshot` command.

## What the widgets show

| Widget | Attribute / source | The story it tells |
|---|---|---|
| Occupancy status | `vision.state` | the headline: OCCUPIED / CLEAR, flips in ~1 s |
| Latest Snapshot | Telemetry Files (`snapshot` cmd) | what the camera saw, with the model's verdict as its Classification |
| Person Score gauge | `vision.score` | live confidence vs. the `vision.threshold` trigger |
| Inference gauges | `vision.infer_ms`, `vision.fps` | 22 ms / ~4 fps on the Cortex-M7 — the edge-AI claim |
| Model Source tile | `model.src` | `builtin` → `cloud` → `flash` as pushes/persistence happen |
| Model tiles | `model.name`, `model.ver` | flip live on an AI-Model push, survive reboot |
| Board LED / Camera | `led`, `vision.cam_ok` | hardware sanity at a glance |
| Person vs Clear chart | `vision.score`, `vision.clear_score` | occupancy events read as crossing curves |
| Command console | template commands | `snapshot` / `threshold` / `led-*` with ack history |
| Vitals | `sys.cpu_pct`, heap, `vision.frames` | what the pipeline costs the MCU |
