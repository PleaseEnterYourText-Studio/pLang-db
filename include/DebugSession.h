#ifndef PDB_DEBUG_SESSION_H
#define PDB_DEBUG_SESSION_H

#include <string>
#include <vector>
#include <lldb/API/LLDB.h>

// pdb 调试会话：封装 liblldb，同步模式命令驱动。
// 一个会话对应一个可执行文件 target 与一次调试运行。
class DebugSession
{
public:
    // 打开可执行文件，创建 target；失败返回 false
    bool open(const std::string& executable);

    // 启动运行（可在断点后重跑）
    bool run(const std::vector<std::string>& args);

    // 是否已有目标
    bool hasTarget() const { return target.IsValid(); }
    // 进程当前状态
    lldb::StateType state() const;

    // 断点
    bool breakAtLine(const std::string& file, int line);
    bool breakAtFunction(const std::string& name);
    void listBreakpoints() const;

    // 执行控制（同步阻塞，直到下次停止或退出）
    bool continueExec();
    bool stepOver();
    bool stepInto();
    bool stepOut();

    // 当前停止状态
    bool isStopped() const;
    std::string stopReasonText() const;

    // 栈帧
    int frameCount() const;
    bool selectFrame(int index);
    int currentFrameIndex() const;

    // 变量与表达式
    std::vector<std::string> locals() const;
    std::vector<std::string> args() const;
    std::string printExpr(const std::string& expr) const;
    std::string frameDescription() const;

    // 源码行
    std::string sourceLine(int contextLines = 0) const;

    // 清理
    void destroy();

private:
    mutable lldb::SBDebugger dbg;
    lldb::SBTarget target;
    mutable lldb::SBProcess process;
    mutable lldb::SBThread thread;
    int selectedFrame = 0;
    std::string executable;
};

#endif
