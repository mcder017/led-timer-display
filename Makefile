# This toplevel Makefile compiles the library in the lib subdirectory
# and the led-timer-display program.
RGB_LIBDIR=./lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a

DISPLAYDIR=./display
DISPLAYPROG=$(DISPLAYDIR)/led-timer-display

all : $(RGB_LIBRARY) $(DISPLAYPROG)

$(RGB_LIBRARY): FORCE
	$(MAKE) -C $(RGB_LIBDIR)

$(DISPLAYPROG) : FORCE
	$(MAKE) -C $(DISPLAYDIR)

clean:
	$(MAKE) -C $(RGB_LIBDIR) clean
	$(MAKE) -C $(DISPLAYDIR) clean

FORCE:
.PHONY: FORCE
