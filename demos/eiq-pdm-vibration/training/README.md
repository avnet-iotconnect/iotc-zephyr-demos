# Training assets

The actual dataset + report behind the shipped [`../model/`](../model/), so you
can reproduce the training (or skip the capture step entirely and go straight
to eIQ Time Series Studio).

| File | What |
|---|---|
| `capture-session.csv` | raw Phase-1 console capture (10 756 samples, `VIB,t_ms,state,ax_g,ay_g,az_g` @ ~100 Hz, FXLS8974 @ 400 Hz ODR) |
| `tss/{idle,balanced,unbalanced,both}.csv` | the same data split per class by [`../tools/tss_convert.py`](../tools/tss_convert.py) — space-delimited `ax ay az`, ready for TSS import (delimiter = Space, channels = 3) |
| `training-report-78-rforest.pdf` | the eIQ TSS training report for the shipped model (metrics, confusion matrix, pipeline) |

The four classes come from the ML Vibro Sens Click's own motors, driven by the
capture firmware: `idle` (motors off), `balanced` (healthy baseline motor),
`unbalanced` (fault motor), `both` (both motors).
