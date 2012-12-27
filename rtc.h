#ifndef _RTC_H_
#define _RTC_H_

#include <time.h>

void rtc_init();
int rtc_valid();
struct tm *rtc_to_time(uint32_t rtcval, struct tm *res);
uint32_t rtc_from_time(const struct tm *timp);
void rtc_set(uint32_t val);
int time_to_str(char *buf, size_t sz, const struct tm *tim);

#endif
