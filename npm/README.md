# pdb — PLang 调试器

GDB 风格的 PLang 调试器，基于 LLDB C++ API。支持断点、单步、变量查看、栈回溯、表达式求值。

## 安装

```bash
npm i -g --allow-scripts=@peyt/pdb @peyt/pdb
```

安装需要你机器上有 **LLVM/LLDB**：

- macOS：`brew install llvm`
- Linux（Debian/Ubuntu）：`sudo apt install llvm`
- Windows：从 https://llvm.org 安装并设置 `LLVM_HOME`

安装脚本会自动定位 LLDB 并配置 `pdb` 使用它。
如果 LLVM 不在标准位置，请先设置 `LLVM_HOME` 环境变量。

## 编译可调试程序

pdb 调试需要调试信息。用 plc 编译 PLang 程序：

```bash
plc -O0 app.plang -o app     # -O0 保留完整调试信息，链接时自动生成 dSYM
```

macOS 上 plc 链接时会自动生成 `app.dSYM`，pdb 依赖它读取变量与断点信息。

## 使用

```bash
pdb ./app
```

```
(pdb) break app.plang:6      # 按行号断点
(pdb) break fib               # 按函数名断点
(pdb) run                     # 运行
(pdb) print n                 # 打印变量/表达式
(pdb) info locals             # 局部变量
(pdb) info args               # 参数
(pdb) bt                      # 栈回溯
(pdb) next / step / finish    # 单步
(pdb) continue                # 继续
(pdb) quit                    # 退出
```

命令列表见 `help`。

## 许可证

AGPL-3.0
