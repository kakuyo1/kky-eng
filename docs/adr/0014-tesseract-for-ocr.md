# 0014 OCR 使用 Tesseract

- **状态**：accepted
- **背景**：阶段 A 需要为截图和自动扫描选择 OCR 引擎，两条路径必须共用一致的英文识别结果。
- **决定**：截图与扫描统一使用 Tesseract。安装包分发 Tesseract 运行时及英文 traineddata。
- **后果**：阶段 A 需要维护 Tesseract 调用边界与离线测试；安装产物增加 OCR 运行时和语言数据。
