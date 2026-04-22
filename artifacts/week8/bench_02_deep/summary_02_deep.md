# Bench 02 Deep Summary

| scenario | policy | throughput_mean | throughput_std | p95_mean | p99_mean | 503_mean | 504_mean | success_mean |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| normal | pool | 17462.41 | 4257.28 | 3.29 | 4.95 | 0.0000 | 0.0000 | 0.3000 |
| normal | per_request | 18994.63 | 3026.58 | 3.10 | 5.48 | 0.0000 | 0.0000 | 0.1000 |
| burst | pool | 17243.53 | 4174.60 | 13.71 | 18.90 | 0.0847 | 0.0000 | 0.2153 |
| burst | per_request | 19006.23 | 2610.86 | 11.87 | 17.85 | 0.0000 | 0.0000 | 0.0848 |
| saturation | pool | 16594.96 | 4676.74 | 25.47 | 34.55 | 0.2428 | 0.0000 | 0.0563 |
| saturation | per_request | 19704.23 | 863.23 | 15.44 | 23.17 | 0.0000 | 0.0000 | 0.0000 |
