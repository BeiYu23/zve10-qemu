# E10 vs M2 注册来源按库统计（同一份修复后探针，dladdr 解析）

E10 menu=0（双 preload 副作用）；M2 正常。两列均为注册事件数。

```
lib                                             E10     M2 判读
/usr/lib/libMWF.so                              122     14 
/usr/lib/libObj.so                              111     53 
/usr/lib/libbackup.so                            44      0 ★ M2 完全没跑
/usr/lib/libInfraBluetoothCore.so                10     10 
/usr/lib/libNetService.so                         6      0 ★ M2 完全没跑
/usr/lib/libupdaterufp.so                         6      0 ★ M2 完全没跑
/usr/lib/libInfraNetConnectionManagerEther.so      6      6 
/usr/lib/libRsrcHotshoe.so                        4      4 
/usr/lib/libmprctrl.so                            4      0 ★ M2 完全没跑
/usr/lib/libWlanResource.so                       4      4 
/usr/lib/libInfraHttpClient.so                    3      0 ★ M2 完全没跑
/usr/lib/libInfraAirplaneMode.so                  3      3 
/usr/bin/im.elf                                   3      3 
/usr/lib/libdig_level.so                          3      0 ★ M2 完全没跑
/usr/lib/libmpr.so                                3      0 ★ M2 完全没跑
/usr/lib/libInfraMediaDetect.so                   2      1 
/usr/lib/libosal_uipc.so                          2      2 
/usr/lib/libInfraKikiLogGenerator.so              2      2 
/usr/lib/libgps_rsrc.so                           2      0 ★ M2 完全没跑
/usr/lib/libInfraNetInitializer.so                1      1 
/usr/lib/libuiobserve.so                          1      0 ★ M2 完全没跑
/usr/lib/libosal_ulogio.so                        1      1 
/usr/lib/libResourceBluetooth.so                  1      1 
/usr/lib/libJiritsu.so                            1      1 
/usr/lib/libInfraIPsec.so                         1      0 ★ M2 完全没跑
/usr/lib/libWlanAdjust.so                         1      1 
/usr/bin/bootin.elf                               1      1 
/usr/lib/libInfraNetDeviceInfo.so                 1      0 ★ M2 完全没跑
/usr/lib/libBTAdjust.so                           1      1 
/usr/lib/libInfraAccessLog.so                     1      1 
/usr/lib/libInfraHdmi.so                          1      1 
/usr/lib/libBusPower.so                           0      2 M2 独有
/usr/lib/libInfraNetRfTest.so                     0      2 M2 独有
/usr/lib/libInfraCrypt.so                         0      1 M2 独有
/usr/lib/libUsbBusPower.so                        0      2 M2 独有
/usr/lib/libsencore.so                            0      2 M2 独有
/usr/lib/libUsbControl.so                         0      4 M2 独有
```
