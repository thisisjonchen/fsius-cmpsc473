# ISO9660 Test Data Directory Tree

Example directory layout for presentation and parsing (per TODO: Example Directory Layout for Presentation and Parsing).

```
test_data/
│
├── README.txt (319 B)
│   └── Project overview and description
│
├── documents/
│   ├── report.txt (278 B)
│   │   └── Multi-section formatted document
│   ├── notes.txt (113 B)
│   │   └── Simple meeting notes
│   ├── long_filename_test_file.txt (32 B)
│   │   └── Tests long filename handling
│   └── empty.txt (0 B)
│       └── Edge case: zero-byte file
│
├── programs/
│   ├── hello.c (146 B)
│   │   └── Simple C program source
│   └── Makefile (91 B)
│       └── Build instructions
│
├── images/
│   ├── photo1.bin (10 KB)
│   │   └── Binary data (large)
│   └── photo2.bin (5 KB)
│       └── Binary data (medium)
│
├── config/
│   └── settings/
│       └── app.conf (84 B)
│           └── Application configuration
│
└── data/
    └── logs/
        └── system.log (215 B)
            └── System log file
```

## Statistics

| Metric | Value |
|--------|-------|
| Total directories | 6 |
| Total files | 11 |
| Deepest nesting | 3 levels |
| Total size | ~16 KB (data only) |
| File types | .txt, .c, .conf, .log, .bin, Makefile |

## File Categories

**Text Files**
- README.txt — Main documentation
- documents/*.txt — Various text files with different content
- config/settings/app.conf — Configuration file format
- data/logs/system.log — Log file format

**Source Code**
- programs/hello.c — Simple C program
- programs/Makefile — Build instructions

**Binary Files**
- images/*.bin — Binary data (simulates images/media)

**Edge Cases**
- empty.txt — Zero-byte file
- long_filename_test_file.txt — Tests filename length handling
- Nested directories — Tests deep directory traversal (3 levels)

## Test Scenarios

### For ISO Generator
- Basic creation — Convert entire test_data to ISO
- Rock Ridge (-r) — Preserve Unix permissions and long names
- Joliet (-J) — Windows compatibility with Unicode names
- Volume label — Test custom volume names

### For ISO Parser
- PVD parsing — Read Primary Volume Descriptor
- Root directory — List top-level contents
- Nested directories — Traverse config/settings and data/logs
- File extraction — Extract specific files (hello.c, report.txt)
- Binary files — Handle non-text data correctly
- Empty files — Parse zero-byte files
- Path resolution — Find files by full path
- Metadata — Read file sizes, dates (if supported)

## Testing Commands

**Create ISO:**
```bash
genisoimage -r -J -V "TEST_DATA" -o test.iso test_data/
```

**Expected ISO Size:** Approximately 30–50 KB (depends on ISO overhead)

**Validation Points:**
- Total files: 11
- Total directories: 6 (including test_data root)
- Deepest nesting: 3 levels (test_data/config/settings/app.conf)
- Largest file: photo1.bin (10 KB)
- Smallest file: empty.txt (0 bytes)

## Notes

- All text files use Unix line endings (LF)
- Binary files contain random data (not actual images)
- Directory structure tests both shallow and deep nesting
- Filenames test both short (8.3 DOS) and long name support
