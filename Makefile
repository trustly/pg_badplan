EXTENSION    = pg_badplan
EXTVERSION   = $(shell grep default_version $(EXTENSION).control | sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")
TESTS        = $(wildcard test/sql/*.sql)
REGRESS      = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test
MODULES      = $(patsubst %.c,%,$(wildcard *.c))
PG_CONFIG    ?= pg_config
PG_CPPFLAGS  = -DEXTNAME=\"$(EXTENSION)\"

all:

release-zip: all
	git archive --format zip --prefix=$(EXTENSION)-$(EXTVERSION)/ --output ./$(EXTENSION)-$(EXTVERSION).zip HEAD
	unzip ./$(EXTENSION)-$(EXTVERSION).zip
	rm ./$(EXTENSION)-$(EXTVERSION).zip
	sed -i -e "s/__VERSION__/$(EXTVERSION)/g"  ./$(EXTENSION)-$(EXTVERSION)/META.json
	zip -r ./$(EXTENSION)-$(EXTVERSION).zip ./$(EXTENSION)-$(EXTVERSION)/
	rm ./$(EXTENSION)-$(EXTVERSION) -rf

DATA = $(wildcard *--*.sql)
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)