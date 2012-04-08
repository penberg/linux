WARNINGS := -Wcast-align
WARNINGS += -Wformat
WARNINGS += -Wformat-security
WARNINGS += -Wformat-y2k
WARNINGS += -Wshadow
WARNINGS += -Winit-self
WARNINGS += -Wpacked
WARNINGS += -Wredundant-decls
WARNINGS += -Wstrict-aliasing=3
WARNINGS += -Wswitch-default
WARNINGS += -Wno-system-headers
WARNINGS += -Wundef
WARNINGS += -Wwrite-strings
WARNINGS += -Wbad-function-cast
WARNINGS += -Wmissing-declarations
WARNINGS += -Wmissing-prototypes
WARNINGS += -Wnested-externs
WARNINGS += -Wold-style-definition
WARNINGS += -Wstrict-prototypes
WARNINGS += -Wdeclaration-after-statement

CFLAGS  = -O3 -g -std=gnu99 $(WARNINGS)

PROGRAMS = vmevent-test

all: $(PROGRAMS)

clean:
	rm -f $(PROGRAMS) *.o
.PHONY: clean
