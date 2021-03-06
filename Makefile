# To build sninit, check configuration variables in config.mk and run "make".
# Alternatively, you can use ./configure, which will set those variables
# to some pre-defined values.

# Do not rely on configure too much though, it is only expected to handle
# standard situations. In case you need some unusual setup (weirdly configured
# dietlibc etc), ditch configure and edit config.h/config.mk/Makefile directly.

include config.mk

all: init telinit run init.8 telinit.8 inittab.5 initdir.5 run.8 $(extra)

# Force early libc build
init telinit run: $(if $(ARCH),libc.a)

# Init may be built using different subsets of code blocks.
# Blocks are selected by keywords from $(initblocks); see config.mk on this.

init_conf = init_conf.o init_conf_map.o init_conf_arg.o init_conf_tab.o \
	init_conf_dir.o init_conf_rec.o init_conf_key.o
init_null = init_null.o
init_err = sys_err_init.o
init_sys = sys_printf.o sys_err_init.o
init_dents = sys_getdents.o
init_ppoll = sys_ppoll.o

init: init.o init_pass.o init_proc.o init_ctrl.o init_dump.o \
	init_wait.o init_warn.o init_cmds.o init_find.o \
	$(sort $(foreach b,$(initblocks),$(init_$(b))))

telinit_err = sys_err_telinit.o
telinit_sys = sys_err_telinit.o

telinit: telinit.o \
	$(sort $(foreach b,$(initblocks),$(telinit_$(b))))

run_err = sys_err_run.o
run_execvp = sys_execvp.o
run_sys = sys_err_run.o sys_execvp.o
run_unshare = sys_unshare.o

run: run.o \
	$(sort $(foreach b,$(initblocks),$(run_$(b))))

install: install-bin install-man
install-stripped: install-bin-stripped install-man

install-bin:
	install -m 0755 -D init $(DESTDIR)$(sbindir)/$sinit
	install -m 0755 -D telinit $(DESTDIR)$(sbindir)/$stelinit
	install -m 0755 -D run $(DESTDIR)$(sbindir)/$srun

install-man:
	install -m 0644 -D init.8 $(DESTDIR)$(man8dir)/$sinit.8
	install -m 0644 -D telinit.8 $(DESTDIR)$(man8dir)/$stelinit.8
	install -m 0644 -D inittab.5 $(DESTDIR)$(man5dir)/$sinittab.5
	install -m 0644 -D initdir.5 $(DESTDIR)$(man5dir)/$sinitdir.5

install-bin-stripped: install-bin
	$(STRIP) $(DESTDIR)$(sbindir)/$sinit
	$(STRIP) $(DESTDIR)$(sbindir)/$stelinit
	$(STRIP) $(DESTDIR)$(sbindir)/$srun

clean: clean-temp

distclean: clean clean-inst

clean-temp:
	rm -f *.o *.ho *.d builtin.c

clean-inst:
	rm -f init telinit statictab run *.[58]

# --- Built-in inittab ---------------------------------------------------------
#
# This differs significantly from the rest of the file
# because statictab must be host-executable, not target-executable
# like init and telinit. So HOSTCC must be used here, together with some libc.
# Since libc choice is hardly relevant for a build-time tool,
# gcc/glibc is assumed, unless we're doing a native build in which case
# primary objects and $LIBS are used.
#
# Non-standard but short .ho extension is used for host objects.
# This is ok, since gcc treats any unknown extension as an object
# to be passed to linker.
#
# And yes, this whole section is pretty ugly.

statictab := statictab.o init_conf.o init_conf_map.o init_conf_mem.o\
	init_conf_tab.o init_conf_dir.o init_conf_rec.o init_conf_key.o\
	init_find.o

ifndef HOSTCC

statictab: $(statictab) $(if $(ARCH),libc.a)

else

statictab += sys_getdents.o

statictab: $(statictab:.o=.ho)
	$(HOSTCC) -o $@ $(filter %.ho,$^)

%.ho: %.c
	$(HOSTCC) -c -o $@ $<

endif

ifdef builtin
init: builtin.o
builtin.c: $(builtin) | statictab
	./statictab $< > $@
endif

# --- Bundled libc -------------------------------------------------------------
#
# This section is even more ugly. Here we build libc.a from the files in libc/,
# among which there may be alternatives like libc/(ARCH)/foo.s and libc/foo.c
#
# The natural way to resolve alternatives in make is to provide alternative rules
# for the same target. This fails badly for archives however; ar does not lock
# files, does not tolerate flock $@ ar c $@ ... approach, and make provides
# no way for breadth-first building (i.e. all objects, then single ar cru $@).
# https://www.gnu.org/software/make/manual/html_node/Archive-Pitfalls.html
#
# So instead the idea is to form an explicit list of objects to build,
# and use make functions to resolve alternatives.

libc: libc.a

ifneq ($(ARCH),)

# The smell of lisp is strong here
libf := $(sort $(basename $(notdir\
		$(wildcard libc/*.[cs])\
		$(wildcard libc/$(ARCH)/*.[cs])\
		$(wildcard libc/libtest/*.[cs]) )))
libs := $(foreach s, $(libf), $(basename $(firstword\
		$(wildcard libc/$s.[cs])\
		$(wildcard libc/$(ARCH)/$s.[cs])\
		$(wildcard libc/libtest/$s.[cs]) )))
libc := $(patsubst %,%.o,$(libs))
# Yes this can be done with a single variable. No I won't do that.

# libf = _start strlen write printf ...
# libs = libc/x86/_start.s libc/strlen.c libc/x86/write.s libc/libtest/printf.c ...
# libc = libc/x86/_start.o libc/strlen.o libc/x86/write.o libc/libtest/printf.o ...

# LTO objects do not work when packed in an .a library,
# at least not without some additional effort.
# See https://gcc.gnu.org/wiki/LinkTimeOptimization
libc/$(ARCH)/%.o libc/libtest/%.o libc/%.o: CFLAGS := $(filter-out -flto, $(CFLAGS))

# Pack only objects newer that libc.a.
#
# It would make sense to declare $(libc) intermediate targets, and it would almost work,
# except that a single changed source file would trigger re-building of all the $(libc).
# So instead let's keep the objects around.
#
libc.a: $(libc)
	$(AR) cr $@ $?

clean: clean-libc

clean-libc:
	rm -f libc.a libc/*.[od] libc/*/*.[od]

else

libc.a:
	@echo Set ARCH in config.mk to build bundled libc; false

endif

# --- Subdir entry points ------------------------------------------------------
#
# This is only for convenience, so that "make test" would work from the top dir.

.PHONY: test

test: $(if $(ARCH),libc.a)
	$(MAKE) -C test run

# entering/exiting messages for this get really annoying, and there's only two
# targets there, with no dependecies, so let's list them here explicitly.
sbin = sbin/trap sbin/slogdg
sbin: $(sbin)
$(sbin): ; $(MAKE) -C sbin $(notdir $(sbin))

clean: clean-test
distclean: clean-sbin	# does not depend on configuration

clean-test:
	$(MAKE) -C test clean

clean-sbin:
	$(MAKE) -C sbin clean

# --- Implicit rules -----------------------------------------------------------

%.5 %.8: %.man mansubst
	./mansubst $< > $@ 

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

.s.o:
	$(AS) $(ASFLAGS) -o $@ $<

%: %.o
	$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LIBS)


# Include dependencies, if any
-include *.d libc/*.d libc/*/*.d
