#include "DebugSession.h"
#include <iostream>
#include <sstream>
#include <algorithm>

bool DebugSession::open(const std::string& executable)
{
    if (!dbg.IsValid())
    {
        dbg = lldb::SBDebugger::Create();
        if (!dbg.IsValid()) return false;
        dbg.SetAsync(false);   // 同步模式：命令驱动，停止即阻塞返回
    }
    target = dbg.CreateTarget(executable.c_str());
    if (!target.IsValid()) return false;
    this->executable = executable;
    selectedFrame = 0;
    return true;
}

bool DebugSession::run(const std::vector<std::string>& args)
{
    if (!target.IsValid()) return false;
    std::vector<const char*> argv;
    for (auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    lldb::SBLaunchInfo launchInfo(argv.data());
    lldb::SBError err;
    process = target.Launch(launchInfo, err);
    return process.IsValid() && err.Success();
}

lldb::StateType DebugSession::state() const
{
    if (!process.IsValid()) return lldb::eStateInvalid;
    return process.GetState();
}

bool DebugSession::isStopped() const
{
    return process.IsValid() && process.GetState() == lldb::eStateStopped;
}

std::string DebugSession::stopReasonText() const
{
    if (!process.IsValid()) return "no process";
    auto reason = process.GetSelectedThread().GetStopReason();
    switch (reason)
    {
        case lldb::eStopReasonBreakpoint: return "breakpoint hit";
        case lldb::eStopReasonSignal: return "signal";
        case lldb::eStopReasonException: return "exception";
        case lldb::eStopReasonExec: return "exec";
        case lldb::eStopReasonPlanComplete: return "plan complete";
        case lldb::eStopReasonThreadExiting: return "thread exiting";
        default: return "stopped";
    }
}

bool DebugSession::breakAtLine(const std::string& file, int line)
{
    if (!target.IsValid()) return false;
    auto bp = target.BreakpointCreateByLocation(file.c_str(), line);
    return bp.IsValid() && bp.GetNumLocations() > 0;
}

bool DebugSession::breakAtFunction(const std::string& name)
{
    if (!target.IsValid()) return false;
    auto bp = target.BreakpointCreateByName(name.c_str());
    return bp.IsValid() && bp.GetNumLocations() > 0;
}

void DebugSession::listBreakpoints() const
{
    if (!target.IsValid()) return;
    for (int i = 0; i < target.GetNumBreakpoints(); ++i)
    {
        auto bp = target.GetBreakpointAtIndex(i);
        std::cout << bp.GetID() << ": ";
        for (int j = 0; j < bp.GetNumLocations(); ++j)
        {
            auto loc = bp.GetLocationAtIndex(j);
            auto sc = loc.GetAddress().GetLineEntry().GetFileSpec();
            std::cout << sc.GetFilename() << ":" << loc.GetAddress().GetLineEntry().GetLine()
                      << "  addr=0x" << std::hex << loc.GetAddress().GetLoadAddress(target) << std::dec;
            if (j + 1 < bp.GetNumLocations()) std::cout << ", ";
        }
        std::cout << "\n";
    }
}

bool DebugSession::continueExec()
{
    if (!process.IsValid()) return false;
    auto err = process.Continue();
    return err.Success();
}

bool DebugSession::stepOver()
{
    if (!process.IsValid()) return false;
    auto t = process.GetSelectedThread();
    lldb::SBError err;
    t.StepOver(lldb::eOnlyDuringStepping, err);
    return err.Success();
}

bool DebugSession::stepInto()
{
    if (!process.IsValid()) return false;
    auto t = process.GetSelectedThread();
    t.StepInto(lldb::eOnlyDuringStepping);
    return true;
}

bool DebugSession::stepOut()
{
    if (!process.IsValid()) return false;
    auto t = process.GetSelectedThread();
    lldb::SBError err;
    t.StepOut(err);
    return err.Success();
}

int DebugSession::frameCount() const
{
    if (!process.IsValid()) return 0;
    return process.GetSelectedThread().GetNumFrames();
}

bool DebugSession::selectFrame(int index)
{
    if (!process.IsValid()) return false;
    int n = process.GetSelectedThread().GetNumFrames();
    if (index < 0 || index >= n) return false;
    selectedFrame = index;
    return true;
}

int DebugSession::currentFrameIndex() const { return selectedFrame; }

std::string DebugSession::frameDescription() const
{
    if (!process.IsValid() || !isStopped()) return "no frame";
    auto frame = process.GetSelectedThread().GetFrameAtIndex(selectedFrame);
    return frame.GetFunctionName();
}

std::vector<std::string> DebugSession::locals() const
{
    std::vector<std::string> out;
    if (!isStopped()) return out;
    auto frame = process.GetSelectedThread().GetFrameAtIndex(selectedFrame);
    auto vars = frame.GetVariables(true, false, false, false);
    for (int i = 0; i < vars.GetSize(); ++i)
    {
        auto v = vars.GetValueAtIndex(i);
        std::ostringstream os;
        os << v.GetTypeName() << " " << v.GetName() << " = " << v.GetValue();
        out.push_back(os.str());
    }
    return out;
}

std::vector<std::string> DebugSession::args() const
{
    std::vector<std::string> out;
    if (!isStopped()) return out;
    auto frame = process.GetSelectedThread().GetFrameAtIndex(selectedFrame);
    auto vars = frame.GetVariables(false, true, false, false);
    for (int i = 0; i < vars.GetSize(); ++i)
    {
        auto v = vars.GetValueAtIndex(i);
        std::ostringstream os;
        os << v.GetTypeName() << " " << v.GetName() << " = " << v.GetValue();
        out.push_back(os.str());
    }
    return out;
}

std::string DebugSession::printExpr(const std::string& expr) const
{
    if (!isStopped()) return "process not stopped";
    auto frame = process.GetSelectedThread().GetFrameAtIndex(selectedFrame);
    auto value = frame.EvaluateExpression(expr.c_str());
    if (!value.IsValid()) return "error: cannot evaluate '" + expr + "'";
    if (value.GetError().Fail())
        return std::string("error: ") + value.GetError().GetCString();
    std::ostringstream os;
    os << value.GetTypeName() << " " << expr << " = " << value.GetValue();
    return os.str();
}

std::string DebugSession::sourceLine(int contextLines) const
{
    if (!isStopped()) return "";
    auto frame = process.GetSelectedThread().GetFrameAtIndex(selectedFrame);
    auto lineEntry = frame.GetLineEntry();
    auto spec = lineEntry.GetFileSpec();
    if (!spec.IsValid() || lineEntry.GetLine() == 0) return "";
    lldb::SBStream stream;
    int ctx = contextLines > 0 ? contextLines : 0;
    dbg.GetSourceManager().DisplaySourceLinesWithLineNumbers(
        spec, lineEntry.GetLine(), ctx, ctx, "", stream);
    std::ostringstream os;
    os << spec.GetFilename() << ":" << lineEntry.GetLine() << "\n"
       << stream.GetData();
    return os.str();
}

void DebugSession::destroy()
{
    if (process.IsValid()) process.Kill();
    if (target.IsValid()) dbg.DeleteTarget(target);
    process = lldb::SBProcess();
    target = lldb::SBTarget();
}
