#include "postgres.h"
#include "executor/execdesc.h"
#include "executor/executor.h"
#include "miscadmin.h"
#include "utils/guc.h"
#include "utils/elog.h"
#include <unistd.h>

#ifndef EXTNAME
#define EXTNAME "pg_badplan"
#endif

PG_MODULE_MAGIC;

void		_PG_init(void);
void		_PG_fini(void);

static void pgpwo_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void pgpwo_ExecutorEnd(QueryDesc *queryDesc);
static void pgpwo_RecalculateRatio(double val, void *extra);
static bool pgpwo_CheckLogdirConf(char **val, void **extra, GucSource src);

static bool pgpwo_enabled;
static double pgpwo_ratio;
static double pgpwo_ratio_under;
static double pgpwo_ratio_over;
static int pgpwo_min_row_threshold;
static char *pgpwo_logdir;
static int pgpwo_min_dump_interval_ms;
static int64_t pgpwo_last_ts;

static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

void _PG_init(void) {
	if (!process_shared_preload_libraries_in_progress)
	{
		elog(ERROR, "This module can only be loaded via shared_preload_libraries");
		return;
	}

	prev_ExecutorStart = ExecutorStart_hook;
	ExecutorStart_hook = pgpwo_ExecutorStart;

	prev_ExecutorEnd = ExecutorEnd_hook;
	ExecutorEnd_hook = pgpwo_ExecutorEnd;

	DefineCustomBoolVariable(EXTNAME ".enabled",
							 "Enable / Disable pg_planwayoff",
							 NULL,
							 &pgpwo_enabled,
							 true,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomRealVariable(EXTNAME ".ratio",
							 "Set ratio",
							 NULL,
							 &pgpwo_ratio,
							 0.2,
							 0,
							 1,
							 PGC_USERSET,
							 0,
							 NULL,
							 pgpwo_RecalculateRatio,
							 NULL);

	DefineCustomIntVariable(EXTNAME ".min_row_threshold",
							 "Set minimum row threshold",
							 NULL,
							 &pgpwo_min_row_threshold,
							 1000,
							 0,
							 INT_MAX,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomStringVariable(EXTNAME ".logdir",
							"Set log directory",
							NULL,
							&pgpwo_logdir,
							NULL,
							PGC_USERSET,
							0,
							pgpwo_CheckLogdirConf,
							NULL,
							NULL);

	DefineCustomIntVariable(EXTNAME ".min_dump_interval_ms",
							 "Set minimum dump to file interval (in milliseconds)",
							 NULL,
							 &pgpwo_min_dump_interval_ms,
							 60000,
							 0,
							 INT_MAX,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

}

/*
Restore previous hooks on module unload
*/
void _PG_fini(void) {
	ExecutorEnd_hook = prev_ExecutorEnd;
	ExecutorStart_hook = prev_ExecutorStart;
}

/* 
Whenever we change the ratio recalculate the over and under thresholds
*/
void pgpwo_RecalculateRatio(double val, void *extra) {
	pgpwo_ratio_under = val;
	pgpwo_ratio_over = 1 / val;
	ereport(LOG, (errmsg_internal(EXTNAME ": setting ratio to %.3f > x > %.3f", pgpwo_ratio_over, pgpwo_ratio_under)));
}

/*
Called when setting the logdir. Checks if the dir is available and writeable
*/
static bool pgpwo_CheckLogdirConf(char **val, void **extra, GucSource src) {
	/* Check if dir exists and we can write to it */
	int r;

	if (val == NULL || *val == NULL) {
		return TRUE;
	}

	/* Empty string set to NULL */
	if (**val == '\0') {
		*val = NULL;
		return TRUE;
	}

	/* Check if we can stat and write to dir */
	r = access(*val, W_OK);
	if (r == 0) {
		return TRUE;
	}
	else {
		ereport(ERROR, (errmsg_internal(EXTNAME ": failed to set logdir. access() returned %d (%s)", r, strerror(r))));
	}

	return FALSE;
}

/*
We need to enable instrumentation otherwise the actual rows aren't calculated
*/
static void pgpwo_ExecutorStart(QueryDesc *queryDesc, int eflags) {
	if (pgpwo_enabled) {
		queryDesc->instrument_options |= INSTRUMENT_ROWS;
	}

	/* Continue running ExecutorStart hooks */
	if (prev_ExecutorStart) {
		prev_ExecutorStart(queryDesc, eflags);
	}
	else {
		standard_ExecutorStart(queryDesc, eflags);
	}
}

/*
Checks if the rows estimate vs rows actual is outside ratio
 */
static void pgpwo_ExecutorEnd(QueryDesc *queryDesc) {
	if (pgpwo_enabled) {
		if (queryDesc->plannedstmt != NULL &&
			queryDesc->planstate != NULL && 
			queryDesc->planstate->instrument != NULL) {

			InstrEndLoop(queryDesc->planstate->instrument);

			double expected = queryDesc->plannedstmt->planTree->plan_rows;
			double nloops = queryDesc->planstate->instrument->nloops;
			double actual = queryDesc->planstate->instrument->ntuples / nloops;
			double ratio = actual/expected;

			if ((ratio <= pgpwo_ratio_under || ratio >= pgpwo_ratio_over) && 
				(expected > pgpwo_min_row_threshold || actual > pgpwo_min_row_threshold)) {

				if (pgpwo_logdir != NULL) {
					struct timespec tp;
					clock_gettime(CLOCK_MONOTONIC_RAW, &tp);

					int64_t curr_ms = (tp.tv_sec * 1000) + (tp.tv_nsec / 100000);

					if (curr_ms - pgpwo_last_ts > pgpwo_min_dump_interval_ms) {
						char path[MAXPGPATH];
						File fd;

						snprintf(path, MAXPGPATH, "%s/" EXTNAME "-%d-%lld.sql", pgpwo_logdir, MyBackendId, curr_ms);

						ereport(LOG, (errmsg_internal(EXTNAME ": writing dump to path %s", path)));

						if ((fd = PathNameOpenFile(path, O_WRONLY | O_CREAT, 0644)) > 0) {
							FileWrite(fd, (char *) queryDesc->sourceText, strlen(queryDesc->sourceText), 0);
							FileClose(fd);
						} else {
							ereport(LOG, (errmsg_internal(EXTNAME ": failed to open %s because of %s (%d)", path, strerror(fd), fd)));
						}

						pgpwo_last_ts = curr_ms;
					}
				} else {
					ereport(LOG, (errmsg_internal(EXTNAME ": rows expected/actual ratio %0.3f exceeded for query %s", ratio, queryDesc->sourceText)));
				}
			}
		}
	}
	
	/* Continue running ExecutorEnd hooks */
	if (prev_ExecutorEnd) {
		prev_ExecutorEnd(queryDesc);
	}
	else {
		standard_ExecutorEnd(queryDesc);
	}
}