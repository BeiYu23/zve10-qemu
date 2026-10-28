# IMDB 启动清单（提取自 libIMDB.so，共 199 条，按原厂顺序）

> 用途：ZV-E10 的模块/库加载顺序依据。`Unknown symbol Backup_read` 与
> `No snd_msg registered` 很可能是加载顺序错误导致（a7c2-re 实测结论）。

| # | 条目 | ID | 模式掩码 | 入口函数 |
|---:|---|---:|---:|---|
| 1 | `/kmod/dmm.ko` | 0x61 | 0x7FFFFFFF | DmmConfig=/usr/share/dmm/DmmConfig.bin, dmm |
| 2 | `/kmod/upm.ko` | 0x92 | 0x7FFFFFFF | upm |
| 3 | `/kmod/stream.ko` | 0x3B | 0x7FFFFFFF | stream |
| 4 | `/kmod/stream2.ko` | 0xC3 | 0x7FFFFFFF | stream2 |
| 5 | `/kmod/mcmn_drv.ko` | 0xD9 | 0xFFFFFFF9 | mcmn_drv |
| 6 | `/kmod/ms_drv.ko` | 0xC4 | 0xFFFFFFF9 | ms_drv |
| 7 | `/kmod/mmc_drv.ko` | 0xD6 | 0xFFFFFFF9 | mmc_drv |
| 8 | `/kmod/sata_drv.ko` | 0xD7 | 0xFFFFFFF9 | sata_drv |
| 9 | `/kmod/msen_drv.ko` | 0xD8 | 0xFFFFFFF9 | msen_drv |
| 10 | `/kmod/hdmi.ko` | 0x9B | 0x7FFFFFFF | hdmi |
| 11 | `/kmod/cec.ko` | 0x9D | 0x7FFFFFFF | cec |
| 12 | `libosal_uipc.so` | 0x3F | 0x7FFFFFFF |  |
| 13 | `libosal_utm.so` | 0x7FD | 0x7FFFFFFF |  |
| 14 | `libosal_ulogio.so` | 0x7FE | 0x7FFFFFFF | ulogio_so_suspend, ulogio_so_resume |
| 15 | `libInfraInterchipDatatrans.so` | 0x282 | 0x7FFFFFFF | interchip_transfer_user_init |
| 16 | `libNVM.so` | 0x1E3 | 0x7FFFFFFF | NVM_Initialize, NVM_Suspend, NVM_Resume |
| 17 | `libbackup.so` | 0x3E | 0x7FFFFFFF | backup_user_suspend, backup_user_resume |
| 18 | `libSaveLoadSettings.so` | 0x2DD | 0x7FFFFFFF | infra_saveLoadSettings_init, infra_saveLoadSettings_sus, infra_saveLoadSettings_res |
| 19 | `libbkdmn.so` | 0x2CD | 0x7FFFFFFF | BkDmn_init, BkDmn_exit, BkDmn_suspend |
| 20 | `libOnDemandLoader.so` | 0xBD | 0x7FFFFFFF | odl_init, odl_sus, odl_res |
| 21 | `libInfraKikiLogGenerator.so` | 0x110 | 0x7FFFFFFF | KikiLogGenerator_init, KikiLogGenerator_sus, KikiLogGenerator_res |
| 22 | `libInfraAccessLog.so` | 0x331 | 0x7FFFFFFF | AccessLogInfra_init, AccessLogInfra_sus, AccessLogInfra_res |
| 23 | `libSysDef.so` | 0xF7 | 0x7FFFFFFF |  |
| 24 | `libMWF.so` | 0x97 | 0x7FFFFFFF | MWF_Init |
| 25 | `libi2cuipc.so` | 0x52 | 0xFFFFFFBF | i2cu_user_init, i2cu_user_exit, i2cu_user_sus |
| 26 | `libI2CRight.so` | 0x270 | 0xFFFFFFBF | i2c_right_init, i2c_right_exit, i2c_right_sus |
| 27 | `libInfraInput.so` | 0x7FB007A | 0xFFFFFFBF | infra_input_init, infra_input_sus, infra_input_res |
| 28 | `libInfraError.so` | 0x27A | 0xFFFFFFBF | infra_error_init, infra_error_exit, infra_error_sus |
| 29 | `libJiritsu.so` | 0x16F | 0xFFFFFF9F | jiritsu_init, jiritsu_sus, jiritsu_res |
| 30 | `libAtn.so` | 0x266 | 0x7FFFFFFF | Atn_init, Atn_sus, Atn_res |
| 31 | `libGetLog.so` | 0x20F | 0x7FFFFFFF | GetLog_init, GetLog_exit, GetLog_sus |
| 32 | `libread_refresh.so` | 0x278 | 0x7FFFFFFF | read_refresh_init, read_refresh_exit, read_refresh_suspend |
| 33 | `libTestDriver.so` | 0x2B6 | 0x00000100 | TestDriver_Init |
| 34 | `/usr/kmod/liro.ko` | 0x48 | 0x7FFFFFFF | liro |
| 35 | `libCoreSync.so` | 0x7FB0131 | 0xFFFFFFDF | core_sync_user_init, core_sync_user_sus, core_sync_user_res |
| 36 | `libgrm.so` | 0x7FB0080 | 0xFFFFFFDF | grm_init, grm_sus, grm_res |
| 37 | `libgpe.so` | 0x82 | 0xFFFFFFDF | GpeInit, GpeSus, GpeRes |
| 38 | `libDDS.so` | 0x7FA005C | 0x7FFFFFFF | DDSUserInit, DDSUserSus, DDSUserRes |
| 39 | `libuxengine.so` | 0x7FB007F | 0xFFFFFF9F | uxengine_init, uxengine_sus, uxengine_res |
| 40 | `libInfraIndctr.so` | 0x7FB004E | 0xFFFFFF9F | infra_indctr_init, infra_indctr_sus, infra_indctr_res |
| 41 | `libInfraTp.so` | 0x7FB007B | 0xFFFFFF9F | infra_tp_init, infra_tp_sus, infra_tp_res |
| 42 | `libRsrcHotshoe.so` | 0x16C | 0xFFFFFF9F | rsrc_hotshoe_init, rsrc_hotshoe_sus, rsrc_hotshoe_res |
| 43 | `libInfraHotshoe.so` | 0x7FB007C | 0xFFFFFF9F | infra_hotshoe_init, infra_hotshoe_sus, infra_hotshoe_res |
| 44 | `libInfraSircs.so` | 0x7FB007D | 0xFFFFFF9F | infra_sircs_init, infra_sircs_sus, infra_sircs_res |
| 45 | `libInfraDial.so` | 0x7FB011B | 0xFFFFFF9F | infra_dial_init, infra_dial_sus, infra_dial_res |
| 46 | `libInfraLanc.so` | 0x7FB0119 | 0xFFFFFF9F | infra_lanc_init, infra_lanc_sus, infra_lanc_res |
| 47 | `libInfraSelector.so` | 0x7FB0063 | 0xFFFFFF9F | Selector_init, Selector_sus, Selector_res |
| 48 | `libosal_udate.so` | 0x7FB0090 | 0xFFFFFF9F | udate_init, udate_sus, udate_res |
| 49 | `libInfraAvbbc.so` | 0x7FB0055 | 0xFFFFFF9F | InfraAvbbc_Init, InfraAvbbc_Sus, InfraAvbbc_Res |
| 50 | `libInfraMediaConfig.so` | 0x7FB00CF | 0xFFFFFF9F | InfraMediaConfig_Init, InfraMediaConfig_Suspend, InfraMediaConfig_Resume |
| 51 | `libInfraMediaCommon.so` | 0xD5 | 0xFFFFFF9F | mcmn_init, mcmn_suspend, mcmn_resume |
| 52 | `libInfraFsys.so` | 0xC5 | 0xFFFFFF9F | InfraFsys_Init |
| 53 | `libFsysPosixWrapper.so` | 0x2EF | 0xFFFFFF9F | FsysPosix_Init, FsysPosix_Exit |
| 54 | `libInfraOptionInstall.so` | 0x277 | 0xFFFFFF9F | infra_option_install_init, infra_option_install_sus, infra_option_install_res |
| 55 | `libInfraOptionInstaller.so` | 0x1AE | 0xFFFFFF9F | infra_option_installer_init, infra_option_installer_sus, infra_option_installer_res |
| 56 | `libInfraEyeFiControl.so` | 0x7FB0104 | 0xFFFFFF9F | InfraEyeFiControl_Init, InfraEyeFiControl_Suspend, InfraEyeFiControl_Resume |
| 57 | `libInfraMediaMount.so` | 0xB0 | 0xFFFFFF9F | InfraMediaMount_Init |
| 58 | `libInfraMediaDetect.so` | 0xAF | 0xFFFFFF9F | InfraMediaDetect_Init, InfraMediaDetect_Suspend, InfraMediaDetect_Resume |
| 59 | `libmprdummy.so` | 0xC6 | 0xFFFFFF9F |  |
| 60 | `libdiablo.so` | 0x195 | 0xFFFFFF9F |  |
| 61 | `libclema.so` | 0x127 | 0xFFFFFF9F |  |
| 62 | `libmpr.so` | 0x3C | 0xFFFFFF9F |  |
| 63 | `libmprctrl.so` | 0xC7 | 0xFFFFFF9F | InfraMprCtrl_Init |
| 64 | `libMprSeq.so` | 0x1C0 | 0xFFFFFF9F | MprSequencerIf_Init, MprSequencerIf_Suspend, MprSequencerIf_Resume |
| 65 | `libInfraIPTC.so` | 0x276 | 0xFFFFFF9F | InfraIPTC_init, InfraIPTC_sus, InfraIPTC_res |
| 66 | `libInfraSystemProperty.so` | 0x208 | 0xFFFFFF9F | InfraSystemProperty_init, InfraSystemProperty_exit |
| 67 | `libInfraCrypt.so` | 0x2EC | 0xFFFFFF9F | iCrypt_init, iCrypt_exit, iCrypt_sus |
| 68 | `libInfraSetupFile.so` | 0x1DE | 0xFFFFFF9F | InfraSetupFile_init, InfraSetupFile_exit, InfraSetupFile_sus |
| 69 | `libInfraSystemAdjust.so` | 0x209 | 0xFFFFFF9F | InfraSystemAdjust_init, InfraSystemAdjust_exit |
| 70 | `libInfraStillPool.so` | 0x7FB0135 | 0xFFFFFF9F | InfraStillPool_Init |
| 71 | `libInfraDatabase.so` | 0x7FB0037 | 0xFFFFFF9F | InfraDatabase_Init |
| 72 | `libInfraSalvageStatus.so` | 0x7FB00AE | 0xFFFFFF9F | InfraSalvageStatus_Init |
| 73 | `libInfraCacheMgr.so` | 0x38 | 0xFFFFFF9F | InfraCacheMgr_Init |
| 74 | `libResourceBluetooth.so` | 0x1A2 | 0xFFFFFFBF | ResourceBluetooth_init, ResourceBluetooth_sus, ResourceBluetooth_res |
| 75 | `libUsbBusPower.so` | 0x121 | 0xFFFFFFBF | UsbBusPower_Init, UsbBusPower_Suspend, UsbBusPower_Resume |
| 76 | `libInfraUsbCmn.so` | 0xE7 | 0x7FFFFFFF | InfraUsbLib_init |
| 77 | `libsensapi.so` | 0x7FB003D | 0x7FFFFFFF | senser_init, senser_sus, senser_res |
| 78 | `libUsbControl.so` | 0xCA | 0xFFFFFF9F | UsbControl_Init, UsbControl_Suspend, UsbControl_Resume |
| 79 | `libBusPower.so` | 0x122 | 0xFFFFFF9F | BusPower_Init, BusPower_Suspend, BusPower_Resume |
| 80 | `libHotplug.so` | 0x7FB00C8 | 0xFFFFFF9F | Hotplug_Init, Hotplug_Suspend, Hotplug_Resume |
| 81 | `libInfraHdmi.so` | 0x9A | 0xFFFFFFDF | HDMI_init, HDMI_sus, HDMI_res |
| 82 | `libInfraCec.so` | 0x7FB009C | 0xFFFFFF9F | CEC_init, CEC_sus, CEC_res |
| 83 | `libInfraAirplaneMode.so` | 0x14D | 0xFFFFFF9F | infra_airplane_mode_init, infra_airplane_mode_sus, infra_airplane_mode_res |
| 84 | `libInfraNfc.so` | 0x181 | 0xFFFFFF9F | infra_nfc_init, infra_nfc_sus, infra_nfc_res |
| 85 | `libInfraBluetooth.so` | 0x19E | 0xFFFFFF9F | InfraBluetooth_init, InfraBluetooth_sus, InfraBluetooth_res |
| 86 | `libupdater0604.so` | 0x7FB004B | 0xFFFFFFDF | udtr_senser_init, udtr_senser_exit |
| 87 | `libInfraAdjMedia.so` | 0x7FB0115 | 0xFFFFFFDF | InfraAdjMedia_Init, InfraAdjMedia_Exit |
| 88 | `libInfraNetInitializer.so` | 0x294 | 0xFFFFFF9F | InfraNetInitializer_Init, InfraNetInitializer_Sus, InfraNetInitializer_Res |
| 89 | `libWlanResource.so` | 0xA5 | 0xFFFFFF9F | wlanResource_init, wlanResource_sus, wlanResource_res |
| 90 | `libWlanAdjust.so` | 0x157 | 0xFFFFFF9F | WlanAdjust_Init, WlanAdjust_Sus, WlanAdjust_Res |
| 91 | `libWiredLanResource.so` | 0x1D5 | 0xFFFFFF9F | WiredLanResource_Init, WiredLanResource_Sus, WiredLanResource_Res |
| 92 | `libWiredLanAdjust.so` | 0x1D6 | 0xFFFFFF9F | WiredLanAdjust_Init, WiredLanAdjust_Sus, WiredLanAdjust_Res |
| 93 | `libNetServiceEther.so` | 0x293 | 0xFFFFFF9F | netserviceether_init, netserviceether_sus, netserviceether_res |
| 94 | `libInfraNetConnectionManagerEther.so` | 0x2AD | 0xFFFFFF9F | NetConnectionManagerEther_init, NetConnectionManagerEther_sus, NetConnectionManagerEther_res |
| 95 | `libInfraEthernetService.so` | 0x223 | 0xFFFFFF9F | EthernetService_Init, EthernetService_Sus, EthernetService_Res |
| 96 | `libNetService.so` | 0x13E | 0xFFFFFF9F | netservice_init, netservice_sus, netservice_res |
| 97 | `libInfraIPsec.so` | 0x2BA | 0xFFFFFF9F | InfraIPsec_init, InfraIPsec_exit, InfraIPsec_sus |
| 98 | `libCameraStillDate.so` | 0x7FB012C | 0xFFFFFF9F | CameraStillDate_init |
| 99 | `libdig_level.so` | 0x12F | 0xFFFFFF9F | dig_level_init, dig_level_sus, dig_level_res |
| 100 | `libInfraNetDeviceInfo.so` | 0x147 | 0xFFFFFF9F | InfraNetDeviceInfo_Init, InfraNetDeviceInfo_Sus, InfraNetDeviceInfo_Res |
| 101 | `libWirelessPower.so` | 0x142 | 0xFFFFFFBF | WirelessPower_Init, WirelessPower_Suspend, WirelessPower_Resume |
| 102 | `libHandleRsrc.so` | 0x1DB | 0xFFFFFFDF | handle_rsrc_init, handle_rsrc_sus, handle_rsrc_res |
| 103 | `libgps_rsrc.so` | 0x108 | 0xFFFFFFDF | gps_rsrc_init, gps_rsrc_sus, gps_rsrc_res |
| 104 | `libgps.so` | 0x7FB00A1 | 0xFFFFFF9F | gps_init, gps_sus, gps_res |
| 105 | `libcompass.so` | 0x7FB00A2 | 0xFFFFFF9F | compass_init, compass_sus, compass_res |
| 106 | `libgeo.so` | 0x7FB00A0 | 0xFFFFFF9F | geo_init, geo_sus, geo_res |
| 107 | `libUsbChargingApp.so` | 0x124 | 0x00000020 | UsbChgApp_Init, UsbChgApp_Exit |
| 108 | `libInfraBluetoothWoLEBoot.so` | 0x19E | 0x00000020 | libInfraBluetoothWoLEBoot_init |
| 109 | `libInfraBluetoothWoLEBoot_init` | 0x4D59 | 0x0000019E |  |
| 110 | `libInfraHdmiInput.so` | 0x179 | 0xFFFFFF9F | HDMI_Input_init, HDMI_Input_exit, HDMI_Input_sus |
| 111 | `libLccDaemon.so` | 0x210 | 0xFFFFFF1F | Lcc_Daemon_Init |
| 112 | `libAdjModule.so` | 0x217 | 0x7FFFFFFF | AdjModule_Init, AdjModule_Sus, AdjModule_Resume |
| 113 | `libDpro_EVF.so` | 0x1E0 | 0x7FFFFFFF | evf_init, evf_sus, evf_res |
| 114 | `libInfraUpmPro.so` | 0x260 | 0x7FFFFFFF | InfraUpmPro_init, InfraUpmPro_exit, InfraUpmPro_sus |
| 115 | `libfan.so` | 0x238 | 0xFFFFFFDF | fan_infra_init, fan_infra_sus, fan_infra_res |
| 116 | `libInfraUsb.so` | 0x263 | 0x7FFFFFFF | InfraUsb_init, InfraUsb_exit, InfraUsb_sus |
| 117 | `libEMG.so` | 0x28E | 0x7FFFFFFF | emg_init, emg_sus, emg_res |
| 118 | `libAdjustUIProxy.so` | 0x286 | 0x7FFFFFFF | AdjustUIProxy_init, AdjustUIProxy_exit, AdjustUIProxy_sus |
| 119 | `libTetheringResource.so` | 0x2CB | 0x7FFFFFFF | TetheringResource_Init, TetheringResource_Sus, TetheringResource_Res |
| 120 | `libNetServiceTethering.so` | 0x2CA | 0x7FFFFFFF | netservicetethering_init, netservicetethering_sus, netservicetethering_res |
| 121 | `libJiritsuUIView.so` | 0x2E0 | 0x7FFFFFFF | JiritsuUIView_init, JiritsuUIView_sus, JiritsuUIView_res |
| 122 | `libJiritsuSlave.so` | 0x2F3 | 0x7FFFFFFF | JiritsuSlave_init, JiritsuSlave_sus, JiritsuSlave_res |
| 123 | `libInfraHttpClient.so` | 0xA9 | 0x7FFFFFFF | InfraHttpClient_Init, InfraHttpClient_Sus, InfraHttpClient_Res |
| 124 | `libInfraMediaPwrMgr.so` | 0x7FB00F8 | 0xFFFFFF9F | InfraMediaPwrMgr_init, InfraMediaPwrMgr_inact |
| 125 | `libInfraCameraBackground.so` | 0x7FB0102 | 0xFFFFFFDF | CameraBackground_init, CameraBackground_inact |
| 126 | `libBizInitializer.so` | 0x258 | 0x7FFFFFFF | BIZ_Init, BIZ_Suspend, BIZ_Resume |
| 127 | `/usr/local/lib/libVssApp.so` | 0x1A9 | 0xFFFFFF9F | vssapp_init, vssapp_exit |
| 128 | `/usr/local/lib/AdjChkConfig.so` | 0x6338 | 0x00006338 |  |
| 129 | `/usr/local/lib/libVssAppMain.so` | 0x52D0 | 0x00006338 |  |
| 130 | `libJetControl.so` | 0xCB | 0x7FFFFFFF | JetControl_init, JetControl_exit |
| 131 | `libInfraUsbHostStorage.so` | 0xEE | 0x7FFFFFFF | UsbHostStorage_Init, UsbHostStorage_Exit |
| 132 | `libInfraMtpServer.so` | 0xE0 | 0x7FFFFFFF | MtpServer_Init, MtpServer_Exit |
| 133 | `libInfraMscServer.so` | 0xDF | 0x7FFFFFFF | MscServer_Init, MscServer_Exit |
| 134 | `libInfraExtCmdSvc.so` | 0xE1 | 0x7FFFFFFF | ExtCmdSvc_Init, ExtCmdSvc_Exit |
| 135 | `libusbcmd.so` | 0xE2 | 0x7FFFFFFF | UsbCmd_ExtCmdSvcWrap_Init, UsbCmd_ExtCmdSvcWrap_Exit |
| 136 | `libInfraDevInfoSender.so` | 0xE4 | 0x7FFFFFFF | DevInfoSender_Init, DevInfoSender_Exit |
| 137 | `/usr/kmod/usbg_storage.ko` | 0xCC | 0x7FFFFFFF | usbg_storage |
| 138 | `/usr/kmod/usbg_stillimage.ko` | 0xEB | 0x7FFFFFFF | usbg_stillimage |
| 139 | `/usr/kmod/usbg_sen.ko` | 0xEA | 0x7FFFFFFF | usbg_sen |
| 140 | `libInfraJetScsiServer.so` | 0xF5 | 0x7FFFFFFF | JetScsiServer_init, JetScsiServer_exit |
| 141 | `libInfraDevInfoSender.so` | 0xE4 | 0x7FFFFFFF | DevInfoSender_Init, DevInfoSender_Exit |
| 142 | `libtestcmd_scn.so` | 0xDC | 0x7FFFFFFF | testcmd_scn_init, testcmd_scn_exit |
| 143 | `libInfraKikiLogSender.so` | 0x10F | 0x7FFFFFFF | KikiLogSender_init, KikiLogSender_exit |
| 144 | `libupdaterufp.so` | 0x11D | 0x7FFFFFFF | udtr_ufp_init, udtr_ufp_exit |
| 145 | `libInfraPtpExtCmd.so` | 0xE3 | 0x7FFFFFFF | PtpExtCmd_Init, PtpExtCmd_Exit |
| 146 | `libInfraAppDlUsbCommunication.so` | 0x12E | 0x7FFFFFFF | AppDlUsbCommunication_Init, AppDlUsbCommunication_Exit |
| 147 | `libInfraExtBackupCommunicator.so` | 0x146 | 0x7FFFFFFF | ExtBackupCommunicator_Init, ExtBackupCommunicator_Exit |
| 148 | `libInfraDlnaControl.so` | 0x141 | 0x7FFFFFFF | DlnaControl_init, DlnaControl_exit |
| 149 | `libInfraPtpControl.so` | 0xE6 | 0x7FFFFFFF | InfraPtpControl_Init, InfraPtpControl_Exit |
| 150 | `libInfraMDnsControl.so` | 0x15F | 0x7FFFFFFF | MDnsControl_init, MDnsControl_exit |
| 151 | `libInfraCameraProfile.so` | 0x15B | 0x7FFFFFFF | infra_cameraProfile_init, infra_cameraProfile_exit |
| 152 | `libInfraRemote.so` | 0x16A | 0x7FFFFFFF | InfraRemote_Init, InfraRemote_Exit |
| 153 | `libInfraStreamingServer.so` | 0x171 | 0x7FFFFFFF | InfraStreamingServer_init, InfraStreamingServer_exit |
| 154 | `libInfraRemoteControlNet.so` | 0x172 | 0x7FFFFFFF | InfraRemoteControlNet_Init, InfraRemoteControlNet_Exit |
| 155 | `libInfraWebApi.so` | 0x176 | 0x7FFFFFFF | InfraNetWebApi_init, InfraNetWebApi_exit |
| 156 | `libInfraLensCommunicator.so` | 0x17D | 0x7FFFFFFF | LensCommunicator_Init, LensCommunicator_Exit |
| 157 | `libInfraNetConnectionManager.so` | 0x188 | 0x7FFFFFFF | NetConnectionManager_init, NetConnectionManager_exit |
| 158 | `libInfraNetFunctionManager.so` | 0x187 | 0x7FFFFFFF | NetFunctionManager_init, NetFunctionManager_exit |
| 159 | `libInfraLiveViewClient.so` | 0x191 | 0x7FFFFFFF | LiveViewClient_init, LiveViewClient_exit |
| 160 | `libInfraWebApiClient.so` | 0x190 | 0x7FFFFFFF | InfraWebApiClient_init, InfraWebApiClient_exit |
| 161 | `libInfraNetworkServiceInfo.so` | 0x193 | 0x7FFFFFFF | InfraNetworkServiceInfo_Init, InfraNetworkServiceInfo_Exit |
| 162 | `libInfraHighlight.so` | 0x199 | 0x7FFFFFFF | InfraHighlight_Init, InfraHighlight_Exit |
| 163 | `libInfraMakeScenario.so` | 0x19B | 0x7FFFFFFF | InfraMakeScenario_Init, InfraMakeScenario_Exit |
| 164 | `libInfraBGMMeta.so` | 0x19D | 0x7FFFFFFF | InfraBGMMeta_Init, InfraBGMMeta_Exit |
| 165 | `libInfraMetaSupplier.so` | 0x19C | 0x7FFFFFFF | InfraMetaSupplier_Init, InfraMetaSupplier_Exit |
| 166 | `libInfraActionDetect.so` | 0x1A4 | 0x7FFFFFFF | InfraActionDetect_Init, InfraActionDetect_Exit |
| 167 | `libInfraNetLiveStreaming.so` | 0x1B1 | 0x7FFFFFFF | InfraNetLiveStreaming_Init, InfraNetLiveStreaming_Exit |
| 168 | `libInfraFtpClient.so` | 0x1AF | 0x7FFFFFFF | InfraFtpClient_Init, InfraFtpClient_Exit |
| 169 | `libInfraCertificate.so` | 0x1B0 | 0x7FFFFFFF | InfraCertificate_Init, InfraCertificate_Exit |
| 170 | `libInfraBluetoothLowEnergy.so` | 0x1A0 | 0x7FFFFFFF | InfraBluetoothLowEnergy_init, InfraBluetoothLowEnergy_exit |
| 171 | `libInfraCCMClient.so` | 0x1FD | 0x7FFFFFFF | InfraCCMClient_Init, InfraCCMClient_Exit |
| 172 | `libInfraSavonaServer.so` | 0x1FE | 0x7FFFFFFF | InfraSavonaServer_Init, InfraSavonaServer_Exit |
| 173 | `libInfraOpusLiveStreaming.so` | 0x20D | 0x7FFFFFFF | InfraOpusLiveStreaming_Init, InfraOpusLiveStreaming_Exit |
| 174 | `libInfraRtmpLiveStreaming.so` | 0x2D6 | 0x7FFFFFFF | InfraRtmpLiveStreaming_Init, InfraRtmpLiveStreaming_Exit |
| 175 | `libInfraUstream.so` | 0x211 | 0x7FFFFFFF | InfraUstream_Init, InfraUstream_Exit |
| 176 | `libInfraMonitoring.so` | 0x255 | 0x7FFFFFFF | InfraMonitoring_Init, InfraMonitoring_Exit |
| 177 | `libInfraRemoteCtrlProxy.so` | 0x256 | 0x7FFFFFFF | InfraRemoteCtrlProxy_Init, InfraRemoteCtrlProxy_Exit |
| 178 | `libInfraFileTransfer.so` | 0x24F | 0x7FFFFFFF | InfraFileTransfer_Init, InfraFileTransfer_Exit |
| 179 | `libInfraRemoteCtrlCGI.so` | 0x25B | 0x7FFFFFFF | InfraRemoteCtrlCGI_Init, InfraRemoteCtrlCGI_Exit |
| 180 | `libOptionInstallerUSB.so` | 0x25F | 0x7FFFFFFF | option_installer_usb_init, option_installer_usb_exit |
| 181 | `libInfraUpdaterDPro.so` | 0x269 | 0x7FFFFFFF | InfraUpdaterDPro_init, InfraUpdaterDPro_exit |
| 182 | `libInfraMultiIF.so` | 0x27B | 0x7FFFFFFF | InfraMultiIF_Init, InfraMultiIF_Exit |
| 183 | `libInfraCellularService.so` | 0x25D | 0x7FFFFFFF | InfraCellular_Init, InfraCellular_Exit |
| 184 | `libInfraTcsync.so` | 0x272 | 0x7FFFFFFF | InfraTcsync_Init, InfraTcsync_Exit |
| 185 | `libInfraSKD.so` | 0x259 | 0x7FFFFFFF | SKData_Init, SKData_Exit |
| 186 | `libInfraOptionInstallUSB.so` | 0x2A7 | 0x7FFFFFFF | option_install_usb_init, option_install_usb_exit |
| 187 | `libInfraNetIncom.so` | 0x2A9 | 0x7FFFFFFF | InfraNetIncom_Init, InfraNetIncom_Exit |
| 188 | `libInfraMCBClient.so` | 0x2B5 | 0x7FFFFFFF | InfraMCBClient_Init, InfraMCBClient_Exit |
| 189 | `libInfraExtTransferApp.so` | 0x2BC | 0x7FFFFFFF | ExtTransferApp_Init, ExtTransferApp_Exit |
| 190 | `libInfraBluetoothClassic.so` | 0x19F | 0x7FFFFFFF | InfraBluetoothClassic_init, InfraBluetoothClassic_exit |
| 191 | `libInfraStreamingUsb.so` | 0x2D9 | 0x7FFFFFFF | UsbInfraStreaming_Init, UsbInfraStreaming_Exit |
| 192 | `/usr/bin/WebApiLauncher.sh` | 0x17C | 0x7FFFFFFF |  |
| 193 | `/usr/bin/startBTCore.sh` | 0x1DA | 0x7FFFFFFF |  |
| 194 | `/usr/bin/qsi.elf` | 0xD3 | 0xFFFFFFF9 | noevent0 |
| 195 | `/usr/bin/qsi.elf` | 0xD3 | 0x00000006 |  |
| 196 | `/usr/bin/network.sh` | 0x7ED | 0x7FFFFFFF |  |
| 197 | `libIMStress.so` | 0x7EE | 0x7FFFFFFF | IM_STRESS_SO_init, IM_STRESS_SO_exit, IM_STRESS_SO_sus |
| 198 | `libIMStress2.so` | 0x7EC | 0x7FFFFFFF | IM_STRESS_SO_init, IM_STRESS_SO_exit, IM_STRESS_SO_sus |
| 199 | `libIMSample.so` | 0x7EF | 0x7FFFFFFF | IM_SAMPLE_SO_init, IM_SAMPLE_SO_exit, IM_SAMPLE_SO_sus |
