// MCP: Streamable HTTP без сесій і без SSE, кожна відповідь — звичайний JSON.
#include "app.h"

static const char MCP_TOOLS[] PROGMEM = R"JSON({"tools":[
{"name":"set_status","description":"Set the colour of the physical agent status light. Call with state=busy when you start working, state=waiting when you need input or approval from the user, state=done when the task is finished, state=error on failure, state=idle to switch the light off.",
"inputSchema":{"type":"object","properties":{
"state":{"type":"string","enum":["idle","busy","waiting","done","error"]},
"agent_id":{"type":"string","description":"Stable name of this agent or session, so several agents can share one light. Default: \"default\"."},
"name":{"type":"string","description":"Optional human-readable name of the agent or project."},
"task":{"type":"string","description":"Optional one-line summary of the task you are working on. Kept until you send a new one."},
"message":{"type":"string","description":"Optional short note about what you are doing right now."}},
"required":["state"]}},
{"name":"get_status","description":"Get the current state of the agent status light and the list of agents that reported a status.",
"inputSchema":{"type":"object","properties":{}}}
]})JSON";

void handleMcp() {
  JsonDocument req, res;
  res["jsonrpc"] = "2.0";
  if (deserializeJson(req, server.arg("plain")) || !req.is<JsonObject>()) {
    res["id"] = nullptr;
    res["error"]["code"] = -32700;
    res["error"]["message"] = "Parse error";
    return sendJson(400, res);
  }
  if (req["id"].isNull()) return server.send(202, "text/plain", "");  // notifications/* і відповіді клієнта
  res["id"] = req["id"];
  String method = req["method"] | "";

  if (method == "initialize") {
    JsonObject r = res["result"].to<JsonObject>();
    r["protocolVersion"] = req["params"]["protocolVersion"] | "2025-03-26";
    r["capabilities"]["tools"].to<JsonObject>();
    r["serverInfo"]["name"] = "agentlight";
    r["serverInfo"]["version"] = FW_VERSION;
    r["instructions"] = "Physical status light. Report your state with set_status: busy while working, waiting when you need the user, done when finished.";
  } else if (method == "ping") {
    res["result"].to<JsonObject>();
  } else if (method == "tools/list") {
    res["result"] = serialized(FPSTR(MCP_TOOLS));
  } else if (method == "tools/call") {
    String name = req["params"]["name"] | "";
    const char* err = nullptr;
    if (name == "set_status") err = applyStatus(req["params"]["arguments"]);
    else if (name != "get_status") err = "unknown tool";
    String text = err;
    if (!err) {
      JsonDocument st;
      fillStatus(st.to<JsonObject>());
      st.remove("wifi");
      serializeJson(st, text);
    }
    JsonObject r = res["result"].to<JsonObject>();
    JsonObject c = r["content"].add<JsonObject>();
    c["type"] = "text";
    c["text"] = text;
    r["isError"] = err != nullptr;
  } else {
    res["error"]["code"] = -32601;
    res["error"]["message"] = "Method not found";
  }
  sendJson(200, res);
}
