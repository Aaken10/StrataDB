from pathlib import Path
p = Path('C:/workspace/stratadb/include/stratadb/sstable.h')
text = p.read_text()
text = text.replace('#include <memory>\r\n#include <memory>', '#include <memory>\r\n')
p.write_text(text)
