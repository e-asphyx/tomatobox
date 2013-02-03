#include <stdlib.h>
#include <stdio.h>

#include "stm32f10x.h"
#include "rtc.h"

#define BKP_MAGIC 0xAAAA

static int rtc_is_valid = 1;

void rtc_init() {
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);

	if(BKP->DR1 != BKP_MAGIC) {
		PWR_BackupAccessCmd(ENABLE);                        /* Allow write access to BKP Domain */

		RCC_BackupResetCmd(ENABLE);                         /* Reset Backup Domain */
		RCC_BackupResetCmd(DISABLE);

		RCC_LSEConfig(RCC_LSE_ON);                          /* Enable LSE */
		while(RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET); /* Wait till LSE is ready */
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);             /* Select LSE as RTC Clock Source */
		RCC_RTCCLKCmd(ENABLE);                              /* Enable RTC Clock */

		RTC_WaitForSynchro();                               /* Wait for RTC registers synchronization */
		RTC_WaitForLastTask();
		RTC_SetPrescaler(32767);                            /* RTC period = RTCCLK/RTC_PR = (32.768 KHz)/(32767+1) */
		RTC_WaitForLastTask();

		BKP->DR1 = BKP_MAGIC;

		PWR_BackupAccessCmd(DISABLE);                       /* Protect backup registers */

		rtc_is_valid = 0;
	} else {
		/* Wait for RTC registers synchronization */
		RTC_WaitForSynchro();
	}
}

int rtc_valid() {
	return rtc_is_valid;
}

void rtc_set(uint32_t val) {
	PWR_BackupAccessCmd(ENABLE);

	RTC_SetCounter(val);
	RTC_WaitForLastTask();

	PWR_BackupAccessCmd(DISABLE);
}


/*-----------------------------------------------------------------------------*/
/* based on newlib implementation */
/*-----------------------------------------------------------------------------*/

#define YEAR_BASE 1900
#define EPOCH_YEAR 2000
#define EPOCH_WDAY 6

#define SECSPERHOUR (60 * 60)
#define SECSPERDAY (SECSPERHOUR * 24)
#define ISLEAP(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)
#define YEAR_LENGTH(leap) ((leap) ? 366 : 365)
#define MON_LENGTH(leap, mon) (mon_lengths[mon] + (((mon) == 1) && (leap)))

static const int mon_lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

struct tm *rtc_to_time(uint32_t rtcval, struct tm *res) {
	/* base decision about std/dst time on current time */
	long days = ((int32_t)rtcval) / SECSPERDAY;
	long rem = ((int32_t)rtcval) % SECSPERDAY;

	while(rem < 0) {
		rem += SECSPERDAY;
		days--;
	}

	while(rem >= SECSPERDAY) {
		rem -= SECSPERDAY;
		days++;
	}

	/* compute hour, min, and sec */
	res->tm_hour = (int)(rem / SECSPERHOUR);
	rem %= SECSPERHOUR;
	res->tm_min = (int)(rem / 60);
	res->tm_sec = (int)(rem % 60);

	/* compute day of week */
	if((res->tm_wday = ((EPOCH_WDAY + days) % 7)) < 0) res->tm_wday += 7;

	/* compute year & day of year */
	int y = EPOCH_YEAR;
	int yleap;
	if(days >= 0) {
		while(1) {
			yleap = ISLEAP(y);
			if(days < YEAR_LENGTH(yleap)) break;
			y++;
			days -= YEAR_LENGTH(yleap);
		}
	} else {
		do {
			y--;
			yleap = ISLEAP(y);
			days += YEAR_LENGTH(yleap);
		} while (days < 0);
	}

	res->tm_year = y - YEAR_BASE;
	res->tm_yday = days;

	res->tm_mon = 0;
	while(days >= MON_LENGTH(yleap, res->tm_mon)) {
		days -= MON_LENGTH(yleap, res->tm_mon);
		res->tm_mon++;
	}
	res->tm_mday = days + 1;

	res->tm_isdst = 0;

	return res;
}

static const int days_before_month[12] =
	{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

uint32_t rtc_from_time(const struct tm *timp) {
	int year_abs = timp->tm_year + YEAR_BASE;

	/* compute hours, minutes, seconds */
	int32_t tim = timp->tm_sec + (timp->tm_min * 60) + (timp->tm_hour * SECSPERHOUR);

	/* compute days in year */
	long days = timp->tm_mday - 1 + days_before_month[timp->tm_mon];
	if(timp->tm_mon > 1 && ISLEAP(year_abs)) days++;

	/* compute days in other years */
	int year = year_abs;
	if(year > EPOCH_YEAR) {
		for(year = EPOCH_YEAR; year < year_abs; year++) days += YEAR_LENGTH(ISLEAP(year));
	} else if (year < EPOCH_YEAR) {
		for(year = EPOCH_YEAR - 1; year >= year_abs; year--) days -= YEAR_LENGTH(ISLEAP(year));
    }

	/* compute total seconds */
	tim += (days * SECSPERDAY);

	return (uint32_t)tim;
}

int time_to_str(char *buf, size_t sz, const struct tm *tim) {
	return sniprintf(buf, sz, "%02d:%02d:%02d %02d-%02d-%d",
					tim->tm_hour, tim->tm_min, tim->tm_sec,
					tim->tm_mday, tim->tm_mon + 1, YEAR_BASE + tim->tm_year);
}

int validate_time(const struct tm *tim) {
	return tim->tm_hour >= 0 && tim->tm_hour <= 23 &&
		tim->tm_min >= 0 && tim->tm_min <= 59 &&
		tim->tm_sec >= 0 && tim->tm_sec <= 59;
}

int validate_date(const struct tm *tim) {
	return tim->tm_mday >= 1 && tim->tm_mday <= 31 &&
		tim->tm_mon >= 0 && tim->tm_mon <= 11;
}

int parse_time(const char *str, struct tm *tim) {
	char *end;
	/* hours */
	tim->tm_hour = strtol(str, &end, 10);
	if(end != str && *end) {
		str = end + 1;
		/* minutes */
		tim->tm_min = strtol(str, &end, 10);
		if(end != str) {
			/* seconds (optional) */
			tim->tm_sec = *end ? strtol(end + 1, NULL, 10) : 0;

			return validate_time(tim);
		}
	}
	return 0;
}

int parse_date(const char *str, struct tm *tim) {
	char *end;
	/* day */
	tim->tm_mday = strtol(str, &end, 10);
	if(end != str && *end) {
		str = end + 1;
		/* month */
		tim->tm_mon = strtol(str, &end, 10);
		if(end != str && *end) {
			str = end + 1;
			/* year */
			tim->tm_year = strtol(str, &end, 10);
			if(end != str) {
				tim->tm_mon--;
				tim->tm_year -= 1900;

				return validate_date(tim);
			}
		}
	}
	return 0;
}
