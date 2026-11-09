# Trunking codeplug schema (analysis of a reference codeplug)

Reference used only for analysis; no codeplug is shipped with OpenDepot.
Goal: generate a codeplug programmatically (record constructors in code).

The trunking codeplug = the conventional record set + a SmartZone system record
group. FLASHcode example: 500008-000488-2 (Q806, H14, H38, Q173).

## Records present only in the trunking codeplug
(tag, len) and inferred role from the reference:

| tag | len | role (inferred) |
|---|---|---|
| 0x0015 | 34 | system/profile flags block |
| 0x0017 | 12 | system id/flags |
| 0x001a | 7  | small system flag |
| 0x0022 | 24 | system id list |
| 0x003a | 218| **system definition** (SmartZone system, large block) |
| 0x003b | 25 | per-system site/flag array |
| 0x003c | 114| per-system site/flag array (larger) |
| 0x003d | 25 | per-system site/flag array |
| 0x003e | 31 | system option bytes |
| 0x003f | 21 | system option bytes |
| 0x004d | 27 | **list entry** ("LIST 1") |
| 0x004e | 27 | **list entry** ("LIST 1") |
| 0x0f9f| 14 | system ref/flags |
| 0x0fa0| 14 | system ref/flags |
| 0x0fa8| 34 | personality/flags |
| 0x0fa9| 26 | personality/flags |
| 0x0fd0| 170| **personality table** (repeating 10-byte entries) |
| 0x0fd1| 19 | personality flags |
| 0x0fd2| 13 | talkgroup id/flags |
| 0x0fd3| 19 | talkgroup -> 0x0fd2 ref |
| 0x0fd4| 19 | status id/flags |
| 0x0fd5| 30 | status -> 0x0fd4 ref |
| 0x0fd6| 27 | **talkgroup name** "MESSAGE 1" |
| 0x0fd7| 9  | talkgroup value |
| 0x0fd8| 18 | ref 0x0fd7 + 0x0fd6 |
| 0x0fd9| 27 | **talkgroup name** "STATUS 1" |
| 0x0fda| 9  | talkgroup value |
| 0x0fdb| 18 | ref 0x0fda + 0x0fd9 |
| 0x0fdc| 27 | **talkgroup name** "SITE 1" |
| 0x0fdd| 17 | site value |
| 0x0fde| 18 | ref 0x0fdd + 0x0fdc |
| 0x0fdf| 13 | call/id flags |
| 0x0fe0| 18 | ref 0x0fdf |
| 0x0fe1| 15 | talkgroup entry |
| 0x0fe2| 13 | ref 0x0fe1 + 0x0f... |
| 0x0fe3| 4  | empty entry |
| 0x0fe4| 35 | refs bundle (0x0fe3,0x0fe7,0x0fe2,0x0feb,0x0fd5,0x0fd1) |
| 0x0fe5| 20 | flags |
| 0x0fe6| 45 | system detail block |
| 0x0fe7| 136| **system detail** (refs to 0x0fe6,0x0fe5,0x0fe0,0x0fd0,...) |
| 0x0fe8| 25 | **talkgroup name** "CALL 1" |
| 0x0fe9| 14 | call value |
| 0x0fea| 12 | flags |
| 0x0feb| 26 | ref 0x0fea + 0x0fe9 |
| 0x0fec| 25 | "CALL 1" (dup) |
| 0x0fed| 14 | call value |
| 0x0fee| 11 | flags |
| 0x0fef| 21 | ref 0x0fee + 0x0fed |

Records removed vs conventional: 0x0052, 0x0053, 0x0fb4/0x0fb5, 0x0fbe,
0x0fcb/0x0fcc/0x0fcd* (conventional personalities/channels replaced by trunking ones).

## Generation approach
- Build header + identity (0x1004/0x1005/0x1006) from the model/serial/FLASHcode.
- Build config/directory (0x0047 etc.) from defaults.
- Build a conventional zone/channel by default.
- For trunking: emit 0x003a system + 0x0fd0 personality table + talkgroup records
  (MESSAGE/STATUS/SITE/CALL) with user-chosen name/system id/frequencies.
- All values constructed in code (no embedded codeplug blob); reference only informs
  field offsets/lengths.

Next: implement `NewCodeplug(CreateOptions, Codeplug&)` constructors per record,
then verify by writing to the test radio and reading back.
