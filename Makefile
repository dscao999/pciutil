.PHONY: clean all

all: pcipath

pcipath: pci_setcap.o
	$(LINK.o) $^ -lpci -o $@

clean:
	rm -f *.o pcipath
