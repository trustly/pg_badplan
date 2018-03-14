#include "postgres.h"
#include "executor/execdesc.h"
#include "executor/executor.h"
#include "miscadmin.h"
#include "utils/guc.h"
#include "utils/elog.h"

#ifndef EXTNAME
#define EXTNAME "pg_badplan"
#endif

PG_MODULE_MAGIC;

void		_PG_init(void);
void		_PG_fini(void);

static void pgpwo_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void pgpwo_ExecutorEnd(QueryDesc *queryDesc);
static void pgpwo_RecalculateRatio(double val, void *extra);

static bool pgpwo_enabled;
static double pgpwo_ratio;
static double pgpwo_ratio_under;
static double pgpwo_ratio_over;
static int pgpwo_min_row_threshold;

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
							 100,
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
				ereport(LOG, (errmsg_internal(EXTNAME ": expected=%.0f actual=%.0f ratio=%.3f (%.3f)", expected, actual, ratio, pgpwo_ratio)));
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