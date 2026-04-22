# Bench 02 Summary

| scenario | policy | throughput_mean | throughput_std | p95_mean | p95_std | error503_mean | error503_std |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| normal | pool | 11447.07 | 186.66 | 1.58 | 0.14 | 0.0000 | 0.0000 |
| normal | per_request | 11648.91 | 137.42 | 1.49 | 0.10 | 0.0000 | 0.0000 |
| burst | pool | 11410.10 | 1035.06 | 8.17 | 1.25 | 0.0000 | 0.0000 |
| burst | per_request | 12240.27 | 91.16 | 7.10 | 0.09 | 0.0000 | 0.0000 |
| saturation | pool | 2067.96 | 33.01 | 11.98 | 0.27 | 0.0000 | 0.0000 |
| saturation | per_request | 2130.17 | 71.49 | 12.33 | 0.45 | 0.0000 | 0.0000 |
