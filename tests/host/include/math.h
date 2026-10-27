// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#define isfinite(x) __builtin_isfinite(x)
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define NAN __builtin_nan("")
double floor(double);
double fabs(double);
double sqrt(double);
