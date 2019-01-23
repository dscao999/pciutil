#ifndef PCIDEV_DSCAO__
#include <pci/pci.h>

#define MAX_BUSES	256

enum PORTTYPE {
	ENDPOINT = 0, L_ENDPOINT = 1, ROOTPORT = 4, SWUP = 5, SWDN = 6,
	SWPCI = 7, PCISW = 8, ROOTEND = 9, ROOTEC = 10
};

struct device {
	const struct pci_dev *pdev;
	struct device *prev, *next; /* a bunch of siblings */
	unsigned int devcap_id, devcap, devctl;
	unsigned int cap_offset;
	enum PORTTYPE type; 
	int sub_bus;
};

struct pcibus {
	struct device *up_agent;
	struct device *device;
};

struct domain {
	int num;
	struct domain *next;
	struct pcibus buses[MAX_BUSES];
};
#endif /* PCIDEV_DSCAO__ */
