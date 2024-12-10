CFLAGS=-Wall -O3 -g -Wextra -Wno-unused-parameter
CXXFLAGS=$(CFLAGS)

SRCS=led-timer-display.cc Displayer.cc MessageFormatter.cc Receiver.cc TextChangeOrder.cc
OBJECTS=$(subst .cc,.o,$(SRCS))

# Where our library resides. You mostly only need to change the
# RGB_LIB_DISTRIBUTION, this is where the library is checked out.
RGB_LIB_DISTRIBUTION=..
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a
LDFLAGS+=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread

all : led-timer-display

$(RGB_LIBRARY): FORCE
	$(MAKE) -C $(RGB_LIBDIR)

%.o : %.cc
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) -c $< -o $@

led-timer-display : $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)

led-timer-display.o : led-timer-display.cc

Displayer.o: Displayer.cc Displayer.h

MessageFormatter.o: MessageFormatter.cc MessageFormatter.h

Receiver.o: Receiver.cc Receiver.h

TextChangeOrder.o: TextChangeOrder.cc TextChangeOrder.h

clean:
	rm -f $(OBJECTS) $(BINARIES)

FORCE:
.PHONY: FORCE
