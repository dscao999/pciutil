#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include "pci.h"

#define CAP_OFFSET	0x34

static const int payload[] = {128, 256, 512, 1024, 2048, 4096};
static int pciset = 0;

struct pcidev_id {
	int domain, busno, devno, func;
};
static inline void pcidev_busid_read(struct pcidev_id *devid, const char *str)
{
	sscanf(str, "%x:%x.%x", (unsigned int *)&devid->busno,
		(unsigned int *)&devid->devno, (unsigned int *)&devid->func);
	devid->domain = 0;
}

static void pci_fillbus(struct device *cdev, struct domain **domain)
{
	struct domain *cdm, *p_cdm;
	struct pcibus *bus;
	int busno;

	p_cdm = NULL;
	cdm = *domain;
	while (cdm) {
		if (cdm->num == cdev->pdev->domain)
			break;
		p_cdm = cdm;
		cdm = cdm->next;
	}
	if (!cdm) {
		cdm = malloc(sizeof(*cdm));
		memset(cdm, 0, sizeof(*cdm));
		cdm->num = cdev->pdev->domain;
		if (p_cdm)
			p_cdm->next = cdm;
		else
			*domain = cdm;
	}
	busno = cdev->pdev->bus;
	bus = cdm->buses + busno;
	cdev->next = bus->device;
	if (bus->device)
		bus->device->prev = cdev;
	bus->device = cdev;
}

static void pci_filltree(struct domain *domain)
{
	struct domain *cdm;
	int i, j, found;
	struct pcibus *mbus, *cbus;
	struct device *cdev;

	cdm = domain;
	while (cdm) {
		for (i = 0; i < MAX_BUSES; i++) {
			mbus = cdm->buses + i;
			if (mbus->device == NULL)
				continue;
			found = 0;
			for (j = 0; j < MAX_BUSES && !found; j++) {
				cbus = cdm->buses + j;
				if (cbus->device == NULL)
					continue;
				cdev = cbus->device;
				do {
					if (cdev->sub_bus == i) {
						mbus->up_agent = cdev;
						found = 1;
						break;
					}
					cdev = cdev->next;
				} while(cdev);
			}
		}
		cdm = cdm->next;
	}
}

static int pci_scan(struct pci_access *access, struct domain **dm)
{
	struct pci_dev *pdev;
	struct device *cdev;
	unsigned int noffset, cv;
	int numdevs;

	numdevs = 0;
	pdev = access->devices;
	while (pdev) {
		noffset = pci_read_byte(pdev, CAP_OFFSET);
		while (noffset != 0) {
			cv = pci_read_long(pdev, noffset);
			if ((cv & 0xff) == 0x10)
				break;
			noffset = (cv >> 8) & 0x0ff;
		}
		if (noffset == 0) {
			pdev = pdev->next;
			continue;
		}

		cdev = malloc(sizeof(*cdev));
		cdev->pdev = pdev;
		cdev->prev = NULL;
		cdev->next = NULL;
		cdev->cap_offset = noffset;
		cdev->devcap_id = cv;
		cdev->devcap = pci_read_long(pdev, noffset+4);
		cdev->devctl = pci_read_long(pdev, noffset+8);
		cdev->type = (cv >> 20) & 0x0f;
		if (cdev->type <= 8 && cdev->type >= 4) {
			cv = pci_read_long(pdev, 0x18);
			cdev->sub_bus = (cv >> 8) & 0x0ff;
			assert((cv & 0x0ff) == pdev->bus);
		} else {
			cdev->sub_bus = -1;
		}
		numdevs++;

		pci_fillbus(cdev, dm);

		pdev = pdev->next;
	}
	return numdevs;
}

static void print_path(struct domain *domain, struct pcidev_id *devids,
		int numpcis)
{
	int i, busno, setp;
	struct pcidev_id *id;
	struct pcibus *bus, *ubus;
	struct device *cdev, *udev;
	u32 devctl;

	for (i = 0, id = devids; i < numpcis; i++, id++) {
		busno = id->busno;
		bus = domain->buses + busno;
		cdev = bus->device;
		while (cdev) {
			if (cdev->pdev->dev != id->devno ||
					cdev->pdev->func != id->func) {
				cdev = cdev->next;
				continue;
			}
			ubus = bus;
			udev = cdev;
			setp = 0;
			do {
				printf("%04x::%02x:%02x.%01x", domain->num,
						udev->pdev->bus,
						udev->pdev->dev,
						udev->pdev->func);
				printf(" MaxPayload: %d set to %d",
						payload[udev->devcap & 0x07],
						payload[(udev->devctl>>5) & 0x07]);
				if (pciset && !setp) {
					devctl = (udev->devctl & 0xffffff1fu) | (0 << 5);
					setp = 1;
					pci_write_long(udev->pdev, udev->cap_offset+8, devctl);
				}
				udev = ubus->up_agent;
				if (udev) {
					ubus = domain->buses + udev->pdev->bus;
					printf("<---");
				}
			} while (udev);
			printf("\n");
			cdev = cdev->next;
		}
	}
}

int main(int argc, char *argv[])
{
	int retv = 0, i, numpcis, fin, opt;
	struct pci_access *access;
	struct pcidev_id *devids, *devid;
	extern int optind, opterr, optopt;
	struct domain *domain = 0;

	opterr = 0;
	fin = 0;
	do {
		opt = getopt(argc, argv, ":s");
		switch(opt) {
		case -1:
			fin = 1;
			break;
		case '?':
			fprintf(stderr, "Unknown option: %c\n", optopt);
			break;
		case ':':
			fprintf(stderr, "Missiong option argument for %c\n", optopt);
			break;
		case 's':
			pciset = 1;
			break;
		default:
			assert(0);
		}
	} while (fin == 0);
	
	numpcis = argc - optind;
	devids = NULL;
	if (numpcis > 0) {
		devids = malloc(numpcis*sizeof(*devids));
		if (!devids) {
			fprintf(stderr, "Out of Memory!\n");
			return 10000;
		}
	}
		
	numpcis = 0;
	for (devid = devids, i = optind; i < argc; i++) {
		devid->busno = -1;
		devid->devno = -1;
		devid->func = -1;
		pcidev_busid_read(devid, argv[i]);
		if (devid->busno != -1 && devid->devno != -1 &&
				devid->func != -1) {
			numpcis++;
			devid++;
		}
	}

	access = pci_alloc();
	if (!access) {
		retv = 10000;
		fprintf(stderr, "Out of Memory!\n");
		goto exit_00;
	}
	pci_init(access);
	pci_scan_bus(access);

	pci_scan(access, &domain);
	pci_filltree(domain);

	print_path(domain, devids, numpcis);

	pci_cleanup(access);

	free(devids);
exit_00:
	return retv;
}
