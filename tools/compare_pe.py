"""Compare a freshly built Noclip.dll / NoclipLauncher.exe with the released one, section by section.

    python -m pip install pefile
    python tools/compare_pe.py build/Noclip.dll path/to/released/Noclip.dll

Every link writes a new time stamp (PE header and debug directory) and a new build ID (the 16-byte GUID of the
debug record), so two builds of the same source always differ in those 20 bytes. Everything else (code, data,
imports, relocations) must be identical."""
import sys
import pefile

a_path, b_path = sys.argv[1], sys.argv[2]
a, b = open(a_path, 'rb').read(), open(b_path, 'rb').read()
print('sizes:', len(a), len(b))
if len(a) != len(b):
    sys.exit('different sizes: not the same build')
pe = pefile.PE(data=a)
diff = [i for i in range(len(a)) if a[i] != b[i]]
first = pe.sections[0].PointerToRawData
header = [i for i in diff if i < first]
print('differing bytes in the PE header: %d (the link time stamp is 4 of them)' % len(header))
for s in pe.sections:
    lo, hi = s.PointerToRawData, s.PointerToRawData + s.SizeOfRawData
    n = sum(1 for i in diff if lo <= i < hi)
    print('  %-9s %7d bytes, %d differ' % (s.Name.rstrip(b'\0').decode(), s.SizeOfRawData, n))
debug = []
for d in getattr(pe, 'DIRECTORY_ENTRY_DEBUG', []):
    debug.append((d.struct.get_file_offset(), d.struct.get_file_offset() + d.struct.sizeof()))
    if d.struct.PointerToRawData:
        debug.append((d.struct.PointerToRawData, d.struct.PointerToRawData + d.struct.SizeOfData))
rest = [i for i in diff if i >= first and not any(lo <= i < hi for lo, hi in debug)]
print('differing bytes outside the header and the debug directory/record: %d' % len(rest))
print('SAME CODE AND DATA' if not rest and len(header) <= 4 else 'DIFFERENT')
