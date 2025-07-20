# Copilot Instructions

## File Encoding Rules
- **新規作成のh, cppファイルは UTF-8 BOM付きとする**
- This prevents "Characters that cannot be displayed in current code page (932)" warnings in Visual Studio
- Ensures proper handling of Japanese comments if needed