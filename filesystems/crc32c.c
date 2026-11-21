/*
 * Table-based CRC-32C/Castagnoli checksum calculation. This polynomial is
 * distinct from ordinary GPT CRC-32 and must not be substituted for it.
 * An on-disk checksum detects data inconsistency, not authenticated origin.
 */
 



 

















static uint32_t crc32c_table [256];

static void
init_crc32c_table (void)
{
  auto uint32_t reflect (uint32_t ref, int len);
  uint32_t reflect (uint32_t ref, int len)
    {
      uint32_t result = 0;
      int i;

      for (i = 1; i <= len; i++)
        {
          if (ref & 1)
            result |= 1 << (len - i);
          ref >>= 1;
        }

      return result;
    }

  static int crc32c_table_inited;
  if(crc32c_table_inited)
	  return;
  crc32c_table_inited = 1;

  uint32_t polynomial = 0x1edc6f41;
  int i, j;

  for(i = 0; i < 256; i++)
    {
      crc32c_table[i] = reflect(i, 8) << 24;
      for (j = 0; j < 8; j++)
        crc32c_table[i] = (crc32c_table[i] << 1) ^
            (crc32c_table[i] & (1 << 31) ? polynomial : 0);
      crc32c_table[i] = reflect(crc32c_table[i], 32);
    }
}

uint32_t
/*
 * Calculates CRC-32C over an exact caller-provided buffer and length.
 * The caller must compare against the appropriate filesystem field.
 */
grub_getcrc32c (uint32_t crc, const void *buf, int size)
{
  int i;
  const uint8_t *data = buf;

  if (! crc32c_table[1])
    init_crc32c_table ();
  /*
   * Complement on entry and exit permits incremental hashing with the
   * previous return value as the next seed. The Btrfs directory lookup also
   * applies its name-key convention to the result.
   */
  crc^= 0xffffffff;

  for (i = 0; i < size; i++)
    {
      crc = (crc >> 8) ^ crc32c_table[(crc & 0xFF) ^ *data];
      data++;
    }

  return crc ^ 0xffffffff;
}
