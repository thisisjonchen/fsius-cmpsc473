# CMPSC473 Honors Option Project - File System in User Space

This project implements an ISO9660-based file system that includes an ISO parser and an ISO generator.

## TODO
* [ ] ISO Generator
  * [ ] Example Directory Layout for Presentation and Parsing
  * [ ] `mkisofs` command (-r for Rock Ridge ext., -J for Joliet)
* [ ] ISO Parser
  * [ ] open(path)
  * [ ] close(iso)
  * [ ] read_sector(iso, sector, count, buf)
  * [ ] parse_pvd(iso)
  * [ ] list_dir(iso, path, entries)
  * [ ] read_file(path, buf, size)
  * [ ] resolve_path(path, out_record)
  * [ ] parse_dir_record(data, offset, out_record)
  * [ ] ...misc. helpers
* [ ] ISO Wrapper (interface for using Parser and Generator)
  * [ ] Build Generator
  * [ ] Build Parser
   

## References
- [https://wiki.osdev.org/FAT#Creating_a_fresh_FAT_filesystem](https://wiki.osdev.org/ISO_9660)
