pg_badplan
==========

What is it?
-----------

pg_badplan is an PostgreSQL extension that logs when the planners row estimate vs actual is over a specified ratio.

This is useful when you want to find queries where the planner might be doing a bad plan becase the statistics is bad.

It works by installing execution start and end hooks that first enable row instrumentation and during end check the plans estimated vs executed actual rows. Since it enables instrumentation it will likely impose a performance penelty but how much we haven't measured.

Installation
------------

This extension has been developed using Pg 10.2 but should probably work with earlier relases as well.

Build and install by running `make install`. This will consult `pg_config` with where to install into and where the necessary headers are. Make sure your `$PATH` lists the correct `pg_config`.

Enable the module by adding `shared_preload_libraries = 'pg_badplan'` to your `postgresql.conf`. Then restart PostgreSQL.

Configuration
-------------

The following GUCs can be configured, in postgresql.conf:

- *pg_badplan.enabled* (boolean, default true): whether or not pg_badplan should be enabled
- *pg_badplan.ratio* (real, deafult 0.2): A value between 0 and 1 that sets the ratio max expected/actual or actual/expected ratio before we log. For example 0.2 means that we're 5 times off the expected and 0.1 means we're 10 times off the expected.
- *pg_badplan.min_row_threshold* (int, default 1000): Queries where expected and actual rows are below this threshold are ignored since these probablly execute so fast anyway.
- *pg_badplan.logdir* (string, default ""): A directory where we'll write the SQL for the query with the bad plan. The filename is in the form of `<backend-id>-<ms-timestamp>.sql`
- *pg_badplan.min_dump_interval_ms* (int, default 60000): The mimimum elapsed time in milleseconds since the last time we dumped a query to disk. This is useful for not flooding *pg_badplan.logdir* with
saves.

Usage
-----
Once loaded and enabled it will start investigating your queries. If `pg_badplan.logdir` is set queries will end up in a file named as described above, otherwise we'll log the ratio and the query to the postgresql log with `pg_badplan:` as prefix for easy grepping.
