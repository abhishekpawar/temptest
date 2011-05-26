#include "../comedidev.h"
#include "comedi_pci.h"

#define PCI_VENDOR_ID_DYNALOG           0x10b5
#define PCI_DEVICE_ID_DYNALOG_PCI_1050  0x1050
#define DRV_NAME                        "dyna_pci1050"

#define PCI1050_AREAD	 	0	/* ANALOG READ */
#define PCI1050_AWRITE	 	0	/* ANALOG WRITE */
#define PCI1050_ACONTROL	2	/* ANALOG CONTROL */

static DEFINE_PCI_DEVICE_TABLE(dyna_pci1050_pci_table) = {
	{PCI_VENDOR_ID_DYNALOG, PCI_DEVICE_ID_DYNALOG_PCI_1050, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0}
};

MODULE_DEVICE_TABLE(pci, dyna_pci1050_pci_table);

static int dyna_pci1050_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it);
static int dyna_pci1050_detach(struct comedi_device *dev);

static const struct comedi_lrange range_pci1050_ai = { 3, {
							  BIP_RANGE(10),
							  BIP_RANGE(5),
							  UNI_RANGE(10)
							  }
};

struct boardtype {
	const char *name;
	int device_id;
	int ai_chans;
	int ai_bits;
};

static const struct boardtype boardtypes[] = {
	{
	.name = "dyna_pci1050",
	.device_id = PCI_DEVICE_ID_DYNALOG_PCI_1050,
	.ai_chans = 16,
	.ai_bits = 12,
	},
};

static struct comedi_driver dyna_pci1050_driver = {
	.driver_name = DRV_NAME,
	.module = THIS_MODULE,
	.attach = dyna_pci1050_attach,
	.detach = dyna_pci1050_detach,
	.board_name = &boardtypes[0].name,
	.offset = sizeof(struct boardtype),
	.num_names = ARRAY_SIZE(boardtypes),
};

struct dyna_pci1050_private {
	struct pci_dev *pci_dev;	/*  ptr to PCI device */
	char valid;			/*  card is usable */
	int data;
	unsigned int ai_n_chan;
	unsigned int *ai_chanlist;
	unsigned int ai_flags;
	unsigned int ai_data_len;
	short *ai_data;

	/* device information */
	unsigned long BADR0, BADR1, BADR2;
	unsigned long BADR0_SIZE, BADR1_SIZE, BADR2_SIZE;
};

#define thisboard ((const struct boardtype *)dev->board_ptr)
#define devpriv ((struct dyna_pci1050_private *)dev->private)


/******************************************************************************/
/*********************** INITIALIZATION FUNCTIONS *****************************/
/******************************************************************************/

static int dyna_pci1050_ai_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	int n;
	u16 d;
	unsigned int chan;

	printk(KERN_INFO "comedi: dyna_pci1050: %s\n", __func__);

	/* get the channel number */
	chan = CR_CHAN(insn->chanspec);

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		mb(); smp_mb(); udelay(100);
		outw_p(0x0030 + chan, devpriv->BADR2 + 2);
		mb(); smp_mb(); udelay(100);
		/* read data */
		d = inw_p(devpriv->BADR2);
		d &= 0x0FFF;
		data[n] = d;
	}

	/* return the number of samples read/written */
	return n;
}

static int dyna_pci1050_ns_to_timer(unsigned int *ns, int round)
{
	printk(KERN_INFO "comedi: dyna_pci1050: %s\n", __func__);

	return *ns;
}

static int dyna_pci1050_ai_cmdtest(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

	/* cmdtest tests a particular command to see if it is valid.
	 * Using the cmdtest ioctl, a user can create a valid cmd
	 * and then have it executes by the cmd ioctl.
	 *
	 * cmdtest returns 1,2,3,4 or 0, depending on which tests
	 * the command passes. */

	/* step 1: make sure trigger sources are trivially valid */

	printk(KERN_INFO "comedi: dyna_pci1050: %s\n", __func__);

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_TIMER | TRIG_EXT;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER | TRIG_EXT;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique and mutually compatible
     */

	/* note that mutual compatibility is not an issue here */
	if (cmd->scan_begin_src != TRIG_TIMER &&
	    cmd->scan_begin_src != TRIG_EXT)
		err++;
	if (cmd->convert_src != TRIG_TIMER && cmd->convert_src != TRIG_EXT)
		err++;
	if (cmd->stop_src != TRIG_COUNT && cmd->stop_src != TRIG_NONE)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
#define MAX_SPEED	10000	/* in nanoseconds */
#define MIN_SPEED	1000000000	/* in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->scan_begin_arg < MAX_SPEED) {
			cmd->scan_begin_arg = MAX_SPEED;
			err++;
		}
		if (cmd->scan_begin_arg > MIN_SPEED) {
			cmd->scan_begin_arg = MIN_SPEED;
			err++;
		}
	} else {
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		/* should specify multiple external triggers */
		if (cmd->scan_begin_arg > 9) {
			cmd->scan_begin_arg = 9;
			err++;
		}
	}
	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < MAX_SPEED) {
			cmd->convert_arg = MAX_SPEED;
			err++;
		}
		if (cmd->convert_arg > MIN_SPEED) {
			cmd->convert_arg = MIN_SPEED;
			err++;
		}
	} else {
		/* external trigger */
		/* see above */
		if (cmd->convert_arg > 9) {
			cmd->convert_arg = 9;
			err++;
		}
	}

	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		if (cmd->stop_arg > 0x00ffffff) {
			cmd->stop_arg = 0x00ffffff;
			err++;
		}
	} else {
		/* TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		dyna_pci1050_ns_to_timer(&cmd->scan_begin_arg,
				 cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->scan_begin_arg)
			err++;
	}
	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		dyna_pci1050_ns_to_timer(&cmd->convert_arg,
				 cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			err++;
		if (cmd->scan_begin_src == TRIG_TIMER &&
		    cmd->scan_begin_arg <
		    cmd->convert_arg * cmd->scan_end_arg) {
			cmd->scan_begin_arg =
			    cmd->convert_arg * cmd->scan_end_arg;
			err++;
		}
	}

	if (err)
		return 4;

	return 0;
}

static int dyna_pci1050_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	printk(KERN_INFO "comedi: dyna_pci1050: %s\n", __func__);

	return -1;
}

/******************************************************************************/
/*********************** INITIALIZATION FUNCTIONS *****************************/
/******************************************************************************/

static int dyna_pci1050_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	struct pci_dev *pcidev;

	printk(KERN_INFO "comedi: dyna_pci1050: %s\n", __func__);

	printk(KERN_INFO "comedi: dyna_pci1050: minor number %d\n", dev->minor);

	dev->board_name = thisboard->name;
	dev->irq = 0;

	if (alloc_private(dev, sizeof(struct dyna_pci1050_private)) < 0)
		return -ENOMEM;

	/*
	 * Probe the device to determine what device in the series it is.
	 */

	for (pcidev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, NULL);
	     pcidev != NULL;
	     pcidev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pcidev)) {

		if (pcidev->vendor != PCI_VENDOR_ID_DYNALOG)
			continue;

		if (pcidev->device != PCI_DEVICE_ID_DYNALOG_PCI_1050)
			continue;

		goto found;
	}
	printk("comedi: dyna_pci1050: no supported device found!\n");
	return -EIO;

found:
	printk("comedi: dyna_pci1050: dynalog device found\n");

	/* initialize device */
	devpriv->BADR0 = pci_resource_start(pcidev, 0);
	devpriv->BADR1 = pci_resource_start(pcidev, 1);
	devpriv->BADR2 = pci_resource_start(pcidev, 2);
	devpriv->BADR0_SIZE = pci_resource_len(pcidev, 0);
	devpriv->BADR1_SIZE = pci_resource_len(pcidev, 1);
	devpriv->BADR2_SIZE = pci_resource_len(pcidev, 2);
	printk(KERN_INFO "comedi: dyna_pci1050: iobase 0x%4lx : 0x%4lx : 0x%4lx : %lu, %lu, %lu\n",
               devpriv->BADR0, devpriv->BADR1, devpriv->BADR2, devpriv->BADR0_SIZE, devpriv->BADR1_SIZE, devpriv->BADR2_SIZE);

	if (alloc_subdevices(dev, 1) < 0)
		return -ENOMEM;

	s = dev->subdevices + 0;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = thisboard->ai_chans;
	s->maxdata = (1 << thisboard->ai_bits) - 1;
	s->range_table = &range_pci1050_ai;
	s->len_chanlist = 16;
	s->insn_read = dyna_pci1050_ai_rinsn;
	s->subdev_flags |= SDF_CMD_READ;
	s->do_cmd = dyna_pci1050_ai_cmd;
	s->do_cmdtest = dyna_pci1050_ai_cmdtest;

	printk(KERN_INFO "comedi: dyna_pci1050: attached\n");

	return 1;
}

static int dyna_pci1050_detach(struct comedi_device *dev)
{
	printk(KERN_INFO "comedi: dyna_pci1050: %s\n", __func__);

	return 0;
}

COMEDI_PCI_INITCLEANUP(dyna_pci1050_driver, dyna_pci1050_pci_table);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Prashant Shah");

