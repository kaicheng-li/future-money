# 套利回测工具 C++ 重构版

这是旧版 `BoLL.exe` / `MACD.exe` 的原生 Windows C++ 重构骨架，目标是单 exe、无 Python/Qt 运行时依赖。

## 当前设计

- 原生 Win32 UI：登录页 + BOLL/MACD 回测页。
- 主连代码非空时使用 AkShare 新浪主力连续接口。
- 主连代码留空时使用 Tushare `fut_daily`，token 从 `TUSHARE_TOKEN` 环境变量读取。
- BOLL 参数固定为 26 日均线、上下 2 倍标准差；MACD 为 12/26/9。
- 回测结果支持导出 CSV。
- 本地登录默认 `admin/admin123`，可用 `ARBITRAGE_USER`、`ARBITRAGE_PASSWORD` 环境变量覆盖。

## 构建

在 PowerShell 中：

```powershell
./build.ps1
```

也可以用 Visual Studio 打开 `CMakeLists.txt`。当前环境的 CMake 生成器对 VS 18 的自动检测不稳定，`build.ps1` 直接调用 MSVC，交付构建以它为准。
