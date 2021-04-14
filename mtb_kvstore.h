/***********************************************************************************************//**
 * \file mtb_kvstore.h
 *
 * \brief
 * Utility library for storing key value pairs in memory.
 *
 ***************************************************************************************************
 * \copyright
 * Copyright 2018-2021 Cypress Semiconductor Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **************************************************************************************************/
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include "cy_result.h"

#if defined(CY_RTOS_AWARE) || defined(COMPONENT_RTOS_AWARE)
#include "cyabs_rtos.h"
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * \addtogroup group_kvstore Key Value Storage Library
 * \{
 * This library provides a convenient way to store information as key-value pairs in non-volatile
 * storage.
 *
 * \section section_kvstore_getting_started Getting Started
 * This section provides steps for getting started with this library by providing examples
 * using the [Serial Flash](https://github.com/cypresssemiconductorco/serial-flash) (v1.X) and
 * [HAL Flash driver](https://github.com/cypresssemiconductorco/mtb-hal-cat1)
 * (v1.X) libraries.
 *
 * -# Include the kv-store library header in the application.
 *    \snippet kvstore_snip_serial.c snippet_mtb_kvstore_include
 *
 * -# Implement the block device interface.
 *      - Example implementation using serial-flash library.
 *        \snippet kvstore_snip_serial.c snippet_mtb_kvstore_ser_flash_bd
 *      - Example implementation using hal flash driver.
 *        \snippet kvstore_snip_flash.c snippet_mtb_kvstore_flash_bd
 *
 * -# Initialize the block device.
 *      - Example initialization for serial-flash library. This example configures
 *        the serial-flash using the memory configuration from the BSP. Please refer
 *        to the serial-flash [API Reference Guide]
 * (https://cypresssemiconductorco.github.io/serial-flash/html/index.html)
 *        for further details.
 *        \snippet kvstore_snip_serial.c snippet_mtb_kvstore_ser_flash_init
 *
 *      - Example initialization for hal flash driver. This example initializes
 *        the working flash if available otherwise it initializes the main flash.
 *        Please refer to the HAL [API Reference Guide]
 * (https://cypresssemiconductorco.github.io/mtb-hal-cat1/html/modules.html)
 *        for further details.
 *        \snippet kvstore_snip_flash.c snippet_mtb_kvstore_flash_init
 *
 * -# Initialize the kv-store library.
 *      - Set the start address and length.
 *          - If using the serial-flash library, for this example we define the space
 *            provided to the kv-store library to be the size of 2 sectors.
 *            \snippet kvstore_snip_serial.c snippet_mtb_kvstore_ser_flash_addr_len
 *          - If using the hal flash driver, for this example the address space
 *            provided starts 16 pages from the end of the flash. Note that
 *            if the device does not have a working flash (i.e. using main flash) care must
 *            be taken to ensure that the space allocated below is not used by anything
 *            (for eg. Application image).
 *            \snippet kvstore_snip_flash.c snippet_mtb_kvstore_flash_addr_len
 *      - Call \ref mtb_kvstore_init by passing the start address, length and block device.
 *        \snippet kvstore_snip_serial.c snippet_mtb_kvstore_init
 *
 * -# The library should now be ready to perform operations.
 *      - Write operation.
 *        \snippet kvstore_snip_serial.c snippet_mtb_kvstore_write
 *      - Read operation.
 *        \snippet kvstore_snip_serial.c snippet_mtb_kvstore_read
 *      - Delete operation.
 *        \snippet kvstore_snip_serial.c snippet_mtb_kvstore_delete
 */

#if !defined(MTB_KVSTORE_MAX_KEY_SIZE)
/** Maximum key size permitted. */
#define MTB_KVSTORE_MAX_KEY_SIZE                    (64U)
#endif

#if (defined(CY_RTOS_AWARE) || defined(COMPONENT_RTOS_AWARE)) && \
    !defined(MTB_KVSTORE_MUTEX_TIMEOUT_MS)
/** Timeout in ms for mutex timeout when using an RTOS. */
#define MTB_KVSTORE_MUTEX_TIMEOUT_MS                (50U)
#endif

/** An invalid parameter value is passed in. */
#define MTB_KVSTORE_BAD_PARAM_ERROR                 \
    CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_KVSTORE, 0)
/** The storage area passed in is not aligned to erase sector boundary. See
 * notes in \ref mtb_kvstore_init for more information on constraints.
 */
#define MTB_KVSTORE_ALIGNMENT_ERROR                 \
    CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_KVSTORE, 1)
/** Memory allocation failed. There is not enough space available on the heap. */
#define MTB_KVSTORE_MEM_ALLOC_ERROR                 \
    CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_KVSTORE, 2)
/** Invalid data was detected. The record may be corrupted. */
#define MTB_KVSTORE_INVALID_DATA_ERROR              \
    CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_KVSTORE, 3)
/** Erased data was detected. The record may be corrupted */
#define MTB_KVSTORE_ERASED_DATA_ERROR               \
    CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_KVSTORE, 4)
/** Item was not found in the storage. */
#define MTB_KVSTORE_ITEM_NOT_FOUND_ERROR            \
    CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_KVSTORE, 5)
/** The storage is full. */
#define MTB_KVSTORE_STORAGE_FULL_ERROR              \
    CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_KVSTORE, 6)

/** Function prototype for reading data from the block device.
 *
 * @param[in]  context  Context object that is passed into \ref mtb_kvstore_init
 * @param[in]  addr     Address to read the data from the block device. This address
 *                      is passed in as start_addr + offset.
 * @param[out] buf      Buffer to read the data.
 * @return Result of the read operation.
 */
typedef cy_rslt_t (* mtb_kvstore_bd_read)(void* context, uint32_t addr, uint32_t length,
                                          uint8_t* buf);

/** Function prototype for writing data to the block device.
 *
 * @param[in]  context  Context object that is passed into \ref mtb_kvstore_init
 * @param[in]  addr     Address to program the data into the block device. This address
 *                      is passed in as start_addr + offset.
 * @param[out] buf      Data that needs to be written
 * @return Result of the program operation.
 */
typedef cy_rslt_t (* mtb_kvstore_bd_program)(void* context, uint32_t addr, uint32_t length,
                                             const uint8_t* buf);

/** Function prototype for read from the block device.
 *
 * @param[in]  context  Context object that is passed into \ref mtb_kvstore_init
 * @param[in]  addr     Address to read the data from the device. This address
 *                      is passed in as start_addr + offset.
 * @param[out] length   length of the data that needs to be erased.
 * @return Result of the erase operation.
 */
typedef cy_rslt_t (* mtb_kvstore_bd_erase)(void* context, uint32_t addr, uint32_t length);

/** Function prototype to get the read size of the block device for a specific address.
 *
 * @param[in]  context  Context object that is passed into \ref mtb_kvstore_init
 * @param[in]  addr     Address for which the read size is queried. This address
 *                      is passed in as start_addr + offset.
 * @return Read size of the memory device.
 */
typedef uint32_t (* mtb_kvstore_bd_read_size)(void* context, uint32_t addr);

/** Function prototype to get the program size of the block device for a specific address
 *
 * @param[in]  context  Context object that is passed into \ref mtb_kvstore_init
 * @param[in]  addr     Address for which the program size is queried. This address
 *                      is passed in as start_addr + offset.
 * @return Program size of the memory device.
 */
typedef uint32_t (* mtb_kvstore_bd_program_size)(void* context, uint32_t addr);

/** Function prototype to get the erase size of the block device for a specific address
 *
 * @param[in]  context  Context object that is passed into \ref mtb_kvstore_init
 * @param[in]  addr     Address for which the erase size is queried. This address is passed in a
 *                      start_addr + offset.
 * @return Erase size of the memory device.
 */
typedef uint32_t (* mtb_kvstore_bd_erase_size)(void* context, uint32_t addr);

/** Block device interface */
typedef struct
{
    mtb_kvstore_bd_read         read;           /**< Function to read from device */
    mtb_kvstore_bd_program      program;        /**< Function to program to device */
    mtb_kvstore_bd_erase        erase;          /**< Function to erase device */
    mtb_kvstore_bd_read_size    read_size;      /**< Function to get read size for an address */
    mtb_kvstore_bd_program_size program_size;   /**< Function to get program size for an address */
    mtb_kvstore_bd_erase_size   erase_size;     /**< Function to get erase size for an address */
    void*                       context;        /**< Context object that can be used in the block
                                                   device implementation */
} mtb_kvstore_bd_t;

/** \cond INTERNAL */

/** Ram table entry structure */
typedef struct
{
    uint16_t    hash;
    uint32_t    offset;
} mtb_kvstore_ram_table_entry_t;

/** KV store context */
typedef struct
{
    uint32_t                        start_addr;
    uint32_t                        length;
    const mtb_kvstore_bd_t*         bd;

    mtb_kvstore_ram_table_entry_t*  ram_table;
    uint32_t                        num_entries;
    uint32_t                        max_entries;

    uint8_t*                        transaction_buffer;
    size_t                          transaction_buffer_size;
    char                            key_buffer[MTB_KVSTORE_MAX_KEY_SIZE];

    uint32_t                        active_area_addr;
    uint32_t                        gc_area_addr;
    uint32_t                        free_space_offset;
    uint16_t                        active_area_version;

    uint32_t                        consumed_size;

    #if CY_RTOS_AWARE
    cy_mutex_t                      mtb_kvstore_mutex;
    #endif
} mtb_kvstore_t;

/** \endcond */

/** Initialize a instance kv-store library
 *
 * @param[out]  obj          Pointer to a kv-store object. The caller must allocate the memory
 *                           for this object but the init function will initialize its contents.
 * @param[in]   start_addr   Start address for the memory. All addresses when performing memory
 *                           operations will be offset from this address. See notes for constraints.
 * @param[in]   length       Total space available in bytes. See notes for constraints.
 * @param[in]   block_device Block device interface for the underlying memory to be used.
 *
 * Address space considerations
 * \note The start_addr and start_addr + length must be aligned to the erase sector boundary.
 * \note An even number of erase sectors must be provided as storage. (2 * N * erase sector size)
 *       where N is the number of sectors.
 * \note The implementation assumes that the value of the storage in the erased state is either 0x00
 *       or 0xFF.
 * \note The space provided to the library provided must have the uniform characteristics (erase,
 *       program and read size). A space spanning regions with different characteristics in a
 *       hybrid sector device is not supported and if provided may lead to undefined behavior.
 *
 * RTOS considerations
 * \note In a RTOS environment the library must be initialized after the RTOS kernel has started.
 *
 * @return Result of the initialization operation.
 */
cy_rslt_t mtb_kvstore_init(mtb_kvstore_t* obj, uint32_t start_addr, uint32_t length,
                           const mtb_kvstore_bd_t* block_device);

/** Store a key value pair
 *
 * @param[in] obj  Pointer to a kv-store object
 * @param[in] key  Lookup key for the data.
 * @param[in] data Pointer to the start of the data to be stored.
 * @param[in] size Total size of the data in bytes.
 *
 * @return Result of the write operation.
 */
cy_rslt_t mtb_kvstore_write(mtb_kvstore_t* obj, const char* key, const uint8_t* data,
                            uint32_t size);

/** Read data associated with a key
 *
 * @param[in]       obj  Pointer to a kv-store object
 * @param[in]       key  Lookup key for the data.
 * @param[out]      data Pointer to the start of the buffer for the data to be read into.
 * @param[in,out]   size [in] Total size of the data in bytes. [out] Actual size of the data in
 *                       storage. If a data buffer is provided then the size cannot be NULL or 0.
 *
 * \note It is valid to set both `data` and `size` to NULL to check if the key exists in
 * the storage.
 *
 *  @return Result of the read operation.
 */
cy_rslt_t mtb_kvstore_read(mtb_kvstore_t* obj, const char* key, uint8_t* data,
                           uint32_t* size);

/** Delete a key value pair
 *
 * \note This function will return CY_RSLT_SUCCESS if the key cannot be found in the storage.
 *
 * @param[in]   obj Pointer to a kv-store object.
 * @param[in]   key Lookup key to delete key value pair.
 *
 * @return Result of the delete operation.
 */
cy_rslt_t mtb_kvstore_delete(mtb_kvstore_t* obj, const char* key);

/** Query the size consumed in the kv-store storage.
 *
 * @param[in]   obj  Pointer to a kv-store object.

 * @return      Size of storage consumed in bytes.
 */
uint32_t mtb_kvstore_size(mtb_kvstore_t* obj);

/** Query the free space available in the kv-store storage.
 *
 * @param[in]   obj  Pointer to a kv-store object
 *
 * @return      Size of storage free in bytes.
 */
uint32_t mtb_kvstore_remaining_size(mtb_kvstore_t* obj);

/** Reset kv-store storage.
 *
 * This function erases all the data in the storage.
 *
 * @param[in]   obj Pointer to a kv-store object
 *
 * @return Result of the reset operation
 */
cy_rslt_t mtb_kvstore_reset(mtb_kvstore_t* obj);

/** Delete kv-store instance.
 *
 * This function frees any program memory allocated by the library.
 *
 * @param[in]   obj Pointer to a kv-store object
 */
void mtb_kvstore_deinit(mtb_kvstore_t* obj);

#if defined(__cplusplus)
}
#endif

/** \} group_kvstore */
