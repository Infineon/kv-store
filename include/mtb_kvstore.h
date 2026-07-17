/***********************************************************************************************//**
 * \file mtb_kvstore.h
 *
 * \brief
 * Utility library for storing key value pairs in memory.
 *
 ***************************************************************************************************
 * \copyright
 * (c) 2021-2025, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG.  SPDX-License-Identifier: Apache-2.0
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
#include "mtb_block_storage.h"

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

/** When passed as an argument to \ref mtb_kvstore_ensure_capacity,
 * indicates that cleanup tasks should always be performed regardless
 * of the amount of space which is currently free, to ensure the maximum
 * possible amount of free space is available
 */
#define MTB_KVSTORE_ENSURE_MAX (0xFFFFFFFFu)

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
/** Buffer provided is too small for value found. */
#define MTB_KVSTORE_BUFFER_TOO_SMALL                \
    CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_KVSTORE, 7)

/** Function prototype for reading data from the block device.
 *
 * @param[in]  context  Context object that is passed into \ref mtb_kvstore_init
 * @param[in]  addr     Address to read the data from the block device. This address
 *                      is passed in as start_addr + offset.
 * @param[in]  length   Length of the data that needs to be read.
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
 * @param[in]  length   Length of the data that needs to be written
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
 * @param[in]  length   Length of the data that needs to be erased.
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

/** Function prototype for allocating memory to be used for kv-store internal operations.
 *
 * @param[in]  size     Size in bytes of the memory to be allocated
 * @return Pointer to the allocated memory in case the memory allocation is success or
 *         NULL in case of failure.
 */
typedef void* (* mtb_kvstore_allocate_memory_t)(size_t size);

/** Function prototype for freeing memory
 *
 * @param[in]  ptr     Pointer to the memory block to be freed
 * @return None.
 */
typedef void (* mtb_kvstore_free_memory_t)(void* ptr);

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
    const mtb_block_storage_t*      bd;

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
    mtb_kvstore_allocate_memory_t   alloc_memory;
    mtb_kvstore_free_memory_t       free_memory;

    #if defined(CY_RTOS_AWARE) || defined(COMPONENT_RTOS_AWARE)
    cy_mutex_t                      mtb_kvstore_mutex;
    #endif
} mtb_kvstore_t;

/** \endcond */

/** Initialize an instance of the kv-store library
 *
 * @param[out]  obj          Pointer to a kv-store object. The caller must allocate the memory
 *                           for this object but the init function will initialize its contents.
 * @param[in]   start_addr   Start address for the memory. All addresses when performing memory
 *                           operations will be offset from this address. See notes for constraints.
 * @param[in]   length       Total space available in bytes. See notes for constraints.
 * @param[in]   bsd          Block storage device interface for the underlying memory to be used.
 * @param[in]   alloc_memory Optional parameter that could be used to allocate the requested memory
 *                           size. Application could set up this function pointer if they desire to
 *                           allocate the memory from specific memory region. If not null, KV Store
 *                           uses the specified function for allocating the memory needed for
 *                           internal operations. When specified as NULL, KV Store uses the malloc
 *                           function to allocate the memory
 * @param[in]   free_memory  Optional parameter that could be used to free the memory block
 *                           allocated using the 'alloc_memory' function.If not null, KV Store
 *                           uses the specified function for freeing the memory. When specified
 *                           as NULL, KV Store uses the free function to free the memory
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
 * \note If the application desires to use specific memory region for KV Store internal operation,
 *       it should set up both the alloc_memory and free_memory functions.
 * \note Verify that start_addr is a valid, mapped address before calling this function. When
 *       deriving start_addr from a HAL NVM region info structure, do not blindly use the last
 *       region (regions[region_count - 1]). On some devices the HAL unconditionally appends
 *       extra regions (e.g. a dual-bank region on a single-bank device) whose start_address
 *       may resolve to 0. Passing address 0 (or any unmapped address) will cause a HardFault
 *       when the library attempts to read the storage during initialization.
 *
 * RTOS considerations
 * \note In a RTOS environment the library must be initialized after the RTOS kernel has started.
 *
 * @return Result of the initialization operation.
 */
cy_rslt_t mtb_kvstore_init(mtb_kvstore_t* obj, uint32_t start_addr, uint32_t length,
                           const mtb_block_storage_t* bsd,
                           mtb_kvstore_allocate_memory_t alloc_memory,
                           mtb_kvstore_free_memory_t free_memory);

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

/** Read data associated with a key. If buffer is too small, will partially read and fill buffer.
 *  No corruption checking is performed.
 *
 * @param[in]       obj      Pointer to a kv-store object
 * @param[in]       key      Lookup key for the data.
 * @param[out]      data     Pointer to the start of the buffer for the data to be read into.
 * @param[in,out]   size     [in] Total size of the data in bytes. [out] Number of bytes read into
 *                           buffer. If a data buffer is provided then the size cannot be NULL
 *                           or 0.
 * @param[in]       offset_bytes Number of bytes to skip at the start the value read into data
 *
 *  @return Result of the read operation.
 */
cy_rslt_t mtb_kvstore_read_partial(mtb_kvstore_t* obj, const char* key, uint8_t* data,
                                   uint32_t* size, const uint32_t offset_bytes);

/** Check if a key is stored in memory
 *
 * @param[in]   obj     Pointer to a kv-store object
 * @param[in]   key     Lookup key
 *
 * @return Result of the key_exists operation
 */
cy_rslt_t mtb_kvstore_key_exists(mtb_kvstore_t* obj, const char* key);

/** Get the size in bytes of data in memory corresponding to a given key.
 *
 * @param[in]   obj     Pointer to a kv-store object
 * @param[in]   key     Lookup key for the data
 * @param[out]  size    Size of data in bytes
 *
 * @return Result of the value size operation
 */
cy_rslt_t mtb_kvstore_value_size(mtb_kvstore_t* obj, const char* key, uint32_t* size);

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

/** Tries to make the specified amount of space available
 *  for immediate use. If necessary, internal cleanup operations
 *  will be executed to make additional space available, so this
 * is a potentially long-running operation.
 *
 * @param[in]   obj  Pointer to a kv-store object
 * @param[in]   size Amount of space to ensure is available, in bytes.
 *                   To always perform cleanup operations regardless
 *                   of the amount of space currently free,
 *                   use \ref MTB_KVSTORE_ENSURE_MAX.
 *
 * @return      Result of the operation. If `size` is not \ref MTB_KVSTORE_ENSURE_MAX
 *              and it is not possible to make the requested amount of space
 *              available, returns \ref MTB_KVSTORE_STORAGE_FULL_ERROR.
 */
cy_rslt_t mtb_kvstore_ensure_capacity(mtb_kvstore_t* obj, uint32_t size);

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
