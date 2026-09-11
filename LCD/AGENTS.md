# 项目约束

<!--
本文件用于约束 AI 助手（opencode）在本项目中的行为。
下面的每一条规则都应严格遵守，不得绕过。
如需调整规则，请直接编辑本文件并通知 AI。
-->

- 不执行编译（build）：不得运行任何构建命令（如 idf.py build、cmake --build 等）。
- 不执行烧录/下载固件（flash/download）到板子：不得运行任何烧录或下载命令（如 idf.py flash、esptool.py 等）。
- 实时性不高的就放在PSRAM,PSARM中的程序栈空间尽量给大
- 用中文回答
- 不要闷头干: 改BUG的时候先告诉我哪里错了,再动手改