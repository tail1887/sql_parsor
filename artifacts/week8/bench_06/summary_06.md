# Bench 06 Summary

| scenario | policy | throughput_mean | p95_mean | p99_mean | 503_mean | 504_mean | success_mean |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| burst | dynamic | 18764.17 | 12.05 | 18.00 | 0.0348 | 0.0000 | 0.0901 |
| burst | fixed | 14411.82 | 18.46 | 24.25 | 0.2099 | 0.0945 | 0.3206 |
| normal | dynamic | 18924.24 | 2.98 | 4.52 | 0.0000 | 0.0000 | 0.1250 |
| normal | fixed | 14585.53 | 3.85 | 5.65 | 0.0000 | 0.0000 | 0.6250 |
| saturation | dynamic | 18802.88 | 13.27 | 21.65 | 0.0936 | 0.0000 | 0.0314 |
| saturation | fixed | 13841.98 | 27.24 | 35.60 | 0.3200 | 0.2959 | 0.0089 |
