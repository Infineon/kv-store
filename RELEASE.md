# Key Value Storage Library Release Notes
The key value storage library provides an API enabling use of non-volatile storage to store data in key-value pairs.

### What's Included?
APIs for storing key-value pairs of data in non-volatile storage.

### What Changed?
#### v2.2.1
* Documentation update
#### v2.2.0
* Documentation update
#### v2.1.0
* Fixed minor issue in DCACHE handling
#### v2.0.0
* Replaced the kv-store-specific block device interface with the standard mtb-block-storage interface
* Added ability to override the default `malloc` and `free` functions for custom dynamic memory allocation
#### v1.1.1
* Fixed NULL dereference in mtb_kvstore_read function when checking if a key exists in storage by passing NULL into both _data_ and _size_ parameters
#### v1.1.0
* Added new function: mtb_kvstore_ensure_capacity
* Added new cy_rslt_t return type: MTB_KVSTORE_BUFFER_TOO_SMALL
* New partial read function: can read _size_ number of bytes starting from _offset_ bytes into the value
* Read functions set unused read buffer space to 0s
* New key exists and value size functions
#### v1.0.1
* Fixed issue with RTOS_AWARE component not being handled in one case
* Minor documentation updates
#### v1.0.0
* Initial release


---
(c) 2021-2026, Infineon Technologies AG, or an affiliate of Infineon
Technologies AG.  All rights reserved.
