Screen Recorder 0.1.0（zip 版）
================================

安装
----
1. 解压本压缩包到任意目录（不需管理员权限）。
2. 运行 install.ps1：
       pwsh -File install.ps1
   默认安装到 %LOCALAPPDATA%\ScreenRecording，并创建开始菜单快捷方式「Screen Recorder」。
   自定义目录：
       pwsh -File install.ps1 -InstallDir D:\Apps\ScreenRecording
3. 启动：安装目录 bin\sr-ui.exe

卸载
----
    pwsh -File uninstall.ps1
删除程序目录、开始菜单快捷方式与卸载登记；**保留**用户成片
（默认 %USERPROFILE%\Videos\ScreenRecording）。
要连成片一起删：pwsh -File uninstall.ps1 -Purge

系统要求
--------
Windows 10 1809 及以上；推荐 Windows 11。
需要可用的显示输出（虚拟机需安装虚拟显示驱动）；系统回环音频用于录制系统声音。

已知限制
--------
* 硬件编码：MF 的 H.264 硬件编码器是否存在取决于显卡与驱动；
  界面「设置 → 可用编码器」会显示实际可用项与不可用原因。
* UAC 提权窗口需要以管理员身份运行才能采集到内容，否则该窗口区域为黑屏。
* DRM 受保护内容（部分播放器/流媒体）系统层面即为黑屏，属平台限制。
* 本包不含代码签名证书，Windows SmartScreen 可能提示未知发布者。

许可
----
随包附带的许可声明见 LICENSE.txt（若存在）。
