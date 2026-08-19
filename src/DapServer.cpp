#include "DapServer.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static std::string jsonToString(const llvm::json::Value& v)
{
    std::string s;
    llvm::raw_string_ostream os(s);
    os << v;
    os.flush();
    return s;
}

// 一次读一条 Content-Length 帧消息
std::string DapServer::readMessage()
{
    std::string line;
    int contentLength = -1;
    while (std::getline(std::cin, line))
    {
        if (line.empty() || line == "\r") break;
        size_t colon = line.find(':');
        if (colon != std::string::npos && line.substr(0, colon) == "Content-Length")
        {
            contentLength = std::stoi(line.substr(colon + 1));
        }
    }
    if (contentLength < 0) return "";
    std::string body(contentLength, '\0');
    std::cin.read(&body[0], contentLength);
    return body;
}

void DapServer::sendMessage(const std::string& body)
{
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

void DapServer::sendResponse(int seq, const std::string& command, const llvm::json::Value& body)
{
    llvm::json::Object obj;
    obj["seq"] = 0;
    obj["type"] = "response";
    obj["request_seq"] = seq;
    obj["success"] = true;
    obj["command"] = command;
    if (body.kind() != llvm::json::Value::Null) obj["body"] = body;
    sendMessage(jsonToString(llvm::json::Value(std::move(obj))));
}

void DapServer::sendError(int seq, const std::string& message)
{
    llvm::json::Object obj;
    obj["seq"] = 0;
    obj["type"] = "response";
    obj["request_seq"] = seq;
    obj["success"] = false;
    obj["command"] = "launch";
    obj["message"] = message;
    sendMessage(jsonToString(llvm::json::Value(std::move(obj))));
}

void DapServer::sendEvent(const std::string& event, const llvm::json::Value& body)
{
    llvm::json::Object obj;
    obj["seq"] = 0;
    obj["type"] = "event";
    obj["event"] = event;
    if (body.kind() != llvm::json::Value::Null) obj["body"] = body;
    sendMessage(jsonToString(llvm::json::Value(std::move(obj))));
}

// 顶层变量 → DAP 变量数组（含子值引用，支持结构体展开）
static llvm::json::Array valueList(lldb::SBFrame frame, int baseRef,
                                   std::unordered_map<int, std::string>& childVars,
                                   int& nextChildRef, bool isArgs)
{
    llvm::json::Array out;
    auto vars = frame.GetVariables(isArgs, !isArgs, false, false);
    for (int i = 0; i < vars.GetSize(); ++i)
    {
        auto v = vars.GetValueAtIndex(i);
        if (!v.IsValid()) continue;
        llvm::json::Object item;
        item["name"] = v.GetName();
        item["value"] = v.GetValue() ? v.GetValue() : "";
        item["type"] = v.GetTypeName() ? v.GetTypeName() : "";
        if (v.GetNumChildren() > 0)
        {
            int ref = nextChildRef++;
            childVars[ref] = std::string(v.GetName());
            item["variablesReference"] = ref;
        }
        else
        {
            item["variablesReference"] = 0;
        }
        out.push_back(std::move(item));
    }
    return out;
}

llvm::json::Value DapServer::doInitialize()
{
    llvm::json::Object cap;
    cap["supportsConfigurationDoneRequest"] = true;
    cap["supportsEvaluateForHovers"] = true;
    cap["supportsFunctionBreakpoints"] = false;
    cap["supportsConditionalBreakpoints"] = false;
    cap["supportsSetVariable"] = false;
    llvm::json::Object out;
    out["capabilities"] = std::move(cap);
    return llvm::json::Value(std::move(out));
}

bool DapServer::doLaunch(const llvm::json::Object* args)
{
    if (!args) return false;
    auto it = args->getString("program");
    if (!it) return false;
    if (!dbg.IsValid())
    {
        dbg = lldb::SBDebugger::Create();
        if (!dbg.IsValid()) return false;
        dbg.SetAsync(false);
    }
    target = dbg.CreateTarget(it->str().c_str());
    return target.IsValid();
}

llvm::json::Value DapServer::doSetBreakpoints(const llvm::json::Object* args)
{
    llvm::json::Array out;
    if (!args || !target.IsValid()) return llvm::json::Value(std::move(out));

    std::string path;
    if (auto src = args->getObject("source"))
        if (auto p = src->getString("path")) path = *p;

    // 清除该文件现有断点
    for (int i = 0; i < target.GetNumBreakpoints(); ++i)
    {
        auto bp = target.GetBreakpointAtIndex(i);
        auto loc = bp.GetLocationAtIndex(0);
        auto spec = loc.GetAddress().GetLineEntry().GetFileSpec();
        if (spec.IsValid() && path.find(spec.GetFilename()) != std::string::npos)
        {
            target.BreakpointDelete(bp.GetID());
            --i;
        }
    }

    if (auto bps = args->getArray("breakpoints"))
    {
        for (auto& b : *bps)
        {
            llvm::json::Object item;
            item["verified"] = false;
            if (auto* obj = b.getAsObject())
            {
                if (auto line = obj->getInteger("line"))
                {
                    auto bp = target.BreakpointCreateByLocation(path.c_str(), (int)*line);
                    bool ok = bp.IsValid() && bp.GetNumLocations() > 0;
                    item["verified"] = ok;
                    item["line"] = (int)*line;
                    if (ok)
                        item["source"] = llvm::json::Object{{"path", path}};
                }
            }
            out.push_back(std::move(item));
        }
    }
    llvm::json::Object result;
    result["breakpoints"] = std::move(out);
    return llvm::json::Value(std::move(result));
}

bool DapServer::doConfigurationDone()
{
    return target.IsValid() && !launched;
}

void DapServer::notifyStopped()
{
    sendEvent("stopped", llvm::json::Object{{"reason", "breakpoint"},
                                            {"threadId", 1},
                                            {"allThreadsStopped", true}});
}

llvm::json::Value DapServer::doThreads()
{
    llvm::json::Array threads;
    threads.push_back(llvm::json::Object{{"id", 1}, {"name", "main"}});
    return llvm::json::Value(llvm::json::Object{{"threads", std::move(threads)}});
}

llvm::json::Value DapServer::doStackTrace(const llvm::json::Object* args)
{
    llvm::json::Array frames;
    if (process.IsValid() && process.GetState() == lldb::eStateStopped)
    {
        auto t = process.GetSelectedThread();
        int n = t.GetNumFrames();
        for (int i = 0; i < n && i < 100; ++i)
        {
            auto f = t.GetFrameAtIndex(i);
            auto le = f.GetLineEntry();
            auto spec = le.GetFileSpec();
            llvm::json::Object frame;
            frame["id"] = i;
            frame["name"] = f.GetFunctionName() ? f.GetFunctionName() : "(unknown)";
            if (spec.IsValid() && le.GetLine() > 0)
            {
                frame["line"] = (int)le.GetLine();
                frame["column"] = (int)le.GetColumn();
                frame["source"] = llvm::json::Object{
                    {"name", spec.GetFilename()},
                    {"path", spec.GetDirectory() && spec.GetDirectory()[0]
                        ? std::string(spec.GetDirectory()) + "/" + spec.GetFilename()
                        : std::string(spec.GetFilename())}};
            }
            else
            {
                frame["line"] = 0;
                frame["column"] = 0;
            }
            frames.push_back(std::move(frame));
        }
    }
    return llvm::json::Value(llvm::json::Object{{"stackFrames", std::move(frames)}});
}

llvm::json::Value DapServer::doScopes(const llvm::json::Object* args)
{
    llvm::json::Array scopes;
    if (args && args->getInteger("frameId"))
    {
        scopes.push_back(llvm::json::Object{
            {"name", "Locals"}, {"variablesReference", 1}, {"expensive", false}});
        scopes.push_back(llvm::json::Object{
            {"name", "Arguments"}, {"variablesReference", 2}, {"expensive", false}});
    }
    return llvm::json::Value(llvm::json::Object{{"scopes", std::move(scopes)}});
}

llvm::json::Value DapServer::doVariables(const llvm::json::Object* args)
{
    llvm::json::Array out;
    if (!args || !process.IsValid() || process.GetState() != lldb::eStateStopped)
        return llvm::json::Value(std::move(out));
    auto frame = process.GetSelectedThread().GetFrameAtIndex(0);
    int ref = 0;
    if (auto r = args->getInteger("variablesReference")) ref = (int)*r;

    if (ref == 1 || ref == 2)
    {
        bool isArgs = (ref == 2);
        out = valueList(frame, ref, childVars, nextChildRef, isArgs);
    }
    else
    {
        // 子值展开（结构体成员）：按名字从表达式路径取
        auto it = childVars.find(ref);
        if (it != childVars.end())
        {
            auto val = frame.EvaluateExpression(it->second.c_str());
            if (val.IsValid() && val.GetError().Success())
            {
                for (int i = 0; i < val.GetNumChildren(); ++i)
                {
                    auto c = val.GetChildAtIndex(i);
                    if (!c.IsValid()) continue;
                    llvm::json::Object item;
                    item["name"] = c.GetName() ? c.GetName() : "";
                    item["value"] = c.GetValue() ? c.GetValue() : "";
                    item["type"] = c.GetTypeName() ? c.GetTypeName() : "";
                    item["variablesReference"] = 0;
                    out.push_back(std::move(item));
                }
            }
        }
    }
    llvm::json::Object result;
    result["variables"] = std::move(out);
    return llvm::json::Value(std::move(result));
}

llvm::json::Value DapServer::doContinue(const llvm::json::Object* args)
{
    return llvm::json::Value(llvm::json::Object{{"allThreadsContinued", true}});
}

llvm::json::Value DapServer::doStep(const llvm::json::Object* args, int mode)
{
    return nullptr;
}

// 实际执行（响应之后调用）：continue/单步/启动，阻塞到停止或退出
void DapServer::executeContinue()
{
    if (process.IsValid())
        process.Continue();
}

void DapServer::executeStep(int mode)
{
    if (!process.IsValid() || process.GetState() != lldb::eStateStopped) return;
    auto t = process.GetSelectedThread();
    lldb::SBError err;
    if (mode == 1) t.StepOver(lldb::eOnlyDuringStepping, err);
    else if (mode == 2) t.StepInto(lldb::eOnlyDuringStepping);
    else t.StepOut(err);
}

void DapServer::executeLaunch()
{
    if (!target.IsValid() || launched) return;
    std::vector<const char*> argv;
    argv.push_back(nullptr);
    lldb::SBLaunchInfo info(argv.data());
    lldb::SBError err;
    process = target.Launch(info, err);
    if (process.IsValid()) launched = true;
}

// 停止/退出后广播事件（响应之后调用）
void DapServer::broadcastState()
{
    auto state = process.GetState();
    if (state == lldb::eStateStopped)
    {
        notifyStopped();
    }
    else if (state == lldb::eStateExited || state == lldb::eStateCrashed)
    {
        sendEvent("exited", llvm::json::Object{{"exitCode", 0}});
        sendEvent("terminated", nullptr);
    }
}

llvm::json::Value DapServer::doEvaluate(const llvm::json::Object* args)
{
    if (!args || !process.IsValid() || process.GetState() != lldb::eStateStopped)
        return llvm::json::Value(llvm::json::Object{{"result", "error: not stopped"},
                                                    {"variablesReference", 0}});
    std::string expr;
    if (auto e = args->getString("expression")) expr = *e;
    auto frame = process.GetSelectedThread().GetFrameAtIndex(0);
    auto val = frame.EvaluateExpression(expr.c_str());
    llvm::json::Object out;
    if (val.IsValid() && val.GetError().Success())
    {
        out["result"] = val.GetValue() ? val.GetValue() : "";
        out["type"] = val.GetTypeName() ? val.GetTypeName() : "";
        out["variablesReference"] = 0;
    }
    else
    {
        out["result"] = "error: cannot evaluate";
        out["variablesReference"] = 0;
    }
    return llvm::json::Value(std::move(out));
}

llvm::json::Value DapServer::handleRequest(const std::string& command, const llvm::json::Object* args)
{
    if (command == "initialize") return doInitialize();
    if (command == "launch") return doLaunch(args) ? llvm::json::Value("ok") : llvm::json::Value("failed");
    if (command == "setBreakpoints") return doSetBreakpoints(args);
    if (command == "configurationDone") return nullptr;
    if (command == "threads") return doThreads();
    if (command == "stackTrace") return doStackTrace(args);
    if (command == "scopes") return doScopes(args);
    if (command == "variables") return doVariables(args);
    if (command == "continue") return doContinue(args);
    if (command == "next") return doStep(args, 1);
    if (command == "stepIn") return doStep(args, 2);
    if (command == "stepOut") return doStep(args, 3);
    if (command == "evaluate") return doEvaluate(args);
    if (command == "disconnect") return nullptr;
    return nullptr;
}

void DapServer::run()
{
    while (true)
    {
        std::string body = readMessage();
        if (body.empty()) break;
        auto parsed = llvm::json::parse(body);
        if (!parsed) continue;
        auto* obj = parsed->getAsObject();
        if (!obj) continue;

        auto type = obj->getString("type");
        if (type && *type == "request")
        {
            auto command = obj->getString("command");
            auto seq = obj->getInteger("seq");
            auto* args = obj->getObject("arguments");
            if (command && seq)
            {
                auto result = handleRequest(command->str(), args);
                if (*command == "launch" && result.getAsString() &&
                    *result.getAsString() == "failed")
                    sendError((int)*seq, "launch failed: cannot create target");
                else
                    sendResponse((int)*seq, command->str(), result);
                // 执行类命令：响应已发，再执行并广播状态
                if (*command == "configurationDone")
                {
                    if (doConfigurationDone()) executeLaunch();
                    broadcastState();
                }
                else if (*command == "continue") { executeContinue(); broadcastState(); }
                else if (*command == "next") { executeStep(1); broadcastState(); }
                else if (*command == "stepIn") { executeStep(2); broadcastState(); }
                else if (*command == "stepOut") { executeStep(3); broadcastState(); }
                if (*command == "disconnect") break;
            }
        }
    }
    // 清理
    if (process.IsValid()) process.Kill();
    if (target.IsValid()) dbg.DeleteTarget(target);
}
