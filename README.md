# CMPSC473 Honors Option Project - File System in User Space

This project implements an ISO9660-based file system that includes an ISO parser and an ISO generator.

## TODO
* [x] ISO Generator
  * [x] Example Directory Layout for Presentation and Parsing
  * [x] `mkisofs` command (-r for Rock Ridge ext., -J for Joliet)
* [x] ISO Parser
  * [x] fs_open(path)
  * [x] fs_close()
  * [x] read_sector(sector, count, buf)
  * [x] parse_pvd()
  * [x] list_dir(path, entries)
  * [x] read_file(path, buf, size)
  * [x] resolve_path(path, out_record)
  * [x] parse_dir_record(data, offset, out_record)
  * [x] ...misc. helpers (`fs_stat`, `iso_join_path`, `iso_normalize_path`, `iso_canonicalize_path`, case-insensitive name matching)
* [x] ISO Wrapper (interface for using Parser and Generator)
  * [x] Build Generator (`libgenerator.a`, wraps `genisoimage`/`mkisofs`)
  * [x] Build Parser (`libparser.a`)
* [x] Joliet Filename Extensions - Jonathan
* [x] Rock Ridge Extensions w/ Permissions - Binay


## References
- [OSDev: ISO 9660](https://wiki.osdev.org/ISO_9660)
- [Microsoft Joliet Spec](https://pismotec.com/cfs/jolspec.html)
## Installation & Usage

### Prerequisites
- GCC
- Make
- `genisoimage` (or `mkisofs`)

**Ubuntu / WSL:**
```bash
sudo apt-get update && sudo apt-get install -y gcc make genisoimage
```

### Build
```bash
make
```

### Generate a test ISO
Either directly:
```bash
genisoimage -input-charset utf-8 -r -J -V  "TEST_FS" -o fs.iso test_fs/
```
...or from inside the CLI (uses the same flags under the hood):
```
> mkiso test_fs fs.iso TEST_FS
```

### Run
```bash
./driver
```
Then at the prompt:
```
> mount fs.iso
  Detected: ISO9660, Rock Ridge, Joliet (UCS level 3)
  Active mode: rock-ridge
(fs.iso) / > ls
(fs.iso) / > cd documents
(fs.iso) /documents > cat todo-list.txt
(fs.iso) /documents > stat todo-list.txt
(fs.iso) /documents > mode joliet     # switch naming tree at runtime
(fs.iso) / > unmount
> exit
```
Type `help` for the full command list.
