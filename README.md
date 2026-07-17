# Key Value Storage Library - Persistent Key-Value Storage for Non-Volatile Memory

## Overview

This library provides a convenient way to store information as key-value pairs in non-volatile
storage. It targets embedded systems using internal flash (NVM) or external serial memory and is
designed to be resilient to power failures and to promote even wear of the underlying storage.

## Features

* Supports any storage modeled as a block device, including internal flash or external flash
  (e.g., via QSPI).
* Allows partitioning storage by instantiating multiple independent library instances.
* Designed to be resilient to power failures.
* Designed to promote even wear of the storage.
* Optional thread-safe operation in RTOS environments.

## When to Use

Use this library when your embedded application needs to:

* Persist configuration settings, calibration data, or device state across power cycles.
* Store data in internal NVM or external serial memory with a simple key-based lookup.
* Handle reliable storage with automatic wear leveling and power-failure resilience.
* Manage multiple independent key-value stores within the same application.

This library is not intended for large data storage or file system use cases. It is optimized for
small-to-moderate values where resilience and simplicity are the priorities.

## How to Use

The library works with any block-device-backed storage via the
[block-storage](https://github.com/infineon/block-storage) interface. Choose the usage model that
matches your hardware and follow only that path end-to-end.

---

### Internal Flash (HAL NVM)

**1. Include the header**

```c
#include "mtb_kvstore.h"
```

**2. Initialize the HAL NVM driver**

*HAL API 3.0:*
```c
mtb_hal_nvm_t nvm_obj;
mtb_hal_nvm_region_info_t block_info;
cy_rslt_t result = mtb_hal_nvm_setup(&nvm_obj, NULL);
CY_ASSERT(result == CY_RSLT_SUCCESS);
mtb_hal_nvm_info_t nvm_info;
mtb_hal_nvm_get_info(&nvm_obj, &nvm_info);
/* Select the flash region by its explicit index.
 * Do NOT use regions[region_count - 1]: on some devices the HAL unconditionally
 * appends extra regions (e.g. a dual-bank region on a single-bank device) whose
 * start_address may be 0. Passing that address to mtb_kvstore_init will cause a
 * HardFault when the library reads the storage during initialization. */
block_info = nvm_info.regions[0]; /* main flash region */
```

*HAL API 2.0:*
```c
cyhal_nvm_t nvm_obj;
cyhal_nvm_region_info_t block_info;
cy_rslt_t result = cyhal_nvm_init(&nvm_obj);
CY_ASSERT(result == CY_RSLT_SUCCESS);
cyhal_nvm_info_t nvm_info;
cyhal_nvm_get_info(&nvm_obj, &nvm_info);
/* Select the flash region by its explicit index.
 * Do NOT use regions[region_count - 1] — see note above. */
block_info = nvm_info.regions[0]; /* main flash region */
```

**3. Create the block storage interface**

*HAL 3.0:*
```c
mtb_block_storage_t nvm_bsd;
mtb_block_storage_create_hal_nvm(&nvm_bsd, &nvm_obj);
```

*HAL 2.0:*
```c
mtb_block_storage_t nvm_bsd;
mtb_block_storage_nvm_create(&nvm_bsd);
```

**4. Set the storage region and initialize**

The region must be aligned to erase sector boundaries and contain an even number of erase sectors.
Only half the provided space is available for key-value data (the other half is the swap area).
This example places kv-store in the last 16 pages of the selected region:

```c
uint32_t length     = 16U * block_info.block_size;
uint32_t start_addr = block_info.start_address + block_info.size - length;

mtb_kvstore_t obj;
result = mtb_kvstore_init(&obj, start_addr, length, &nvm_bsd, NULL, NULL);
CY_ASSERT(result == CY_RSLT_SUCCESS);
```

> **NVM-specific pitfalls**
>
> - **Region selection:** Always select the flash region by its explicit index. Never use
>   `regions[region_count - 1]` blindly — on some devices the HAL reports extra regions whose
>   `start_address` is `0`, which causes a HardFault inside `mtb_kvstore_init`.
> - **Linker script:** The flash range allocated to kv-store must be excluded from the application
>   image. Verify that the BSP linker script leaves a matching gap at the top of the flash region
>   so application code is never placed there.
> - **Non-overlapping placement:** Ensure the kv-store region does not overlap the application
>   image, especially when using main flash rather than a dedicated data-flash partition.

---

### External Serial Memory

> **Prerequisite:** This model requires the [serial-memory](https://github.com/infineon/serial-memory)
> library. Add it to your project via the ModusToolbox Library Manager, or add the dependency
> manually to your `deps/` folder before building.

**1. Include the headers**

```c
#include "mtb_kvstore.h"
#include "mtb_serial_memory.h"
```

**2. Initialize the serial memory driver**

```c
cy_rslt_t result;

#if defined(MTB_SERIAL_MEMORY_VERSION_MAJOR) && (MTB_SERIAL_MEMORY_VERSION_MAJOR == 3)
    /* Serial memory version 3 */
    mtb_serial_memory_t serial_memory_obj;
    cy_stc_smif_mem_context_t mem_context;
    cy_stc_smif_mem_info_t mem_info;

    /* base_address, sm_clock and smif_block_obj are defined by the user or
       automatically generated by the ModusToolbox configurators */
    result = mtb_serial_memory_setup(
        &serial_memory_obj, MTB_SERIAL_MEMORY_CHIP_SELECT_0,
        &base_address, &sm_clock, &mem_context, &mem_info, &smif_block_obj);
#else
    /* Serial memory version 2 */
    mtb_serial_memory_t serial_memory_obj;
    cy_stc_smif_context_t context;

    result = mtb_serial_memory_setup(
        &serial_memory_obj, MTB_SERIAL_MEMORY_CHIP_SELECT_0,
        &base_address, &sm_clock, &context, &smif_block_obj);
#endif
```

**3. Create the block storage interface**

```c
mtb_block_storage_t serial_memory_bsd;
result = mtb_block_storage_create_serial_memory(&serial_memory_bsd, &serial_memory_obj);
CY_ASSERT(result == CY_RSLT_SUCCESS);
```

**4. Set the storage region and initialize**

This example allocates 2 erase sectors starting at address 0:

```c
uint32_t start_addr  = 0;
uint32_t sector_size = serial_memory_bsd.get_erase_size(serial_memory_bsd.context, start_addr);
uint32_t length      = sector_size * 2;

mtb_kvstore_t obj;
result = mtb_kvstore_init(&obj, start_addr, length, &serial_memory_bsd, NULL, NULL);
CY_ASSERT(result == CY_RSLT_SUCCESS);
```

---

### Common: Perform Operations

Once initialized, the API is identical regardless of the storage model used.

**Write:**
```c
uint8_t data[10];
result = mtb_kvstore_write(&obj, "hello", data, sizeof(data));
CY_ASSERT(result == CY_RSLT_SUCCESS);
```

**Read:**
```c
uint8_t read_data[10];
uint32_t data_size = sizeof(read_data);
result = mtb_kvstore_read(&obj, "hello", read_data, &data_size);
CY_ASSERT(result == CY_RSLT_SUCCESS);
```

**Delete:**
```c
result = mtb_kvstore_delete(&obj, "hello");
CY_ASSERT(result == CY_RSLT_SUCCESS);
```

### Configuration

**Keys and Values**

- **Keys** are null-terminated ASCII strings. Maximum length is `MTB_KVSTORE_MAX_KEY_SIZE`
  (default: 64). Override with `DEFINES+=MTB_KVSTORE_MAX_KEY_SIZE=<value>` in the application
  Makefile.
- **Values** are arbitrary-length binary data. By default, each value plus the ~20-byte record
  header must fit within one `erase_size` block. For larger values, define
  `MTB_KVSTORE_MAX_VALUE_SIZE` in the application Makefile.
  > **Note:** `MTB_KVSTORE_MAX_VALUE_SIZE` is not currently enforced by the library but is
  > reserved for a future update. Defining it now ensures forward compatibility.

**RTOS Integration**

Enable thread safety by adding `COMPONENTS+=RTOS_AWARE` or `DEFINES+=CY_RTOS_AWARE`. This wraps
all API calls in a mutex.

- The library **must be initialized after the RTOS kernel has started**.
- The default mutex timeout is `MTB_KVSTORE_MUTEX_TIMEOUT_MS`. Override with
  `DEFINES+=MTB_KVSTORE_MUTEX_TIMEOUT_MS=<value>`.
- Write and delete operations may take significantly longer when garbage collection is triggered.
  Do not call them from timing-critical code. Use `mtb_kvstore_ensure_capacity` proactively in a
  non-time-sensitive context.

**Custom Memory Allocation**

Pass non-NULL `alloc_memory` and `free_memory` function pointers to `mtb_kvstore_init` to direct
the library to use a specific memory region for internal allocations instead of the default
`malloc`/`free`.

### Dependencies

* [block-storage](https://github.com/infineon/block-storage) library
* [serial-memory](https://github.com/infineon/serial-memory) library (only when using
  External Serial Memory)
* [abstraction-rtos](https://github.com/infineon/abstraction-rtos) library (only when
  `CY_RTOS_AWARE` / `RTOS_AWARE` is enabled)

## Design Details

### Storage Layout

The address space is divided into two equal areas: **active** and **swap**. All records are written
to the active area. The first record is an area header used during initialization. One half of the
provided storage is available for key-value data.

```
          active area                       swap area
+-----------------------------+  +-----------------------------+
|      area header record     |  |                             |
+-----------------------------+  |                             |
|      key value record 1     |  |                             |
+-----------------------------+  |                             |
|      key value record 2     |  |                             |
+-----------------------------+  |                             |
|      key value record 3     |  |                             |
+-----------------------------+  |                             |
|        free space           |  |                             |
+-----------------------------+  +-----------------------------+
```

### Record Structure

Records are stored sequentially. Each record contains a header with metadata (key/value sizes and
CRC), followed by key and value data, padded to the program size. Updates append a new record; the
latest record for a key takes precedence. Deletes append a record with a "deleted" flag.

```
+---------------------+-------------------------+--------------------------------+---------------+
| Record Header       | Key                     | Data                           | prog size pad |
+---------------------+-------------------------+--------------------------------+---------------+
```

### RAM Table

A RAM table maps a hash of each key to the offset of its latest record in storage. It is built
from storage during initialization and updated on every subsequent operation. When a key lookup is
performed, the actual key string is verified against storage to resolve hash collisions.

### Garbage Collection

Garbage collection copies all non-obsolete records to the swap area, promotes it to active, and
erases the former active area. It is triggered when:

* The active area lacks space for a new modification and contains obsolete records.
* A corrupted record is detected during initialization.
* `mtb_kvstore_ensure_capacity` is called and sufficient free space is not immediately available.

> **Note:** Write and delete operations may be significantly slower when garbage collection runs.
> Avoid calling them from timing-critical code.

## Release Notes and Changelog

- RELEASE.md - Detailed release notes for all versions

## More information

* [Reference Manual](https://infineon.github.io/kv-store/html/modules.html)
* [Infineon GitHub](https://github.com/infineon)
* [ModusToolbox Software Environment, Quick Start Guide, Documentation, and Videos](https://www.infineon.com/cms/en/design-support/tools/sdk/modustoolbox-software/)

## License

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0

## Copyright

© 2021-2026, Infineon Technologies AG, or an affiliate of Infineon Technologies AG.
All rights reserved.
