#ifndef PDB_DAP_SERVER_H
#define PDB_DAP_SERVER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <lldb/API/LLDB.h>
#include "llvm/Support/JSON.h"

// pdb DAP 服务器：stdio 上运行 Debug Adapter Protocol，供 VSCode 调试 PLang。
// 复用 LLVM 的 llvm::json 做序列化（与 pLang LSP 同方案）。
class DapServer
{
public:
    void run();

private:
    // ---- JSON-RPC 消息层 ----
    std::string readMessage();
    void sendMessage(const std::string& body);
    void sendResponse(int seq, const std::string& command, const llvm::json::Value& body);
    void sendError(int seq, const std::string& message);
    void sendEvent(const std::string& event, const llvm::json::Value& body);

    // ---- DAP 方法 ----
    llvm::json::Value handleRequest(const std::string& command, const llvm::json::Object* args);
    llvm::json::Value doInitialize();
    bool doLaunch(const llvm::json::Object* args);
    llvm::json::Value doSetBreakpoints(const llvm::json::Object* args);
    bool doConfigurationDone();
    llvm::json::Value doThreads();
    llvm::json::Value doStackTrace(const llvm::json::Object* args);
    llvm::json::Value doScopes(const llvm::json::Object* args);
    llvm::json::Value doVariables(const llvm::json::Object* args);
    llvm::json::Value doContinue(const llvm::json::Object* args);
    llvm::json::Value doStep(const llvm::json::Object* args, int mode); // 1=next 2=stepIn 3=stepOut
    llvm::json::Value doEvaluate(const llvm::json::Object* args);
    void executeContinue();     // 阻塞执行（响应后调用）
    void executeStep(int mode);
    void executeLaunch();
    void broadcastState();      // stopped/exited 事件

    // 停止后广播 stopped 事件
    void notifyStopped();

    // ---- LLDB ----
    lldb::SBDebugger dbg;
    lldb::SBTarget target;
    lldb::SBProcess process;
    bool launched = false;

    // 帧 → 变量引用：1 = 当前帧 locals+args 顶层
    int frameVarsRef = 1;
    std::unordered_map<int, std::string> childVars; // childRef -> 表达式路径（简化：存名字）
    int nextChildRef = 100;
};

#endif
