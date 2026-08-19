// pdb —— PLang 调试器（GDB 风格 CLI，基于 liblldb）
// 用法：pdb <executable>

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>
#include "DebugSession.h"
#include "DapServer.h"

static std::vector<std::string> splitArgs(const std::string& line)
{
    std::vector<std::string> out;
    std::istringstream is(line);
    std::string tok;
    while (is >> tok) out.push_back(tok);
    return out;
}

static void printHelp()
{
    std::cout <<
        "pdb 命令：\n"
        "  run [args...]          运行程序（可重复运行）\n"
        "  break <file:line>      按文件行号设断点\n"
        "  break <func>           按函数名设断点\n"
        "  info break             列出断点\n"
        "  next (n)               单步跳过\n"
        "  step (s)               单步进入\n"
        "  finish                 运行到当前函数返回\n"
        "  continue (c)           继续运行\n"
        "  print <expr> (p)       打印变量或表达式\n"
        "  bt                     栈回溯\n"
        "  frame <n>              切换栈帧\n"
        "  info locals            当前帧局部变量\n"
        "  info args              当前帧参数\n"
        "  list [n]               显示当前源码（n 行上下文）\n"
        "  quit (q)               退出\n"
        "  help (h)               帮助\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: pdb <executable>\n";
        return 1;
    }
    // DAP 模式：pdb --dap [program]
    if (argc >= 2 && std::string(argv[1]) == "--dap")
    {
        lldb::SBDebugger::Initialize();
        DapServer server;
        server.run();
        lldb::SBDebugger::Terminate();
        return 0;
    }
    std::string exe = argv[1];

    lldb::SBDebugger::Initialize();
    DebugSession session;

    std::cout << "pdb (PLang debugger) " << lldb::SBDebugger::GetVersionString() << "\n";
    if (!session.open(exe))
    {
        std::cerr << "error: cannot open '" << exe << "'\n";
        lldb::SBDebugger::Terminate();
        return 1;
    }
    std::cout << "target: " << exe << "\n";
    std::cout << "type 'help' for command list\n";

    bool running = false;
    std::string line;
    while (true)
    {
        std::cout << (running ? "(pdb) " : "(pdb) ");
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        auto args = splitArgs(line);
        const std::string& cmd = args[0];

        if (cmd == "quit" || cmd == "q") break;
        else if (cmd == "help" || cmd == "h") printHelp();

        else if (cmd == "run")
        {
            std::vector<std::string> progArgs(args.begin() + 1, args.end());
            if (session.run(progArgs))
            {
                running = true;
                if (session.isStopped())
                    std::cout << session.sourceLine(2) << std::flush;
                else
                    std::cout << "process exited\n";
            }
            else
                std::cerr << "error: failed to launch\n";
        }
        else if (cmd == "break" || cmd == "b")
        {
            if (args.size() < 2) { std::cout << "usage: break <file:line | func>\n"; continue; }
            std::string spec = args[1];
            size_t colon = spec.rfind(':');
            bool ok;
            if (colon != std::string::npos)
            {
                std::string file = spec.substr(0, colon);
                int line = std::atoi(spec.substr(colon + 1).c_str());
                ok = line > 0 && session.breakAtLine(file, line);
            }
            else
                ok = session.breakAtFunction(spec);
            std::cout << (ok ? "breakpoint set\n" : "warning: breakpoint not resolved\n");
        }
        else if (cmd == "info")
        {
            if (args.size() < 2) { std::cout << "usage: info <break|locals|args>\n"; continue; }
            if (args[1] == "break") session.listBreakpoints();
            else if (args[1] == "locals")
            {
                for (auto& v : session.locals()) std::cout << v << "\n";
            }
            else if (args[1] == "args")
            {
                for (auto& v : session.args()) std::cout << v << "\n";
            }
            else std::cout << "unknown info: " << args[1] << "\n";
        }
        else if (cmd == "next" || cmd == "n")
        {
            if (session.stepOver()) std::cout << session.sourceLine(2) << std::flush;
            else std::cout << "not stopped\n";
        }
        else if (cmd == "step" || cmd == "s")
        {
            if (session.stepInto()) std::cout << session.sourceLine(2) << std::flush;
            else std::cout << "not stopped\n";
        }
        else if (cmd == "finish")
        {
            if (session.stepOut()) std::cout << session.sourceLine(2) << std::flush;
            else std::cout << "not stopped\n";
        }
        else if (cmd == "continue" || cmd == "c")
        {
            if (!session.continueExec()) std::cout << "not stopped\n";
            else if (session.isStopped()) std::cout << session.sourceLine(2) << std::flush;
            else std::cout << "process exited\n";
        }
        else if (cmd == "print" || cmd == "p")
        {
            if (args.size() < 2) { std::cout << "usage: print <expr>\n"; continue; }
            std::string expr = line.substr(line.find(cmd) + cmd.size());
            size_t sp = expr.find_first_not_of(" \t");
            if (sp != std::string::npos) expr = expr.substr(sp);
            std::cout << session.printExpr(expr) << "\n";
        }
        else if (cmd == "bt" || cmd == "backtrace")
        {
            if (!session.isStopped()) { std::cout << "not stopped\n"; continue; }
            for (int i = 0; i < session.frameCount(); ++i)
            {
                std::cout << "#" << i << "  ";
                session.selectFrame(i);
                std::cout << session.frameDescription() << "\n";
            }
            session.selectFrame(0);
        }
        else if (cmd == "frame")
        {
            if (args.size() < 2) { std::cout << "usage: frame <n>\n"; continue; }
            int idx = std::atoi(args[1].c_str());
            if (session.selectFrame(idx))
                std::cout << "#" << idx << "  " << session.frameDescription()
                          << "\n" << session.sourceLine(2) << std::flush;
            else
                std::cout << "invalid frame index\n";
        }
        else if (cmd == "list" || cmd == "l")
        {
            int ctx = args.size() > 1 ? std::atoi(args[1].c_str()) : 0;
            std::cout << session.sourceLine(ctx);
        }
        else
        {
            std::cout << "unknown command: " << cmd << "  (help for list)\n";
        }
    }

    session.destroy();
    lldb::SBDebugger::Terminate();
    return 0;
}
