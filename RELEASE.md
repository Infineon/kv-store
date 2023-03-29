# Key Value Storage Library Release Notes
The key value storage library provides an API enabling use of non-volatile storage to store data in key-value pairs.

### What's Included?
APIs for storing key-value pairs of data in non-volatile storage.

### What Changed?
#### v1.1.1
* Fixed NULL dereference in mtb_kvstore_read function when checking if key exists in storage by passing NULL into both _data_ and _size_ parameters
#### v1.1.0
* Added new function: mtb_kvstore_ensure_capacity
* Added new cy_rslt_t return type: MTB_KVSTORE_BUFFER_TOO_SMALL
* New partial read funtion: can read _size_ number of bytes starting from _offset bytes_ into the value
* Read functions set unused read buffer space to 0's
* New key exists and value size functions
#### v1.0.1
* Fixed issue with RTOS_AWARE component not being handled in one case
* Minor documentation updates
#### v1.0.0
* Initial release

### Supported Software and Tools
This version of the Key Value Storage Library was validated for compatibility with the following Software and Tools:

| Software and Tools                        | Version |
| :---                                      | :----:  |
| ModusToolbox™ Software Environment        | 2.4.0   |
| GCC Compiler                              | 10.3.1  |
| IAR Compiler                              | 8.4     |
| ARM Compiler                              | 6.11    |

Minimum required ModusToolbox™ Software Environment: v2.0

### More information
Use the following links for more information, as needed:
* [API Reference Guide](https://infineon.github.io/kv-store/html/modules.html)
* [Cypress Semiconductor, an Infineon Technologies Company](http://www.cypress.com)
* [Infineon GitHub](https://github.com/infineon)
* [ModusToolbox™](https://www.cypress.com/products/modustoolbox-software-environment)

---
© Cypress Semiconductor Corporation (an Infineon company) or an affiliate of Cypress Semiconductor Corporation, 2021.