# EDU Factorial PCI Driver
 
A Linux kernel module implementing a PCI character device driver for the QEMU EDU device's factorial computation feature.
 
## Overview
 
This project develops a functional PCI driver that enables user-space applications to compute factorials through a virtual PCI device via a character device interface. The driver communicates with the QEMU EDU device (Vendor ID: 0x1234, Device ID: 0x11e8) through memory-mapped I/O.
 
**QEMU EDU Specification:** https://www.qemu.org/docs/master/specs/edu.html
 
## Implementation
 
The driver handles PCI device discovery and registration through the Linux PCI subsystem, creating a character device at `/dev/edu-fact0` for each detected EDU device. Communication occurs via memory-mapped I/O regions.
 
Device operations:
- Write: Send a number to compute its factorial
- Read: Retrieve the computed result
The driver supports both minimal implementation (polling-based factorial computation) and full implementation (interrupt-driven completion notification with multiple device support).
 
## References
 
- [QEMU EDU Device Specification](https://www.qemu.org/docs/master/specs/edu.html)
- [Linux Kernel Documentation](https://docs.kernel.org)
- [Linux Kernel Source](https://elixir.bootlin.com/linux/v6.12.6/source)
- [Linux Device Drivers (LDD3)](https://lwn.net/Kernel/LDD3)
## License
 
Portfolio Display License v1.0. See LICENSE.md for details.
