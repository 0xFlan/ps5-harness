---
name: file-audit
description: Audit one directory on the console and report its contents, sizes and any refusals using the read-only filesystem tools.
---

# File audit

Use this skill when the user asks what is taking space, what changed, or what a
directory contains.

1. List the target directory with `fs_list`, one level at a time. Do not assume
   a directory exists.
2. Read only small text files with `fs_read` when their contents matter. Skip
   binaries and anything marked denied.
3. Summarise: total visible files, the largest entries, and anything the tools
   refused to read. Name exact paths from the tool results.
4. Write the report only if the user asks for a file. If they do, follow the
   `report-template` resource and place it under a write root with `fs_write`.

Never try to reach around a refusal by changing paths or using another tool.
