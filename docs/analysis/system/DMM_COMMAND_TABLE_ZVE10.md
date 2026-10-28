# ZV-E10 DMM 命令分发表（从 dmm.ko 的 .rodata 重定位还原）

> 来源：`dmm.ko` 的 `.rel.rodata`，表区 `0x80..0x168`，每项 4 字节函数指针。
> **与 a7c2 的 `dmm-service.c` 协议不同** —— 那边是 `0x20..0x23` + magic `0x63A`，
> 这边是**表索引分派**，且分 **async / sync 两张表**。

## async 表（起始偏移 0x80）

| idx | handler |
|---:|---|
| 0 (0x00) | `dmm_ctrl_InternalCtrlHdlr` |
| 1 (0x01) | `dmm_ctrl_AllocUnitNewHdlr` |
| 2 (0x02) | `dmm_ctrl_AllocUnitShareHdlr` |
| 3 (0x03) | `dmm_ctrl_AllocUnitReuseHdlr` |
| 4 (0x04) | `dmm_ctrl_FreeUnitHdlr` |
| 5 (0x05) | `dmm_ctrl_OpenUnitHdlr` |
| 6 (0x06) | `dmm_ctrl_CloseUnitHdlr` |
| 7 (0x07) | `dmm_ctrl_MsgEventDummy` |
| 8 (0x08) | `dmm_ctrl_MsgEventDummy` |
| 9 (0x09) | `dmm_ctrl_MsgEventDummy` |
| 10 (0x0A) | `dmm_ctrl_MsgEventDummy` |
| 11 (0x0B) | `dmm_ctrl_MsgEventDummy` |
| 12 (0x0C) | `dmm_ctrl_AllocUnitRendezvousHdlr` |
| 13 (0x0D) | `dmm_ctrl_FreeUnitRendzvosHdlr` |
| 14 (0x0E) | `dmm_ctrl_MsgEventDummy` |
| 15 (0x0F) | `dmm_ctrl_MsgEventDummy` |
| 16 (0x10) | `dmm_ctrl_MsgEventDummy` |
| 17 (0x11) | `dmm_ctrl_MsgEventDummy` |
| 18 (0x12) | `dmm_ctrl_MsgEventDummy` |
| 19 (0x13) | `dmm_ctrl_GetUnitInfoHdlr` |
| 20 (0x14) | `dmm_ctrl_SetAllocRuleHdlr` |
| 21 (0x15) | `dmm_ctrl_HotplugHdlr` |
| 22 (0x16) | `dmm_ctrl_HotremoveHdlr` |
| 23 (0x17) | `dmm_ctrl_LoadExtraMapHdlr` |
| 24 (0x18) | `dmm_ctrl_GetUnitSizeHdlr` |
| 25 (0x19) | `dmm_ctrl_GetUnitMaxAllocHdlr` |
| 26 (0x1A) | `dmm_ctrl_GetUnitHandleHdlr` |
| 27 (0x1B) | `dmm_ctrl_MsgEventDummy` |
| 28 (0x1C) | `dmm_ctrl_InvMsgHdlr` |
| 29 (0x1D) | `dmm_ctrl_InvMsgHdlr` |

## sync 表（起始偏移 0xF8）

| idx | handler |
|---:|---|
| 0 (0x00) | `dmm_ctrl_AllocUnitNewHdlr` |
| 1 (0x01) | `dmm_ctrl_AllocUnitShareHdlr` |
| 2 (0x02) | `dmm_ctrl_UnsupportedSyncEventHdlr` |
| 3 (0x03) | `dmm_ctrl_FreeUnitHdlr` |
| 4 (0x04) | `dmm_ctrl_OpenUnitHdlr` |
| 5 (0x05) | `dmm_ctrl_CloseUnitHdlr` |
| 6 (0x06) | `dmm_ctrl_MsgEventDummy` |
| 7 (0x07) | `dmm_ctrl_MsgEventDummy` |
| 8 (0x08) | `dmm_ctrl_MsgEventDummy` |
| 9 (0x09) | `dmm_ctrl_MsgEventDummy` |
| 10 (0x0A) | `dmm_ctrl_MsgEventDummy` |
| 11 (0x0B) | `dmm_ctrl_AllocUnitRendezvousHdlr` |
| 12 (0x0C) | `dmm_ctrl_FreeUnitRendzvosHdlr` |
| 13 (0x0D) | `dmm_ctrl_MsgEventDummy` |
| 14 (0x0E) | `dmm_ctrl_MsgEventDummy` |
| 15 (0x0F) | `dmm_ctrl_MsgEventDummy` |
| 16 (0x10) | `dmm_ctrl_MsgEventDummy` |
| 17 (0x11) | `dmm_ctrl_MsgEventDummy` |
| 18 (0x12) | `dmm_ctrl_GetUnitInfoHdlr` |
| 19 (0x13) | `dmm_ctrl_UnsupportedSyncEventHdlr` |
| 20 (0x14) | `dmm_ctrl_UnsupportedSyncEventHdlr` |
| 21 (0x15) | `dmm_ctrl_UnsupportedSyncEventHdlr` |
| 22 (0x16) | `dmm_ctrl_UnsupportedSyncEventHdlr` |
| 23 (0x17) | `dmm_ctrl_UnsupportedSyncEventHdlr` |
| 24 (0x18) | `dmm_ctrl_UnsupportedSyncEventHdlr` |
| 25 (0x19) | `dmm_ctrl_GetUnitHandleHdlr` |
| 26 (0x1A) | `dmm_ctrl_PoweronUnitHdlr` |
| 27 (0x1B) | `dmm_ctrl_InvMsgHdlr` |

