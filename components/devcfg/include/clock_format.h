#pragma once
#include <time.h>
#include <stdbool.h>

// Format the time line from a broken-down local time.
//   h24: true = 24-hour ("14:05"), false = 12-hour ("2:05 PM")
// Writes into out (size outlen).
void format_clock_time(const struct tm *tm, bool h24, char *out, int outlen);

// Format the date line from a broken-down local time, matching the branding
// preview (toLocaleDateString with month:"long", day:"numeric").
//   weekday: include the full weekday → "Wednesday, June 17"; else "June 17".
// Writes into out (size outlen).
void format_clock_date(const struct tm *tm, bool weekday, char *out, int outlen);

// Format a remaining-seconds count as "M:SS" (e.g. 48 -> "0:48", 125 -> "2:05").
// Negative inputs clamp to 0. Writes into out (size outlen).
void format_mmss(int seconds, char *out, int outlen);
