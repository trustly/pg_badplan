EXTENSION    = pg_planwayoff
EXTVERSION   = $(shell grep default_version $(EXTENSION).control | sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")
TESTS        = $(wildcard test/sql/*.sql)
REGRESS      = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test
MODULES      = $(patsubst %.c,%,$(wildcard *.c))
PG_CONFIG    ?= pg_config

all:

release-zip: all
	git archive --format zip --prefix=pg_planwayoff-$(EXTVERSION)/ --output ./pg_planwayoff-$(EXTVERSION).zip HEAD
	unzip ./pg_planwayoff-$(EXTVERSION).zip
	rm ./pg_planwayoff-$(EXTVERSION).zip
	sed -i -e "s/__VERSION__/$(EXTVERSION)/g"  ./pg_planwayoff-$(EXTVERSION)/META.json
	zip -r ./pg_planwayoff-$(EXTVERSION).zip ./pg_planwayoff-$(EXTVERSION)/
	rm ./pg_planwayoff-$(EXTVERSION) -rf


DATA = $(wildcard *--*.sql)
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)