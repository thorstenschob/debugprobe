/*
 * Local feature overlays for CDC bridge variants.
 */

#ifndef PROBE_LOCAL_FEATURE_CONFIG_H_
#define PROBE_LOCAL_FEATURE_CONFIG_H_

#ifndef CDC_PERF_ENABLE
#define CDC_PERF_ENABLE 0
#endif

#ifndef CDC_PERF_AUTOPRINT_MS
#define CDC_PERF_AUTOPRINT_MS 1000
#endif

#ifndef CDC_PERF_CDC_ITF
#define CDC_PERF_CDC_ITF 2
#endif

#ifndef CDC_UART_ATE_ENABLE
#if CDC_PERF_ENABLE
#define CDC_UART_ATE_ENABLE 0
#else
#define CDC_UART_ATE_ENABLE 1
#endif
#endif

#endif
