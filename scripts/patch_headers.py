from pathlib import Path

bloom_path = Path(r'C:/workspace/stratadb/include/stratadb/bloom_filter.h')
text = bloom_path.read_text()
text = text.replace('BloomFilter(size_t bits_per_key = 10);', 'BloomFilter(size_t bits_per_key);')
bloom_path.write_text(text)

sstable_path = Path(r'C:/workspace/stratadb/include/stratadb/sstable.h')
text = sstable_path.read_text()
text = text.replace('#include "stratadb/bloom_filter.h"\n#include "stratadb/wal.h"', '#include "stratadb/bloom_filter.h"\n#include "stratadb/wal.h"\n#include <memory>')
sstable_path.write_text(text)
