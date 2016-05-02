ECHO	= echo
AR	= ar
CC	= gcc
WORD    = 32
CFLAGS	= -Wall -Wextra -g -pedantic -fPIC -std=c99 -O2 -DWORD=${WORD}
TARGET	= forth

.PHONY: all clean doxygen

all: shorthelp $(TARGET) lib$(TARGET).so

shorthelp:
	@$(ECHO) "Use 'make help' for a list of all options"
help:
	@$(ECHO) ""
	@$(ECHO) "project:      lib$(TARGET)"
	@$(ECHO) "description: A small $(TARGET) interpreter and library"
	@$(ECHO) ""
	@$(ECHO) "make (option)*"
	@$(ECHO) ""
	@$(ECHO) "      all             create the $(TARGET) libraries and executables"
	@$(ECHO) "      $(TARGET)           create the $(TARGET) executable"
	@$(ECHO) "      unit            create the unit test executable"
	@$(ECHO) "      test            execute the unit tests"
	@$(ECHO) "      doc             make the project documentation"
	@$(ECHO) "      lib$(TARGET).so     make a shared $(TARGET) library"
	@$(ECHO) "      lib$(TARGET).a      make a static $(TARGET) library"
	@$(ECHO) "      clean           remove generated files"
	@$(ECHO) "      install         (TODO) install the project"
	@$(ECHO) "      uninstall       (TODO) uninstall the project"
	@$(ECHO) "      dist            (TODO) create a distribution archive"
	@$(ECHO) ""
	@$(ECHO) "compile time options:"
	@$(ECHO) ""
	@$(ECHO) "      WORD            set the virtual machine word size,"
	@$(ECHO) "                      valid sizes are 16, 32 and 64" 
	@$(ECHO) ""

doc: lib$(TARGET).htm doxygen
lib$(TARGET).htm: lib$(TARGET).md
	markdown $^ > $@
doxygen:
	doxygen doxygen.conf

lib$(TARGET).a: lib$(TARGET).o
	$(AR) rcs $@ $<

lib$(TARGET).so: lib$(TARGET).o lib$(TARGET).h
	$(CC) $(CFLAGS) -shared $< -o $@

unit: unit.o lib$(TARGET).a

$(TARGET): main.c lib$(TARGET).a
	$(CC) $(CFLAGS) $^ -o $@

run: $(TARGET)
	./$^ 
test: unit
	./$^

clean:
	rm -rf $(TARGET) unit *.blk *.core *.a *.so *.o *.log *.htm doxygen html latex *.db

