// x32 PCI Bus Scanning
// Enumerate PCI devices and find VirtIO devices

#include "pci.h"
#include "platform.h"
#include "printk.h"
#include "virtio_pci.h"

// Forward declaration
void virtio_rng_setup(platform_t *platform, uint8_t bus, uint8_t slot,
                      uint8_t func);

// Scan PCI bus for VirtIO devices
void pci_scan_devices(platform_t *platform) {
  printk("Scanning PCI bus for VirtIO devices...\n");

  int devices_found = 0;

  // Scan first 4 buses only (most systems have devices on bus 0)
  // Scanning all 256 buses takes too long
  for (uint16_t bus = 0; bus < 4; bus++) {
    for (uint8_t slot = 0; slot < 32; slot++) {
      uint32_t vendor_device =
          platform_pci_config_read32(platform, bus, slot, 0, PCI_REG_VENDOR_ID);

      if (vendor_device == 0xFFFFFFFF) {
        continue; // No device
      }

      uint16_t vendor = vendor_device & 0xFFFF;
      uint16_t device = vendor_device >> 16;

      // Check for VirtIO vendor
      if (vendor == VIRTIO_PCI_VENDOR_ID) {
        if (device == VIRTIO_PCI_DEVICE_RNG_LEGACY ||
            device == VIRTIO_PCI_DEVICE_RNG_MODERN) {

          printk("Found VirtIO-RNG at PCI ");
          printk_dec(bus);
          printk(":");
          printk_dec(slot);
          printk(".0 (device ID ");
          printk_hex16(device);
          printk(")\n");

          virtio_rng_setup(platform, bus, slot, 0);
          devices_found++;
        }
      }
    }
  }

  if (devices_found == 0) {
    printk("No VirtIO devices found.\n");
  } else {
    printk("Found ");
    printk_dec(devices_found);
    printk(" VirtIO device(s).\n");
  }
}
