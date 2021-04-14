# Key Value Storage Library

## Overview
This library provides a convenient way to store information as key-value pairs in non-volatile storage.

## Features
* Supports any storage which can be modeled as a block device, including internal flash or external flash
(e.g. via QSPI).
* Allows partitioning storage by instantiating multiple instances of the library.
* Designed to be resilient to power failures.
* Designed to promote even wear of the storage.

## Storage
The utility operates on the assumption that the underlying storage is a block device which may not
have the same program and erase sizes. The application is required to provide an implementation for
a block device interface (`mtb_kvstore_bd_t`) that describes the characteristics of the underlying
storage and defines basic operations (read, write, erase). See `mtb_kvstore_init` for a description
of the constraints that the storage must satisfy.

## Keys and Values
### Keys
Keys are ASCII strings (Null terminated). The maximum key length is defined by `MTB_KVSTORE_MAX_KEY_SIZE`.
This can be overridden by the application by adding `DEFINES+=MTB_KVSTORE_MAX_KEY_SIZE=<value>` with the
desired value to application Makefile.

### Values
Values are arbitrary length binary data.

## RTOS Integration
In an RTOS environment, the library can be made thread safe by defining `DEFINE+=CY_RTOS_AWARE`. This
causes all API to be protected by a mutex to serialize access to underlying storage device. The
kvstore library must be initialized after the RTOS kernel has started for the mutex to be initialized
safely. The default timeout for the mutex is defined by `MTB_KVSTORE_MUTEX_TIMEOUT_MS` and can be
overridden by specifying `DEFINES+=MTB_KVSTORE_MUTEX_TIMEOUT_MS=<value>` with the application Makefile.

## Design details
### Sequential log of records
The key-value pairs are stored sequentially as records. Each operation appends a new record to the next
available program block. If multiple records exist for the same key, the record at the highest address takes
precedence over those that came before it. In an update operation, a new record with the key and updated value
is appended, overriding the previous value. In a delete operation, a new record with the key and a "deleted"
flag is appended.

### Memory Layout
The address space provided by the application is divided into two areas. At any given time, one area is
designated as active and the other as swap. All key/value pair records are stored in the active area.
Each time garbage collection is performed, the active and swap area designations are reversed.
The first record in the active area is an area header used to identify the current active area during
initialization. This means that one half of the provided storage space is available for key value storage.

### Record
Each record contains a record header (`_mtb_kvstore_record_header_t`) that contains metadata including
key/value sizes and a CRC. This is followed by the key and value data. The record is padded to the program
size.

### RAM table
A table that contains a hash for every key and the corresponding offset of the latest record for that key
in storage is maintained in RAM. This table is built from storage during initialization and is updated on
every subsequent operation. The key is verified by matching the key in the record in the storage. If it does
not match the next entry with the same hash in the RAM table is checked. This allow for the possibility that
multiple distinct keys may hash to the same value.

### Garbage collection
When the active area does not have sufficient space remaining to perform a requested modification (add,
update, delete), a garbage collection operation is performed. This process copies all of the non-obsolete
records (i.e. all of those listed in the RAM table) into the swap area. The swap area is then marked as the
new active area by programming the area header at the start. The former active area is erased and becomes
the new swap area.

**NOTE:**
Due to the garbage collection operation, write and delete operations may consume significantly more time than
typical when the active area becomes full. Hence, they must not be called from timing critical code.

## Dependencies
* [abstraction-rtos](https://github.com/cypresssemiconductorco/abstraction-rtos) library if the `CY_RTOS_AWARE`
macro is defined in the Makefile

## More information
* [Cypress Semiconductor, an Infineon Technologies Company](http://www.cypress.com)
* [Cypress Semiconductor GitHub](https://github.com/cypresssemiconductorco)
* [ModusToolbox](https://www.cypress.com/products/modustoolbox-software-environment)

---
© Cypress Semiconductor Corporation, 2021.
