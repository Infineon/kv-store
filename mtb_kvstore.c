#include <string.h>
#include <stdbool.h>

#include "mtb_kvstore.h"
#include "cy_utils.h"

#define _MTB_KVSTORE_MIN_BUFF_SIZE          (128U)
#define _MTB_KVSTORE_HEADER_MAGIC           (0xFACEFACE)
#define _MTB_KVSTORE_FORMAT_VERSION         (0U)
#define _MTB_KVSTORE_INITIAL_AREA_VERSION   (1U)
#define _MTB_KVSTORE_DELETE_FLAG            (1U << 7)
#define _MTB_KVSTORE_NO_FLAG                (0U)
#define _MTB_KVSTORE_INIT_MAX_KEYS          (32U)
#define _MTB_KVSTORE_AREA_SIZE(obj)         (obj->length / 2)
#define _MTB_KVSTORE_AREA_HEADER_OFFSET     (0U)
#define _MTB_KVSTORE_CRC_INIT_VAL           (0xFFFFU)

/***************************** Internal Data Structures ********************************/

// Note: If the following structure is changed the _mtb_kvstore_get_header_crc function
// must be changed to adjust the CRC calculation accordingly.
typedef struct
{
    uint32_t    magic;          /* A constant value, for quick validity checking. */
    uint8_t     format_version; /* Version of the record format */
    uint8_t     flags;          /* Used to mark a record deleted. */
    uint16_t    header_size;    /* Size of the header */
    uint16_t    key_size;       /* Size of the key */
    uint32_t    data_size;      /* Size of the data */
    uint32_t    crc;            /* A 16-bit CRC, calculated on header
                                   (except CRC), key and data. */
} _mtb_kvstore_record_header_t;

typedef struct
{
    uint16_t version; /* Version of the area. Use to check if area is the active area */
    uint16_t format_version; /* Version of the data format in the area header */
} _mtb_kvstore_area_record_data_t;

typedef enum
{
    _MTB_KVSTORE_OPER_ADD,
    _MTB_KVSTORE_OPER_DELETE,
    _MTB_KVSTORE_OPER_UPDATE
} _mtb_kvstore_operation_t;

typedef struct
{
    uint32_t ram_tbl_idx;
    mtb_kvstore_ram_table_entry_t entry;
} _mtb_kvstore_update_ram_table_info_t;

typedef struct
{
    uint32_t old_record_size;
    uint32_t new_record_size;
} _mtb_kvstore_update_consumed_size_info_t;


typedef struct
{
    const char* key;
    const uint8_t* data;
    uint32_t data_size;
    uint16_t key_hash;
} _mtb_kvstore_update_record_info_t;

typedef struct
{
    uint32_t ram_tbl_idx;
    _mtb_kvstore_update_consumed_size_info_t consumed_size_info;
    const _mtb_kvstore_update_record_info_t* update_rec_info;
} _mtb_kvstore_record_info_t;

static char* _mtb_kvstore_area_rec_key = "MTBAREAIDX";

/*************************** Internal Helper Functions *****************************/

#if defined(CY_RTOS_AWARE) || defined(COMPONENT_RTOS_AWARE)

//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_initlock
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_initlock(mtb_kvstore_t* obj)
{
    return cy_rtos_init_mutex(&(obj->mtb_kvstore_mutex));
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_lock
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_lock(mtb_kvstore_t* obj)
{
    return cy_rtos_get_mutex(&(obj->mtb_kvstore_mutex), MTB_KVSTORE_MUTEX_TIMEOUT_MS);
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_lock_wait_forever
//--------------------------------------------------------------------------------------------------
static void _mtb_kvstore_lock_wait_forever(mtb_kvstore_t* obj)
{
    cy_rslt_t result = cy_rtos_get_mutex(&(obj->mtb_kvstore_mutex), CY_RTOS_NEVER_TIMEOUT);
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    CY_UNUSED_PARAMETER(result);
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_unlock
//--------------------------------------------------------------------------------------------------
static void _mtb_kvstore_unlock(mtb_kvstore_t* obj)
{
    cy_rslt_t result = cy_rtos_set_mutex(&(obj->mtb_kvstore_mutex));
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    CY_UNUSED_PARAMETER(result);
}


#else // if defined(CY_RTOS_AWARE)
//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_initlock
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_initlock(mtb_kvstore_t* obj)
{
    CY_UNUSED_PARAMETER(obj);
    return CY_RSLT_SUCCESS;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_lock
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_lock(mtb_kvstore_t* obj)
{
    CY_UNUSED_PARAMETER(obj);
    return CY_RSLT_SUCCESS;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_lock_wait_forever
//--------------------------------------------------------------------------------------------------
static void _mtb_kvstore_lock_wait_forever(mtb_kvstore_t* obj)
{
    CY_UNUSED_PARAMETER(obj);
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_unlock
//--------------------------------------------------------------------------------------------------
static void _mtb_kvstore_unlock(mtb_kvstore_t* obj)
{
    CY_UNUSED_PARAMETER(obj);
}


#endif // if defined(CY_RTOS_AWARE)

//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_is_valid_key
//--------------------------------------------------------------------------------------------------
static inline bool _mtb_kvstore_is_valid_key(const char* key)
{
    uint32_t key_size = strlen(key);
    return (key != NULL) && (key_size > 0) && (key_size < MTB_KVSTORE_MAX_KEY_SIZE);
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_align_up
//--------------------------------------------------------------------------------------------------
static inline uint32_t _mtb_kvstore_align_up(uint32_t val, uint32_t size)
{
    return (((val - 1) / size) + 1) * size;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_is_aligned
//--------------------------------------------------------------------------------------------------
static inline bool _mtb_kvstore_is_aligned(uint32_t val, uint32_t size)
{
    return ((val & (size - 1)) == 0);
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_get_record_size
//--------------------------------------------------------------------------------------------------
static uint32_t _mtb_kvstore_get_record_size(mtb_kvstore_t* obj, uint32_t record_offset,
                                             uint32_t key_size, uint32_t data_size)
{
    uint32_t prog_size = obj->bd->program_size(obj->bd->context, record_offset);
    return _mtb_kvstore_align_up(sizeof(_mtb_kvstore_record_header_t) + key_size + data_size,
                                 prog_size);
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_get_area_header_record_size
//--------------------------------------------------------------------------------------------------
static inline uint32_t _mtb_kvstore_get_area_header_record_size(mtb_kvstore_t* obj,
                                                                uint32_t area_address)
{
    return _mtb_kvstore_get_record_size(obj, area_address,
                                        strlen(_mtb_kvstore_area_rec_key),
                                        sizeof(_mtb_kvstore_area_record_data_t));
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_increment_max_keys
//--------------------------------------------------------------------------------------------------
uint16_t _mtb_kvstore_crc16(const uint8_t* data, uint32_t length, uint16_t init_crc)
{
    //Using CRC-16/CCITT-0 Algorithm
    uint32_t crc = init_crc;
    int i;
    while (length--)
    {
        crc ^= *data++ << 8;
        for (i = 0; i < 8; i++)
        {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc & 0xffff;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_increment_max_keys
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_increment_max_keys(mtb_kvstore_t* obj)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t new_entry_count = obj->max_entries * 2;
    mtb_kvstore_ram_table_entry_t* new_table = (mtb_kvstore_ram_table_entry_t*)malloc(
        new_entry_count * sizeof(mtb_kvstore_ram_table_entry_t));
    if (new_table != NULL)
    {
        memcpy(new_table, obj->ram_table, obj->max_entries * sizeof(mtb_kvstore_ram_table_entry_t));
        free(obj->ram_table);
        obj->ram_table = new_table;
        obj->max_entries = new_entry_count;
    }
    else
    {
        result = MTB_KVSTORE_MEM_ALLOC_ERROR;
    }
    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_update_ram_table
//--------------------------------------------------------------------------------------------------
static void _mtb_kvstore_update_ram_table(mtb_kvstore_t* obj, _mtb_kvstore_operation_t operation,
                                          const _mtb_kvstore_update_ram_table_info_t* info)
{
    switch (operation)
    {
        case _MTB_KVSTORE_OPER_DELETE:
            CY_ASSERT(info->ram_tbl_idx < obj->num_entries);
            obj->num_entries--;
            if (info->ram_tbl_idx < obj->num_entries)
            {
                memmove(&obj->ram_table[info->ram_tbl_idx], &obj->ram_table[info->ram_tbl_idx + 1],
                        (obj->num_entries - info->ram_tbl_idx) *
                        sizeof(mtb_kvstore_ram_table_entry_t));
            }
            break;

        case _MTB_KVSTORE_OPER_ADD:
            CY_ASSERT(obj->num_entries < obj->max_entries);
            CY_ASSERT(info->ram_tbl_idx <= obj->num_entries);
            if (info->ram_tbl_idx < obj->num_entries)
            {
                memmove(&obj->ram_table[info->ram_tbl_idx + 1], &obj->ram_table[info->ram_tbl_idx],
                        (obj->num_entries - info->ram_tbl_idx) *
                        sizeof(mtb_kvstore_ram_table_entry_t));
            }
            obj->num_entries++;
            obj->ram_table[info->ram_tbl_idx].hash = info->entry.hash;
            obj->ram_table[info->ram_tbl_idx].offset = info->entry.offset;
            break;

        case _MTB_KVSTORE_OPER_UPDATE:
            obj->ram_table[info->ram_tbl_idx].hash = info->entry.hash;
            obj->ram_table[info->ram_tbl_idx].offset = info->entry.offset;
            break;

        default:
            CY_ASSERT(false);
    }
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_update_consumed_size
//--------------------------------------------------------------------------------------------------
static void _mtb_kvstore_update_consumed_size(mtb_kvstore_t* obj,
                                              _mtb_kvstore_operation_t operation,
                                              const _mtb_kvstore_update_consumed_size_info_t* info)
{
    switch (operation)
    {
        case _MTB_KVSTORE_OPER_DELETE:
            obj->consumed_size -= info->old_record_size;
            break;

        case _MTB_KVSTORE_OPER_UPDATE:
            obj->consumed_size = obj->consumed_size - info->old_record_size + info->new_record_size;
            break;

        case _MTB_KVSTORE_OPER_ADD:
            obj->consumed_size += info->new_record_size;
            break;

        default:
            CY_ASSERT(false);
    }
}


//--------------------------------------------------------------------------------------------------
//_mtb_kvstore_erase_area
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_erase_area(mtb_kvstore_t* obj, uint32_t area_address)
{
    // This function operates on the assumption that erasing a sector is atomic. We first erase
    // everything but the first sector.
    uint32_t erase_size = obj->bd->erase_size(obj->bd->context, area_address);

    // Erase from second sector to the end
    cy_rslt_t result = CY_RSLT_SUCCESS;
    if (erase_size < _MTB_KVSTORE_AREA_SIZE(obj))
    {
        result = obj->bd->erase(obj->bd->context, area_address + erase_size,
                                _MTB_KVSTORE_AREA_SIZE(obj) - erase_size);
    }

    if (result == CY_RSLT_SUCCESS)
    {
        // Erase the first sector.
        result = obj->bd->erase(obj->bd->context, area_address, erase_size);
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_get_header_crc
//--------------------------------------------------------------------------------------------------
static uint16_t _mtb_kvstore_get_header_crc(_mtb_kvstore_record_header_t* record_header,
                                            uint16_t init_crc)
{
    uint16_t crc = init_crc;

    // Compute CRC for header
    crc = _mtb_kvstore_crc16((uint8_t*)&record_header->magic, sizeof(record_header->magic), crc);
    crc =
        _mtb_kvstore_crc16((uint8_t*)&record_header->format_version,
                           sizeof(record_header->format_version), crc);
    crc = _mtb_kvstore_crc16((uint8_t*)&record_header->flags, sizeof(record_header->flags), crc);
    crc =
        _mtb_kvstore_crc16((uint8_t*)&record_header->header_size,
                           sizeof(record_header->header_size),
                           crc);
    crc = _mtb_kvstore_crc16((uint8_t*)&record_header->key_size, sizeof(record_header->key_size),
                             crc);
    crc = _mtb_kvstore_crc16((uint8_t*)&record_header->data_size, sizeof(record_header->data_size),
                             crc);

    return crc;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_get_record_crc
//--------------------------------------------------------------------------------------------------
static uint16_t _mtb_kvstore_get_record_crc(_mtb_kvstore_record_header_t* record_header,
                                            const char* key,
                                            const uint8_t* data)
{
    CY_ASSERT(record_header != NULL);
    CY_ASSERT(key != NULL);
    uint16_t crc = _MTB_KVSTORE_CRC_INIT_VAL;

    // Compute CRC for header
    crc = _mtb_kvstore_get_header_crc(record_header, crc);
    crc = _mtb_kvstore_crc16((uint8_t*)key, record_header->key_size, crc);
    if ((data != NULL) && (record_header->data_size != 0))
    {
        crc = _mtb_kvstore_crc16(data, record_header->data_size, crc);
    }

    return crc;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_buffered_crc_compute
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_buffered_crc_compute(mtb_kvstore_t* obj, uint32_t address,
                                                   uint32_t size, uint16_t* crc)
{
    CY_ASSERT(obj != NULL);
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t remaining_size = size;
    while (remaining_size > 0)
    {
        uint32_t transfer_size = (obj->transaction_buffer_size >= remaining_size)
                                ? remaining_size
                                : obj->transaction_buffer_size;

        result = obj->bd->read(obj->bd->context, address, transfer_size, obj->transaction_buffer);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }

        *crc = _mtb_kvstore_crc16(obj->transaction_buffer, transfer_size, *crc);

        address += transfer_size;
        remaining_size -= transfer_size;
    }
    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_validate_key
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_validate_key(mtb_kvstore_t* obj, uint32_t key_addr,
                                           const char* user_key, uint32_t key_size)
{
    CY_ASSERT(obj != NULL);

    if (strlen(user_key) != key_size)
    {
        return MTB_KVSTORE_ITEM_NOT_FOUND_ERROR;
    }

    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t remaining_size = key_size;
    while (remaining_size > 0)
    {
        uint32_t transfer_size =
            (obj->transaction_buffer_size >=
             remaining_size) ? remaining_size : obj->transaction_buffer_size;
        result = obj->bd->read(obj->bd->context, key_addr, transfer_size, obj->transaction_buffer);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }

        if (memcmp(user_key, obj->transaction_buffer, transfer_size) != 0)
        {
            return MTB_KVSTORE_ITEM_NOT_FOUND_ERROR;
        }

        key_addr += transfer_size;
        remaining_size -= transfer_size;
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_read_record
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_read_record(mtb_kvstore_t* obj,
                                          uint32_t area_address,
                                          uint32_t offset,
                                          _mtb_kvstore_record_header_t* record_header,
                                          const char* key,
                                          bool validate_key,
                                          uint8_t* data,
                                          uint32_t* data_size)
{
    CY_ASSERT(obj != NULL);
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint16_t crc = _MTB_KVSTORE_CRC_INIT_VAL;

    uint32_t record_start_addr = area_address + offset;
    uint8_t header_size = sizeof(_mtb_kvstore_record_header_t);

    CY_ASSERT(record_header != NULL);

    // Read header for the record
    result =
        obj->bd->read(obj->bd->context, record_start_addr, header_size, (uint8_t*)record_header);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    if ((record_header->magic == 0xFFFFFFFF) || (record_header->magic == 0))
    {
        return MTB_KVSTORE_ERASED_DATA_ERROR;
    }

    if (record_header->magic != _MTB_KVSTORE_HEADER_MAGIC)
    {
        return MTB_KVSTORE_INVALID_DATA_ERROR;
    }

    if ((record_header->key_size == 0) || (record_header->key_size >= MTB_KVSTORE_MAX_KEY_SIZE))
    {
        return MTB_KVSTORE_INVALID_DATA_ERROR;
    }

    // If a data buffer is provided and the size is less that what is in the storage
    // return error.
    if ((data != NULL) && (data_size != NULL) && (*data_size < record_header->data_size))
    {
        *data_size = record_header->data_size;
        return MTB_KVSTORE_INVALID_DATA_ERROR;
    }

    crc = _mtb_kvstore_get_header_crc(record_header, crc);

    // Copy key into the provided key area
    uint32_t key_addr = record_start_addr + record_header->header_size;
    if (key != NULL)
    {
        if (validate_key)
        {
            result = _mtb_kvstore_validate_key(obj, key_addr, key, record_header->key_size);
            if (result != CY_RSLT_SUCCESS)
            {
                return result;
            }
        }
        else
        {
            result = obj->bd->read(obj->bd->context, key_addr, record_header->key_size,
                                   (uint8_t*)key);
            if (result != CY_RSLT_SUCCESS)
            {
                return result;
            }
        }

        // If the user passes in a key for validation since at this point we have
        // validated that the key on the storage is the same as what was passes in
        // it should be ok to use the key passed in by the user for CRC calculation.
        crc = _mtb_kvstore_crc16((uint8_t*)key, record_header->key_size, crc);
    }
    else
    {
        // Start buffered CRC
        result = _mtb_kvstore_buffered_crc_compute(obj, key_addr, record_header->key_size, &crc);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }
    }

    uint32_t data_addr = key_addr + record_header->key_size;
    if ((data != NULL) && (data_size != NULL))
    {
        // Copy data into the data buffer provided
        result = obj->bd->read(obj->bd->context, data_addr, record_header->data_size, data);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }

        crc = _mtb_kvstore_crc16(data, record_header->data_size, crc);
    }
    else
    {
        result = _mtb_kvstore_buffered_crc_compute(obj, data_addr, record_header->data_size, &crc);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }
    }

    // If the CRC did not match then record is corrupted.
    if (record_header->crc != crc)
    {
        return MTB_KVSTORE_INVALID_DATA_ERROR;
    }

    if (data_size != NULL)
    {
        *data_size = record_header->data_size;
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_setup_record_header
//--------------------------------------------------------------------------------------------------
static void _mtb_kvstore_setup_record_header(const char* key,
                                             const uint8_t* data,
                                             uint32_t data_size,
                                             uint8_t format_version,
                                             _mtb_kvstore_operation_t operation,
                                             _mtb_kvstore_record_header_t* record_header)
{
    CY_ASSERT(key != NULL);

    memset(record_header, 0, sizeof(_mtb_kvstore_record_header_t));

    record_header->magic = _MTB_KVSTORE_HEADER_MAGIC;
    record_header->format_version = format_version;
    record_header->header_size = sizeof(_mtb_kvstore_record_header_t);
    record_header->flags = (operation == _MTB_KVSTORE_OPER_DELETE)
                            ? _MTB_KVSTORE_DELETE_FLAG
                            : _MTB_KVSTORE_NO_FLAG;
    record_header->key_size = strlen(key);
    record_header->data_size = data_size;
    record_header->crc = _mtb_kvstore_get_record_crc(record_header, key, data);
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_buffered_write
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_buffered_write(mtb_kvstore_t* obj,
                                             const uint8_t* data,
                                             uint32_t data_size,
                                             uint32_t* write_address,
                                             uint32_t* buffer_space_left,
                                             bool flush)
{
    CY_ASSERT(obj != NULL);
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint8_t* buffer_offset_ptr = obj->transaction_buffer +
                                 (obj->transaction_buffer_size - *buffer_space_left);
    const uint8_t* current_data_ptr = data;
    uint32_t remaining_size = data_size;
    while (remaining_size > 0)
    {
        uint32_t transfer_size =
            (*buffer_space_left >= remaining_size) ? remaining_size : *buffer_space_left;
        memcpy(buffer_offset_ptr, current_data_ptr, transfer_size);
        *buffer_space_left -= transfer_size;
        buffer_offset_ptr += transfer_size;
        remaining_size -= transfer_size;
        current_data_ptr += transfer_size;
        if (*buffer_space_left == 0)
        {
            result = obj->bd->program(obj->bd->context, *write_address,
                                      obj->transaction_buffer_size,
                                      obj->transaction_buffer);
            if (result != CY_RSLT_SUCCESS)
            {
                return result;
            }
            *buffer_space_left = obj->transaction_buffer_size;
            buffer_offset_ptr = obj->transaction_buffer;
            *write_address += obj->transaction_buffer_size;
        }
    }

    if ((*buffer_space_left != obj->transaction_buffer_size) && flush)
    {
        uint32_t prog_size = obj->bd->program_size(obj->bd->context, *write_address);
        uint32_t size = _mtb_kvstore_align_up(obj->transaction_buffer_size - *buffer_space_left,
                                              prog_size);
        result = obj->bd->program(obj->bd->context, *write_address,
                                  size,
                                  obj->transaction_buffer);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }
        *buffer_space_left = obj->transaction_buffer_size;
    }
    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_write_record
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_write_record(mtb_kvstore_t* obj,
                                           uint32_t area_address,
                                           uint32_t offset,
                                           const char* key,
                                           const uint8_t* data,
                                           uint32_t data_size,
                                           _mtb_kvstore_operation_t operation,
                                           const _mtb_kvstore_update_ram_table_info_t* ram_tbl_info,
                                           const _mtb_kvstore_update_consumed_size_info_t* size_info)
{
    CY_ASSERT(obj != NULL);
    CY_ASSERT(key != NULL);
    cy_rslt_t result = CY_RSLT_SUCCESS;

    // Calculate write size
    uint32_t record_address = area_address + offset;
    size_t header_size = sizeof(_mtb_kvstore_record_header_t);
    uint32_t prog_size = obj->bd->program_size(obj->bd->context, record_address);

    // Ensure that the buffer is large enough
    CY_ASSERT(obj->transaction_buffer_size >= header_size);
    // Check that the address written to is aligned to program page boundary
    CY_ASSERT(_mtb_kvstore_is_aligned(area_address, prog_size));
    // The following transactions assume that the buffer size is aligned to the program
    // size. We do that in init but just to make sure we check it below.
    CY_ASSERT((obj->transaction_buffer_size % prog_size) == 0);

    // Setup the area header.
    _mtb_kvstore_record_header_t record_header;
    _mtb_kvstore_setup_record_header(key, data, data_size,
                                     _MTB_KVSTORE_FORMAT_VERSION,
                                     operation,
                                     &record_header);

    // Check that total size does not exceed size of area.
    uint32_t record_size = _mtb_kvstore_get_record_size(obj, record_address,
                                                        record_header.key_size,
                                                        record_header.data_size);
    CY_ASSERT((offset + record_size) <= _MTB_KVSTORE_AREA_SIZE(obj));
    CY_UNUSED_PARAMETER(prog_size);
    CY_UNUSED_PARAMETER(record_size);

    uint32_t buffer_space_left = obj->transaction_buffer_size;
    result = _mtb_kvstore_buffered_write(obj, (uint8_t*)&record_header, header_size,
                                         &record_address,
                                         &buffer_space_left, false);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    result = _mtb_kvstore_buffered_write(obj, (uint8_t*)key, record_header.key_size,
                                         &record_address, &buffer_space_left, false);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    result = _mtb_kvstore_buffered_write(obj, data, record_header.data_size, &record_address,
                                         &buffer_space_left, true);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    if (offset != _MTB_KVSTORE_AREA_HEADER_OFFSET)
    {
        CY_ASSERT(ram_tbl_info != NULL && size_info != NULL);
        // If we wrote the record successfully then update the ram table and consumed size
        _mtb_kvstore_update_ram_table(obj, operation, ram_tbl_info);
        _mtb_kvstore_update_consumed_size(obj, operation, size_info);
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_check_area_valid
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_check_area_valid(mtb_kvstore_t* obj, uint32_t area_address,
                                               uint16_t* version)
{
    CY_ASSERT(obj != NULL);
    _mtb_kvstore_record_header_t header;
    _mtb_kvstore_area_record_data_t area_header_data;
    uint32_t data_size = sizeof(_mtb_kvstore_area_record_data_t);
    cy_rslt_t result = _mtb_kvstore_read_record(obj, area_address, 0, &header,
                                                _mtb_kvstore_area_rec_key, true,
                                                (uint8_t*)&area_header_data, &data_size);
    if (result == CY_RSLT_SUCCESS)
    {
        *version = area_header_data.version;
    }
    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_write_area_record
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_write_area_record(mtb_kvstore_t* obj, uint32_t area_address,
                                                uint32_t area_version)
{
    CY_ASSERT(obj != NULL);

    _mtb_kvstore_area_record_data_t area_header_data;
    area_header_data.format_version = _MTB_KVSTORE_FORMAT_VERSION;
    area_header_data.version = area_version;

    return _mtb_kvstore_write_record(obj, area_address, _MTB_KVSTORE_AREA_HEADER_OFFSET,
                                     _mtb_kvstore_area_rec_key, (uint8_t*)&area_header_data,
                                     sizeof(_mtb_kvstore_area_record_data_t),
                                     _MTB_KVSTORE_OPER_ADD, NULL, NULL);
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_find_record_in_ram_table
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_find_record_in_ram_table(mtb_kvstore_t* obj, const char* key,
                                                       uint32_t* ram_table_idx, uint16_t* key_hash,
                                                       uint32_t* data_size)
{
    CY_ASSERT(obj != NULL);
    cy_rslt_t result = MTB_KVSTORE_ITEM_NOT_FOUND_ERROR;

    *key_hash = _mtb_kvstore_crc16((uint8_t*)key, strlen(key), _MTB_KVSTORE_CRC_INIT_VAL);

    for (*ram_table_idx = 0; *ram_table_idx < obj->num_entries; (*ram_table_idx)++)
    {
        mtb_kvstore_ram_table_entry_t entry = obj->ram_table[*ram_table_idx];
        if (*key_hash < entry.hash)
        {
            continue;
        }

        if (*key_hash > entry.hash)
        {
            result = MTB_KVSTORE_ITEM_NOT_FOUND_ERROR;
            break;
        }

        _mtb_kvstore_record_header_t header;
        result = _mtb_kvstore_read_record(obj, obj->active_area_addr, entry.offset, &header, key,
                                          true, NULL, data_size);
        // If there was a key mismatch then keep searching.
        if (result != MTB_KVSTORE_ITEM_NOT_FOUND_ERROR)
        {
            break;
        }
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_copy_record
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_copy_record(mtb_kvstore_t* obj,
                                          uint32_t src_area_addr,
                                          uint32_t src_offset,
                                          uint32_t dst_area_addr,
                                          uint32_t dst_offset,
                                          uint32_t* next_dst_offset)
{
    CY_ASSERT(obj != NULL);
    cy_rslt_t result = CY_RSLT_SUCCESS;

    uint32_t src_record_addr = src_area_addr + src_offset;
    uint32_t dst_record_addr = dst_area_addr + dst_offset;

    _mtb_kvstore_record_header_t header;
    uint32_t header_size = sizeof(_mtb_kvstore_record_header_t);
    // Read header for the record
    result = obj->bd->read(obj->bd->context, src_record_addr, header_size, (uint8_t*)&header);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    uint32_t record_size = _mtb_kvstore_get_record_size(obj, src_record_addr, header.key_size,
                                                        header.data_size);
    if (dst_offset + record_size > (_MTB_KVSTORE_AREA_SIZE(obj)))
    {
        return MTB_KVSTORE_STORAGE_FULL_ERROR;
    }

    uint32_t remaining_size = record_size;
    uint32_t read_addr = src_record_addr;
    uint32_t write_addr = dst_record_addr;

    while (remaining_size > 0)
    {
        uint32_t transfer_size =
            (obj->transaction_buffer_size >=
             remaining_size) ? remaining_size : obj->transaction_buffer_size;
        result = obj->bd->read(obj->bd->context, read_addr, transfer_size, obj->transaction_buffer);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }

        result = obj->bd->program(obj->bd->context, write_addr, transfer_size,
                                  obj->transaction_buffer);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }
        remaining_size -= transfer_size;
        read_addr += transfer_size;
        write_addr += transfer_size;
    }

    *next_dst_offset = dst_offset + record_size;

    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_garbage_collection
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_garbage_collection(mtb_kvstore_t* obj,
                                                 const _mtb_kvstore_record_info_t* record_info)
{
    CY_ASSERT(obj != NULL);
    cy_rslt_t result = CY_RSLT_SUCCESS;

    // If we need to update a record then that the new size fits the space remaining space.
    // Otherwise return area full error before copying over. We can do this because we track
    // the actual consumed size so we use that to check if there is enough space to accommodate
    // the updated record.
    if ((record_info != NULL) && (record_info->update_rec_info != NULL))
    {
        // Note that the consumed size is not yet updated so we need to subtract the old record size
        // while checking for space left.
        uint32_t total_size = obj->consumed_size - record_info->consumed_size_info.old_record_size +
                              record_info->consumed_size_info.new_record_size;
        if (total_size > _MTB_KVSTORE_AREA_SIZE(obj))
        {
            return MTB_KVSTORE_STORAGE_FULL_ERROR;
        }
    }

    /* Erase the GC area */
    result = _mtb_kvstore_erase_area(obj, obj->gc_area_addr);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    uint32_t dst_offset = _mtb_kvstore_get_area_header_record_size(obj, obj->gc_area_addr);
    for (uint32_t idx = 0; idx < obj->num_entries; idx++)
    {
        if ((record_info != NULL) && (idx == record_info->ram_tbl_idx))
        {
            continue;
        }

        uint32_t dst_next_offset;
        uint32_t src_offset = obj->ram_table[idx].offset;
        result = _mtb_kvstore_copy_record(obj, obj->active_area_addr, src_offset, obj->gc_area_addr,
                                          dst_offset, &dst_next_offset);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }

        obj->ram_table[idx].offset = dst_offset;
        dst_offset = dst_next_offset;
    }

    // We need to inject a record or delete a record in the case where there may not be enough space
    // to add a new record for an update or delete operation.
    if (record_info != NULL)
    {
        if (record_info->update_rec_info != NULL)
        {
            _mtb_kvstore_update_ram_table_info_t ram_tbl_info =
            {
                .ram_tbl_idx  = record_info->ram_tbl_idx,
                .entry.hash   = record_info->update_rec_info->key_hash,
                .entry.offset = dst_offset
            };

            result = _mtb_kvstore_write_record(obj, obj->gc_area_addr, dst_offset,
                                               record_info->update_rec_info->key,
                                               record_info->update_rec_info->data,
                                               record_info->update_rec_info->data_size,
                                               _MTB_KVSTORE_OPER_UPDATE,
                                               &ram_tbl_info, &record_info->consumed_size_info);
            if (result != CY_RSLT_SUCCESS)
            {
                return result;
            }

            dst_offset += record_info->consumed_size_info.new_record_size;
        }
        else
        {
            _mtb_kvstore_update_ram_table_info_t ram_tbl_info =
            {
                .ram_tbl_idx  = record_info->ram_tbl_idx,
                .entry.hash   = 0,
                .entry.offset = 0
            };
            _mtb_kvstore_update_ram_table(obj, _MTB_KVSTORE_OPER_DELETE, &ram_tbl_info);
            _mtb_kvstore_update_consumed_size(obj, _MTB_KVSTORE_OPER_DELETE,
                                              &record_info->consumed_size_info);
        }
    }

    obj->active_area_version++;
    result = _mtb_kvstore_write_area_record(obj, obj->gc_area_addr, obj->active_area_version);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    obj->free_space_offset = dst_offset;

    uint32_t new_gc_area_addr = obj->active_area_addr;
    obj->active_area_addr = obj->gc_area_addr;
    obj->gc_area_addr = new_gc_area_addr;

    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_build_ram_table
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_build_ram_table(mtb_kvstore_t* obj)
{
    CY_ASSERT(obj != NULL);
    cy_rslt_t result = CY_RSLT_SUCCESS;

    // Each key will occupy at least a page hence the max number of keys
    // that fit in the space would be area size / page size.
    obj->num_entries = 0;
    obj->max_entries = _MTB_KVSTORE_INIT_MAX_KEYS;
    obj->free_space_offset = _MTB_KVSTORE_AREA_SIZE(obj);

    //We initially allocate ram table for 32 entries
    obj->ram_table =
        (mtb_kvstore_ram_table_entry_t*)malloc(obj->max_entries *
                                               sizeof(mtb_kvstore_ram_table_entry_t));
    if (NULL == obj->ram_table)
    {
        return MTB_KVSTORE_MEM_ALLOC_ERROR;
    }

    // Start looking from the end of the area header.
    uint32_t record_size = _mtb_kvstore_get_area_header_record_size(obj, obj->active_area_addr);
    // Add area header size to consumed size.
    obj->consumed_size = record_size;

    uint32_t offset = record_size;
    while (offset + sizeof(_mtb_kvstore_record_header_t) < obj->free_space_offset)
    {
        _mtb_kvstore_record_header_t header;
        result = _mtb_kvstore_read_record(obj, obj->active_area_addr, offset, &header,
                                          obj->key_buffer, false, NULL, NULL);
        if (result != CY_RSLT_SUCCESS)
        {
            if (MTB_KVSTORE_ERASED_DATA_ERROR == result)
            {
                // This is a special case since it is expected that we stop populating
                // the ram table when we encounter free space.
                // Hence we return success even though read record return an error.
                result = CY_RSLT_SUCCESS;
            }
            else if (MTB_KVSTORE_INVALID_DATA_ERROR == result)
            {
                // If a corrupted record was found we run GC operation
                // which will copy all valid record until the current corrupted
                // record was encountered.
                result = _mtb_kvstore_garbage_collection(obj, NULL);
                // The above function will set the free space offset in the new area
                // so we directly return the result. The consumed size
                // is already updated in this loop and will not change even after the
                // GC operation.
                return result;
            }
            break;
        }

        // This should be safe as we allocate 1 extra byte in the key buffer than the max key size.
        obj->key_buffer[header.key_size] = '\0';

        uint32_t ram_tbl_idx;
        uint16_t hash;
        uint32_t old_record_data_size = 0;
        result = _mtb_kvstore_find_record_in_ram_table(obj, obj->key_buffer, &ram_tbl_idx, &hash,
                                                       &old_record_data_size);
        if ((result != CY_RSLT_SUCCESS) && (result != MTB_KVSTORE_ITEM_NOT_FOUND_ERROR))
        {
            break;
        }

        uint32_t curr_offset = offset;
        // Add the current record size to the offset to set the next offset.
        record_size = _mtb_kvstore_get_record_size(obj, obj->active_area_addr + curr_offset,
                                                   header.key_size, header.data_size);
        offset += record_size;

        bool delete = (header.flags & _MTB_KVSTORE_DELETE_FLAG) != 0;
        bool found_in_table = (result != MTB_KVSTORE_ITEM_NOT_FOUND_ERROR);

        // If key was not found in the RAM table and is marked for deletion
        // then do nothing.
        if (delete && !found_in_table)
        {
            continue;
        }

        _mtb_kvstore_operation_t operation = (delete)
                                            ? _MTB_KVSTORE_OPER_DELETE
                                            : (found_in_table) ? _MTB_KVSTORE_OPER_UPDATE :
                                             _MTB_KVSTORE_OPER_ADD;

        // If we have to add an entry to the ram table then check
        // if we need to increment keys.
        if ((operation == _MTB_KVSTORE_OPER_ADD) && (obj->num_entries >= obj->max_entries))
        {
            result = _mtb_kvstore_increment_max_keys(obj);
            if (result != CY_RSLT_SUCCESS)
            {
                break;
            }
        }

        _mtb_kvstore_update_ram_table_info_t ram_tbl_info =
        {
            .ram_tbl_idx  = ram_tbl_idx,
            .entry.hash   = hash,
            .entry.offset = curr_offset
        };
        _mtb_kvstore_update_ram_table(obj, operation, &ram_tbl_info);

        uint32_t old_record_size = (operation == _MTB_KVSTORE_OPER_ADD)
                                ? 0
                                : _mtb_kvstore_get_record_size(obj, obj->active_area_addr, strlen(
                                                                   obj->key_buffer),
                                                               old_record_data_size);
        _mtb_kvstore_update_consumed_size_info_t size_info =
        {
            .old_record_size = old_record_size,
            .new_record_size = record_size
        };
        _mtb_kvstore_update_consumed_size(obj, operation, &size_info);


        result = CY_RSLT_SUCCESS;
    }

    obj->free_space_offset = offset;

    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_setup_areas
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_setup_areas(mtb_kvstore_t* obj)
{
    CY_ASSERT(obj != NULL);
    cy_rslt_t result = CY_RSLT_SUCCESS;

    // Divide the space into 2 equal sizes.
    uint32_t area1_start_addr = obj->start_addr;
    uint32_t area2_start_addr = obj->start_addr + _MTB_KVSTORE_AREA_SIZE(obj);

    bool area1_valid;
    bool area2_valid;
    uint16_t area1_version;
    uint16_t area2_version;

    // Read area 1 header
    cy_rslt_t area_valid_result = _mtb_kvstore_check_area_valid(obj, area1_start_addr,
                                                                &area1_version);
    if ((CY_RSLT_SUCCESS != area_valid_result) &&
        (MTB_KVSTORE_ERASED_DATA_ERROR != area_valid_result) &&
        (MTB_KVSTORE_INVALID_DATA_ERROR != area_valid_result) &&
        (MTB_KVSTORE_ITEM_NOT_FOUND_ERROR != area_valid_result))
    {
        return area_valid_result;
    }
    area1_valid = (CY_RSLT_SUCCESS == area_valid_result);

    area_valid_result = _mtb_kvstore_check_area_valid(obj, area2_start_addr, &area2_version);
    if ((CY_RSLT_SUCCESS != area_valid_result) &&
        (MTB_KVSTORE_ERASED_DATA_ERROR != area_valid_result) &&
        (MTB_KVSTORE_INVALID_DATA_ERROR != area_valid_result) &&
        (MTB_KVSTORE_ITEM_NOT_FOUND_ERROR != area_valid_result))
    {
        return area_valid_result;
    }
    area2_valid = (CY_RSLT_SUCCESS == area_valid_result);

    // If both are valid, set the one area whose master record has the
    // higher version as active_area. Erase first sector of the other one.
    if (area1_valid && area2_valid)
    {
        // The versions should never be equal.
        CY_ASSERT(area1_version != area2_version);

        // Check if the area1 version is greater or 0 in case of wrap around.
        if ((area1_version > area2_version) || (area1_version == 0))
        {
            obj->active_area_addr = area1_start_addr;
            obj->active_area_version = area1_version;
            obj->gc_area_addr = area2_start_addr;
        }
        else
        {
            obj->active_area_addr = area2_start_addr;
            obj->active_area_version = area2_version;
            obj->gc_area_addr = area1_start_addr;
        }
    }
    // If one is valid, set its area as active_area.
    else if (area1_valid)
    {
        obj->active_area_addr = area1_start_addr;
        obj->active_area_version = area1_version;
        obj->gc_area_addr = area2_start_addr;
    }
    else if (area2_valid)
    {
        obj->active_area_addr = area2_start_addr;
        obj->active_area_version = area2_version;
        obj->gc_area_addr = area1_start_addr;
    }
    // If none are valid, set area1 as active_area, and program area record
    //with version 1.
    else
    {
        // Erase first sector of area 1 to be able to write the header.
        result = _mtb_kvstore_erase_area(obj, area1_start_addr);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }

        // Write the area header into the area 1.
        result = _mtb_kvstore_write_area_record(obj, area1_start_addr,
                                                _MTB_KVSTORE_INITIAL_AREA_VERSION);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }
        obj->active_area_addr = area1_start_addr;
        obj->active_area_version = _MTB_KVSTORE_INITIAL_AREA_VERSION;
        obj->gc_area_addr = area2_start_addr;
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// _mtb_kvstore_write_with_flags
//--------------------------------------------------------------------------------------------------
static cy_rslt_t _mtb_kvstore_write_with_flags(mtb_kvstore_t* obj, const char* key,
                                               const uint8_t* data,
                                               uint32_t size, bool delete)
{
    CY_ASSERT(obj != NULL);
    cy_rslt_t result = CY_RSLT_SUCCESS;

    uint32_t ram_tbl_idx;
    uint16_t hash;
    uint32_t old_record_data_size = 0;
    result = _mtb_kvstore_find_record_in_ram_table(obj, key, &ram_tbl_idx, &hash,
                                                   &old_record_data_size);
    if ((result != CY_RSLT_SUCCESS) && (result != MTB_KVSTORE_ITEM_NOT_FOUND_ERROR))
    {
        return result;
    }

    bool found_in_table = (result != MTB_KVSTORE_ITEM_NOT_FOUND_ERROR);

    // If we are trying to delete a record and it is not found in the RAM table then it
    // is already been removed or does not exist. Hence we return success.
    if (delete && !found_in_table)
    {
        return CY_RSLT_SUCCESS;
    }

    _mtb_kvstore_operation_t operation = (delete)
                                        ? _MTB_KVSTORE_OPER_DELETE
                                        : (found_in_table) ? _MTB_KVSTORE_OPER_UPDATE :
                                         _MTB_KVSTORE_OPER_ADD;

    // We will be adding a new entry if its not found in the table so
    // check if max keys need to be expanded before we write anything
    // to flash.
    if ((operation == _MTB_KVSTORE_OPER_ADD) && (obj->num_entries >= obj->max_entries))
    {
        result = _mtb_kvstore_increment_max_keys(obj);
        if (result != CY_RSLT_SUCCESS)
        {
            return result;
        }
    }

    // Check if space enough for KV record. If not run GC
    uint32_t record_size = _mtb_kvstore_get_record_size(obj, obj->active_area_addr, strlen(
                                                            key), size);
    uint32_t old_record_size = (operation == _MTB_KVSTORE_OPER_ADD)
                                ? 0
                                : _mtb_kvstore_get_record_size(obj, obj->active_area_addr,
                                                               strlen(key),
                                                               old_record_data_size);

    if (((operation == _MTB_KVSTORE_OPER_UPDATE) || (operation == _MTB_KVSTORE_OPER_ADD)) &&
        ((obj->consumed_size - old_record_size + record_size) > _MTB_KVSTORE_AREA_SIZE(obj)))
    {
        return MTB_KVSTORE_STORAGE_FULL_ERROR;
    }

    if (obj->free_space_offset + record_size > _MTB_KVSTORE_AREA_SIZE(obj))
    {
        // If we need to update or delete a key and we do not enough space left. We can do the
        // update or
        // deletion when we run garbage collection by removing the value from the ram table before
        // GC operation
        // and injecting new updated entry if it is a update operation.
        _mtb_kvstore_record_info_t record_info;
        _mtb_kvstore_update_record_info_t update_rec;

        if ((operation == _MTB_KVSTORE_OPER_DELETE) || (operation == _MTB_KVSTORE_OPER_UPDATE))
        {
            record_info.ram_tbl_idx = ram_tbl_idx;
            record_info.consumed_size_info.new_record_size = record_size;
            record_info.consumed_size_info.old_record_size = old_record_size;
        }

        if (operation == _MTB_KVSTORE_OPER_DELETE)
        {
            record_info.update_rec_info = NULL;
        }
        else if (operation == _MTB_KVSTORE_OPER_UPDATE)
        {
            update_rec.key = key;
            update_rec.data = data;
            update_rec.data_size = size;
            update_rec.key_hash = hash;

            record_info.update_rec_info = &update_rec;
        }

        result = _mtb_kvstore_garbage_collection(obj,
                                                 (operation == _MTB_KVSTORE_OPER_ADD)
                                                  ? NULL
                                                  : &record_info);
        if ((result != CY_RSLT_SUCCESS) || found_in_table)
        {
            return result;
        }
    }

    // We check that we have enough space earlier so when we get here we must
    // have enough space.
    CY_ASSERT(obj->free_space_offset + record_size <= _MTB_KVSTORE_AREA_SIZE(obj));

    _mtb_kvstore_update_ram_table_info_t ram_tbl_info =
    {
        .ram_tbl_idx  = ram_tbl_idx,
        .entry.hash   = hash,
        .entry.offset = obj->free_space_offset
    };

    _mtb_kvstore_update_consumed_size_info_t size_info =
    {
        .old_record_size = old_record_size,
        .new_record_size = record_size
    };

    result = _mtb_kvstore_write_record(obj, obj->active_area_addr, obj->free_space_offset,
                                       key, data, size, operation, &ram_tbl_info,
                                       &size_info);
    if (result == CY_RSLT_SUCCESS)
    {
        obj->free_space_offset += record_size;
    }

    return result;
}


/**************************************** PUBLIC API ******************************************/

//--------------------------------------------------------------------------------------------------
// mtb_kvstore_init
//--------------------------------------------------------------------------------------------------
cy_rslt_t mtb_kvstore_init(mtb_kvstore_t* obj, uint32_t start_addr, uint32_t length,
                           const mtb_kvstore_bd_t* block_device)
{
    if ((NULL == obj) || (NULL == block_device) || (length == 0))
    {
        return MTB_KVSTORE_BAD_PARAM_ERROR;
    }

    // Check if start addr and start addr + length align with erase sector size
    uint32_t erase_size = block_device->erase_size(block_device->context, start_addr);
    if (!_mtb_kvstore_is_aligned(start_addr,
                                 erase_size) ||
        !_mtb_kvstore_is_aligned(start_addr + length, erase_size))
    {
        return MTB_KVSTORE_ALIGNMENT_ERROR;
    }

    // Check that the storage does not have 0 sectors and has even number
    // of erase sectors.
    uint32_t num_erase_sectors = (length / erase_size);
    if ((num_erase_sectors == 0) || ((num_erase_sectors > 0) && ((num_erase_sectors & 1) != 0)))
    {
        return MTB_KVSTORE_ALIGNMENT_ERROR;
    }

    cy_rslt_t result = CY_RSLT_SUCCESS;

    memset(obj, 0, sizeof(mtb_kvstore_t));

    // Init Mutex
    result = _mtb_kvstore_initlock(obj);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    // Get Mutex
    result = _mtb_kvstore_lock(obj);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    uint32_t prog_size = block_device->program_size(block_device->context, start_addr);
    uint32_t read_size = block_device->read_size(block_device->context, start_addr);
    uint32_t buffer_size = (prog_size >= read_size) ? prog_size : read_size;
    if (buffer_size < _MTB_KVSTORE_MIN_BUFF_SIZE)
    {
        buffer_size = _mtb_kvstore_align_up(_MTB_KVSTORE_MIN_BUFF_SIZE, prog_size);
    }

    obj->transaction_buffer = (uint8_t*)malloc(buffer_size);
    if (obj->transaction_buffer != NULL)
    {
        obj->transaction_buffer_size = buffer_size;
        obj->bd = block_device;
        obj->start_addr = start_addr;
        obj->length = length;

        if (result == CY_RSLT_SUCCESS)
        {
            result = _mtb_kvstore_setup_areas(obj);
            if (result == CY_RSLT_SUCCESS)
            {
                result = _mtb_kvstore_build_ram_table(obj);
            }
        }
    }
    else
    {
        /* Not enough heap available to allocate work buffer */
        result = MTB_KVSTORE_MEM_ALLOC_ERROR;
    }

    // Release Mutex
    _mtb_kvstore_unlock(obj);

    if (result != CY_RSLT_SUCCESS)
    {
        mtb_kvstore_deinit(obj);
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// mtb_kvstore_write
//--------------------------------------------------------------------------------------------------
cy_rslt_t mtb_kvstore_write(mtb_kvstore_t* obj, const char* key, const uint8_t* data,
                            uint32_t size)
{
    if (!_mtb_kvstore_is_valid_key(key) || ((data == NULL) && (size != 0)))
    {
        return MTB_KVSTORE_BAD_PARAM_ERROR;
    }

    cy_rslt_t result = _mtb_kvstore_lock(obj);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    result = _mtb_kvstore_write_with_flags(obj, key, data, size, 0);

    _mtb_kvstore_unlock(obj);

    return result;
}


//--------------------------------------------------------------------------------------------------
// mtb_kvstore_read
//--------------------------------------------------------------------------------------------------
cy_rslt_t mtb_kvstore_read(mtb_kvstore_t* obj, const char* key, uint8_t* data,
                           uint32_t* size)
{
    if (!_mtb_kvstore_is_valid_key(key))
    {
        return MTB_KVSTORE_BAD_PARAM_ERROR;
    }

    // If data buffer is passed but size is NULL or 0;
    if ((data != NULL) && ((size == NULL) || (*size == 0)))
    {
        return MTB_KVSTORE_BAD_PARAM_ERROR;
    }

    cy_rslt_t result = _mtb_kvstore_lock(obj);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    uint32_t ram_tbl_idx;
    uint16_t hash;
    result = _mtb_kvstore_find_record_in_ram_table(obj, key, &ram_tbl_idx, &hash, NULL);
    if (result == CY_RSLT_SUCCESS)
    {
        _mtb_kvstore_record_header_t header;
        result = _mtb_kvstore_read_record(obj, obj->active_area_addr,
                                          obj->ram_table[ram_tbl_idx].offset, &header, key, true,
                                          data, size);
    }

    _mtb_kvstore_unlock(obj);

    return result;
}


//--------------------------------------------------------------------------------------------------
// mtb_kvstore_delete
//--------------------------------------------------------------------------------------------------
cy_rslt_t mtb_kvstore_delete(mtb_kvstore_t* obj, const char* key)
{
    cy_rslt_t result = _mtb_kvstore_lock(obj);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    result = _mtb_kvstore_write_with_flags(obj, key, NULL, 0, _MTB_KVSTORE_DELETE_FLAG);

    _mtb_kvstore_unlock(obj);

    return result;
}


//--------------------------------------------------------------------------------------------------
// mtb_kvstore_reset
//--------------------------------------------------------------------------------------------------
cy_rslt_t mtb_kvstore_reset(mtb_kvstore_t* obj)
{
    cy_rslt_t result = _mtb_kvstore_lock(obj);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    // Clear the RAM table
    memset(obj->ram_table, 0, obj->max_entries * sizeof(mtb_kvstore_ram_table_entry_t));
    obj->num_entries = 0;

    // Run GC.
    result = _mtb_kvstore_garbage_collection(obj, NULL);
    if (result == CY_RSLT_SUCCESS)
    {
        obj->consumed_size = obj->free_space_offset;
    }

    _mtb_kvstore_unlock(obj);

    return result;
}


//--------------------------------------------------------------------------------------------------
// mtb_kvstore_deinit
//--------------------------------------------------------------------------------------------------
void mtb_kvstore_deinit(mtb_kvstore_t* obj)
{
    _mtb_kvstore_lock_wait_forever(obj);

    if (obj->transaction_buffer != NULL)
    {
        free(obj->transaction_buffer);
    }

    if (obj->ram_table != NULL)
    {
        free(obj->ram_table);
    }

    #if defined(CY_RTOS_AWARE) || defined(COMPONENT_RTOS_AWARE)
    cy_mutex_t local_mutex = obj->mtb_kvstore_mutex;
    #endif

    _mtb_kvstore_unlock(obj);

    #if defined(CY_RTOS_AWARE) || defined(COMPONENT_RTOS_AWARE)
    cy_rslt_t result = cy_rtos_deinit_mutex(&local_mutex);
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    CY_UNUSED_PARAMETER(result);
    #endif
}


//--------------------------------------------------------------------------------------------------
// mtb_kvstore_size
//--------------------------------------------------------------------------------------------------
uint32_t mtb_kvstore_size(mtb_kvstore_t* obj)
{
    return obj->consumed_size;
}


//--------------------------------------------------------------------------------------------------
// mtb_kvstore_remaining_size
//--------------------------------------------------------------------------------------------------
uint32_t mtb_kvstore_remaining_size(mtb_kvstore_t* obj)
{
    return _MTB_KVSTORE_AREA_SIZE(obj) - obj->consumed_size;
}
