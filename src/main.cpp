// pdb —— PLang 调试器（GDB 风格，基于 liblldb C++ API）
// 最小可运行骨架：验证 LLDB 初始化与 target 创建链路。

#include <iostream>
#include <string>
#include <lldb/API/LLDB.h>

int main(int argc, char* argv[])
{
    // 初始化 LLDB（幂等，可多次调用）
    lldb::SBDebugger::Initialize();

    std::cout << "pdb (PLang debugger)\n";
    std::cout << "LLDB version: "
              << lldb::SBDebugger::GetVersionString() << "\n";

    if (argc < 2)
    {
        std::cout << "usage: pdb <executable> [args...]\n";
        lldb::SBDebugger::Terminate();
        return 0;
    }

    // 创建调试器（不打印、不交互，由 pdb 自己接管 IO）
    lldb::SBDebugger dbg = lldb::SBDebugger::Create();
    if (!dbg.IsValid())
    {
        std::cerr << "error: failed to create debugger\n";
        lldb::SBDebugger::Terminate();
        return 1;
    }

    // 创建目标（加载可执行文件；不立即启动）
    lldb::SBTarget target = dbg.CreateTarget(argv[1]);
    if (!target.IsValid())
    {
        std::cerr << "error: failed to create target for '" << argv[1] << "'\n";
        lldb::SBDebugger::Terminate();
        return 1;
    }

    std::cout << "target: " << target.GetExecutable().GetFilename() << "\n";
    std::cout << "triple: " << target.GetTriple() << "\n";
    std::cout << "breakpoints available via SBTarget, launch via SBProcess.\n";

    // 清理（骨架阶段不做实际调试）
    lldb::SBDebugger::Destroy(dbg);
    lldb::SBDebugger::Terminate();
    return 0;
}
