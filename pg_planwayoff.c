#include "postgres.h"
#include "executor/execdesc.h"
#include "executor/executor.h"
#include "miscadmin.h"
#include "utils/guc.h"
#include "utils/elog.h"

PG_MODULE_MAGIC;

void		_PG_init(void);
void		_PG_fini(void);

static void pgpwo_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void pgpwo_ExecutorEnd(QueryDesc *queryDesc);

static bool pgpwo_enabled;

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

	DefineCustomBoolVariable("pg_planwayoff.enabled",
							 "Enable / Disable pg_planwayoff",
							 NULL,
							 &pgpwo_enabled,
							 true,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);
}

void _PG_fini(void) {
	ExecutorEnd_hook = prev_ExecutorEnd;
	ExecutorStart_hook = prev_ExecutorStart;
}

/*
Enable instrumentation
*/
static void pgpwo_ExecutorStart(QueryDesc *queryDesc, int eflags) {
	queryDesc->instrument_options |= INSTRUMENT_ROWS;

	if (prev_ExecutorStart) {
		prev_ExecutorStart(queryDesc, eflags);
	}
	else {
		standard_ExecutorStart(queryDesc, eflags);
	}
}

/*
Checks if the estimate vs actual is way off and if so log
 */
static void pgpwo_ExecutorEnd(QueryDesc *queryDesc) {
	if (queryDesc->plannedstmt != NULL &&
		queryDesc->planstate != NULL && 
		queryDesc->planstate->instrument != NULL) {

		InstrEndLoop(queryDesc->planstate->instrument);

		double expected = queryDesc->plannedstmt->planTree->plan_rows;
		double nloops = queryDesc->planstate->instrument->nloops;
		double actual = queryDesc->planstate->instrument->ntuples / nloops;
		double ratio = actual/expected;

		ereport(LOG, (errmsg_internal("pgplanwayoff: expected=%.0f actual=%.0f ratio=%.0f", expected, actual, ratio)));
	}
	
	if (prev_ExecutorEnd) {
		prev_ExecutorEnd(queryDesc);
	}
	else {
		standard_ExecutorEnd(queryDesc);
	}
}